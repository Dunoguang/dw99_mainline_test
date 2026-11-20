/*
 * Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/types.h>
#include <linux/vt_kern.h>
#include <linux/vmalloc.h>
#include <linux/sipc.h>
#include <linux/io.h>
#include "alignment/sitm.h"

#include <linux/wcn_integrate_platform.h>


#include "tty.h"
#include "rfkill.h"
#include "dump.h"
#include "lpm.h"

#define STTY_DEV_MAX_NR		1
#define STTY_MAX_DATA_LEN	4096
#define STTY_STATE_OPEN		1
#define STTY_STATE_CLOSE	0
#define COMMAND_HEAD        1
#define STTY_SBUF_WRITE_TIMEOUT_MS 3000

static unsigned int log_level = MTTY_LOG_LEVEL_DEBUG;

#define BT_VER(fmt, ...)						\
	do {										\
		if (log_level >= MTTY_LOG_LEVEL_VER)	\
			pr_err(fmt, ##__VA_ARGS__);			\
	} while (0)
#define BT_DEBUG(fmt, ...)						\
	do {										\
		if (log_level >= MTTY_LOG_LEVEL_DEBUG)	\
			pr_err(fmt, ##__VA_ARGS__);			\
	} while (0)

struct stty_device {
	struct stty_init_data pdata;
	struct tty_port port;
	struct tty_driver *driver;
	struct mutex state_lock;
	struct mutex tx_lock;
	unsigned int openers;
	bool removed;
};

static atomic_t stty_claimed = ATOMIC_INIT(0);
static bool is_user_debug;
static bool is_dumped;

static ssize_t dumpmem_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct stty_device *stty = dev_get_drvdata(dev);

	if (!count)
		return -EINVAL;
	mutex_lock(&stty->state_lock);
	if (buf[0] == 2 || sysfs_streq(buf, "2")) {
		WRITE_ONCE(is_user_debug, true);
	} else if (!is_dumped) {
		bt_host_data_printf();
		is_dumped = true;
	}
	mutex_unlock(&stty->state_lock);
	return count;
}

static void hex_dump(unsigned char *bin, size_t binsz)
{
  char *str, hex_str[]= "0123456789ABCDEF";
  size_t i;

  str = (char *)vmalloc(binsz * 3);
  if (!str) {
    return;
  }

  for (i = 0; i < binsz; i++) {
      str[(i * 3) + 0] = hex_str[(bin[i] >> 4) & 0x0F];
      str[(i * 3) + 1] = hex_str[(bin[i]     ) & 0x0F];
      str[(i * 3) + 2] = ' ';
  }
  str[(binsz * 3) - 1] = 0x00;
  pr_info("%s\n", str);
  vfree(str);
}

static void hex_dump_block(unsigned char *bin, size_t binsz)
{
#define HEX_DUMP_BLOCK_SIZE 8
	int loop = binsz / HEX_DUMP_BLOCK_SIZE;
	int tail = binsz % HEX_DUMP_BLOCK_SIZE;
	int i;

	if (!loop) {
		hex_dump(bin, binsz);
		return;
	}

	for (i = 0; i < loop; i++) {
		hex_dump(bin + i * HEX_DUMP_BLOCK_SIZE, HEX_DUMP_BLOCK_SIZE);
	}

	if (tail)
		hex_dump(bin + i * HEX_DUMP_BLOCK_SIZE, tail);
}

static ssize_t chipid_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i = 0, id;

	id = wcn_get_aon_chip_id();
	pr_info("%s: %d", __func__, id);

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", id);
	return i;
}

static DEVICE_ATTR_RO(chipid);
static DEVICE_ATTR_WO(dumpmem);

static struct attribute *bluetooth_attrs[] = {
	&dev_attr_chipid.attr,
	&dev_attr_dumpmem.attr,
	NULL,
};

static struct attribute_group bluetooth_group = {
	.name = NULL,
	.attrs = bluetooth_attrs,
};


static void stty_handler(int event, void *data)
{
	struct stty_device *stty = data;
	struct stty_init_data *pdata = &stty->pdata;
	unsigned char *buf;
	int count;

	if (event != SBUF_NOTIFY_READ)
		return;
	bt_wakeup_host();
	buf = kmalloc(STTY_MAX_DATA_LEN, GFP_KERNEL);
	if (!buf)
		return;

	do {
		count = sbuf_read(pdata->dst, pdata->channel, pdata->rx_bufid,
				  buf, STTY_MAX_DATA_LEN, 0);
		if (count <= 0)
			break;
		mutex_lock(&stty->state_lock);
		if (stty->openers && !stty->removed) {
			int copied;

			if (READ_ONCE(is_user_debug))
				bt_host_data_save(buf, count, BT_DATA_IN);
			copied = tty_insert_flip_string(&stty->port, buf, count);
			tty_flip_buffer_push(&stty->port);
			if (copied != count)
				pr_warn_ratelimited("ttyBT: receive buffer accepted %d/%d bytes\n",
						    copied, count);
		}
		mutex_unlock(&stty->state_lock);
	} while (count == STTY_MAX_DATA_LEN);

	kfree(buf);
}

static int stty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct stty_device *stty = driver->driver_state;
	int ret;

	tty_port_get(&stty->port);
	ret = tty_port_install(&stty->port, driver, tty);
	if (ret)
		tty_port_put(&stty->port);
	else
		tty->driver_data = stty;
	return ret;
}

static void stty_cleanup(struct tty_struct *tty)
{
	struct stty_device *stty = tty->driver_data;

	tty_port_put(&stty->port);
}

static int stty_open(struct tty_struct *tty, struct file *file)
{
	struct stty_device *stty = tty->driver_data;
	int ret = 0;

	mutex_lock(&stty->tx_lock);
	mutex_lock(&stty->state_lock);
	if (stty->removed) {
		ret = -ENODEV;
		goto unlock;
	}
	if (!stty->openers) {
		ret = sbuf_status(stty->pdata.dst, stty->pdata.channel);
		if (ret)
			goto unlock;
		ret = sitm_ini();
		if (ret)
			goto unlock;
		bt_host_data_reset();
		tty_port_tty_set(&stty->port, tty);
	}
	stty->openers++;
unlock:
	mutex_unlock(&stty->state_lock);
	mutex_unlock(&stty->tx_lock);
	return ret;
}

static void stty_close(struct tty_struct *tty, struct file *file)
{
	struct stty_device *stty = tty->driver_data;

	mutex_lock(&stty->tx_lock);
	mutex_lock(&stty->state_lock);
	if (stty->openers && !--stty->openers) {
		tty_port_tty_set(&stty->port, NULL);
		sitm_cleanup();
	}
	mutex_unlock(&stty->state_lock);
	mutex_unlock(&stty->tx_lock);
}

static int stty_data_transmit(void *context, u8 *buf, size_t count)
{
	struct stty_device *stty = context;
	struct stty_init_data *pdata = &stty->pdata;
	size_t sent = 0;
	int ret;

	if (READ_ONCE(is_user_debug))
		bt_host_data_save(buf, count, BT_DATA_OUT);
	if (buf[0] == COMMAND_HEAD && log_level >= MTTY_LOG_LEVEL_DEBUG) {
		if (count <= 16) {
			hex_dump_block(buf, count);
		} else {
			hex_dump_block(buf, 8);
			hex_dump_block(buf + count - 8, 8);
		}
	}

	while (sent < count) {
		ret = sbuf_write(pdata->dst, pdata->channel, pdata->tx_bufid,
				 buf + sent, count - sent,
				 msecs_to_jiffies(STTY_SBUF_WRITE_TIMEOUT_MS));
		if (ret <= 0)
			return ret ? ret : -ETIMEDOUT;
		if (ret > count - sent)
			return -EIO;
		sent += ret;
	}
	return 0;
}

static ssize_t stty_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
	struct stty_device *stty = tty->driver_data;
	ssize_t ret;

	mutex_lock(&stty->tx_lock);
	if (stty->removed || !stty->openers)
		ret = -EIO;
	else
		ret = sitm_write(buf, min_t(size_t, count, INT_MAX),
				 stty_data_transmit, stty);
	mutex_unlock(&stty->tx_lock);
	return ret;
}

static unsigned int stty_write_room(struct tty_struct *tty)
{
	struct stty_device *stty = tty->driver_data;

	return READ_ONCE(stty->removed) ? 0 : STTY_MAX_DATA_LEN;
}

static const struct tty_operations stty_ops = {
	.install = stty_install,
	.cleanup = stty_cleanup,
	.open = stty_open,
	.close = stty_close,
	.write = stty_write,
	.write_room = stty_write_room,
};

static void stty_port_release(struct tty_port *port)
{
	kfree(container_of(port, struct stty_device, port));
}

static const struct tty_port_operations stty_port_ops = {
	.destruct = stty_port_release,
};

static int stty_driver_init(struct stty_device *stty)
{
	struct tty_driver *driver;
	int ret;

	driver = tty_alloc_driver(STTY_DEV_MAX_NR, 0);
	if (IS_ERR(driver))
		return PTR_ERR(driver);

	driver->driver_name = "ttyBT";
	driver->name = "ttyBT";
	driver->type = TTY_DRIVER_TYPE_SYSTEM;
	driver->subtype = SYSTEM_TYPE_TTY;
	driver->init_termios = tty_std_termios;
	driver->driver_state = stty;
	tty_set_operations(driver, &stty_ops);
	tty_port_link_device(&stty->port, driver, 0);
	ret = tty_register_driver(driver);
	if (ret) {
		tty_driver_kref_put(driver);
		return ret;
	}
	stty->driver = driver;
	return 0;
}

static int stty_parse_dt(struct stty_init_data *pdata, struct device *dev)
{
	struct device_node *np = dev->of_node;
	const char *name;
	u32 value;
	int ret;

	ret = of_property_read_string(np, "sprd,name", &name);
	if (ret || strcmp(name, "ttyBT"))
		return -ENODEV;
	ret = of_property_read_u32(np, "sprd,dst", &value);
	if (ret || value >= SIPC_ID_NR)
		return ret ?: -EINVAL;
	pdata->dst = value;
	ret = of_property_read_u32(np, "sprd,channel", &value);
	if (ret || value > U8_MAX)
		return ret ?: -EINVAL;
	pdata->channel = value;
	ret = of_property_read_u32(np, "sprd,tx_bufid", &pdata->tx_bufid);
	if (ret)
		return ret;
	return of_property_read_u32(np, "sprd,rx_bufid", &pdata->rx_bufid);
}

static int stty_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stty_device *stty;
	struct stty_init_data pdata;
	int ret;

	ret = stty_parse_dt(&pdata, dev);
	if (ret)
		return ret;
	if (atomic_cmpxchg(&stty_claimed, 0, 1))
		return -EBUSY;

	stty = kzalloc(sizeof(*stty), GFP_KERNEL);
	if (!stty) {
		ret = -ENOMEM;
		goto release_claim;
	}
	stty->pdata = pdata;
	mutex_init(&stty->state_lock);
	mutex_init(&stty->tx_lock);
	tty_port_init(&stty->port);
	stty->port.ops = &stty_port_ops;
	platform_set_drvdata(pdev, stty);
	ret = sbuf_register_notifier(pdata.dst, pdata.channel, pdata.rx_bufid,
				     stty_handler, stty);
	if (ret) {
		if (ret == -ENODEV)
			ret = -EPROBE_DEFER;
		goto put_port;
	}
	ret = sysfs_create_group(&dev->kobj, &bluetooth_group);
	if (ret)
		goto remove_notifier;
	ret = rfkill_bluetooth_init(pdev);
	if (ret)
		goto remove_group;
	ret = stty_driver_init(stty);
	if (ret)
		goto remove_rfkill;
	return 0;

remove_rfkill:
	rfkill_bluetooth_remove(pdev);
remove_group:
	sysfs_remove_group(&dev->kobj, &bluetooth_group);
remove_notifier:
	sbuf_register_notifier(pdata.dst, pdata.channel, pdata.rx_bufid,
			       NULL, NULL);
put_port:
	platform_set_drvdata(pdev, NULL);
	tty_port_put(&stty->port);
release_claim:
	atomic_set(&stty_claimed, 0);
	return ret;
}

static void stty_remove(struct platform_device *pdev)
{
	struct stty_device *stty = platform_get_drvdata(pdev);
	struct stty_init_data *pdata = &stty->pdata;

	mutex_lock(&stty->tx_lock);
	mutex_lock(&stty->state_lock);
	WRITE_ONCE(stty->removed, true);
	if (stty->openers) {
		stty->openers = 0;
		sitm_cleanup();
	}
	mutex_unlock(&stty->state_lock);
	mutex_unlock(&stty->tx_lock);

	sysfs_remove_group(&pdev->dev.kobj, &bluetooth_group);
	sbuf_register_notifier(pdata->dst, pdata->channel, pdata->rx_bufid,
			       NULL, NULL);
	rfkill_bluetooth_remove(pdev);
	tty_unregister_driver(stty->driver);
	tty_port_tty_vhangup(&stty->port);
	tty_port_tty_set(&stty->port, NULL);
	tty_driver_kref_put(stty->driver);
	tty_port_put(&stty->port);
	atomic_set(&stty_claimed, 0);
}

static const struct of_device_id stty_match_table[] = {
	{ .compatible = "sprd,wcn_internal_chip", },
	{ },
};

MODULE_DEVICE_TABLE(of, stty_match_table);

static struct platform_driver stty_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ttyBT",
		.of_match_table = stty_match_table,
	},
	.probe = stty_probe,
	.remove = stty_remove,
};

static int __init stty_init(void)
{
	int ret = bluesleep_init();

	if (ret)
		return ret;
	ret = platform_driver_register(&stty_driver);
	if (ret)
		bluesleep_exit();
	return ret;
}

static void __exit stty_exit(void)
{
	platform_driver_unregister(&stty_driver);
	bluesleep_exit();
}

late_initcall(stty_init);
module_exit(stty_exit);

MODULE_AUTHOR("Spreadtrum Bluetooth");
MODULE_DESCRIPTION("SIPC/stty driver");
MODULE_LICENSE("GPL");
