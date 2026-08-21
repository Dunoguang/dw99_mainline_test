
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/sysrq.h>
#include <linux/sched.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

//#include <mach/globalregs.h>
//#include <mach/hardware.h>
//#include <mach/board.h>
//#include <mach/gpio.h>
//#include <mach/adi.h>
//#include <mach/kpd.h>
//#include <mach/sci.h>
//#include <mach/sci_glb_regs.h>

//#include <linux/input-hook.h>
#ifdef CONFIG_OF
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#endif


//#define HALL_DEBUG_PRINTK  
#if defined(HALL_DEBUG_PRINTK)
#define DEBUG_PRINTK(a,arg...)	printk(HALL_DEBUG_PRINTK ": " a,##arg)
#else
#define DEBUG_PRINTK(arg...)
#endif

#ifndef CONFIG_OF
#define HALL_INT  63
#else
static int HALL_INT;
#endif


static unsigned int hall_state=0;  //0----->far   1---->near
#define HALL_NEAR_MASK 0x01

#define KPD_HALLKEY_FAR		KEY_F2
#define KPD_HALLKEY_CLOSE	KEY_F1
//#define KPD_HALLKEY_POLARITY  1
//#define KPD_HALLKEY_SENSITIVE  1
//#define KPD_HALLKEY_DEBOUNCE  5

//static u8 kpd_hallkey_state = !KPD_HALLKEY_POLARITY;

static struct workqueue_struct *hall_key_wq;

struct hall_key_data
{
	struct work_struct  work;
	struct input_dev *hall_input_dev;
	int irq;
};

static void hall_key_work_func(struct work_struct *work)
{
	//unsigned int hall_keycode;
	unsigned int gpio_state;
	struct hall_key_data *ts = NULL;
	ts = container_of(work, struct hall_key_data, work);

	gpio_state=gpio_get_value(HALL_INT);
	DEBUG_PRINTK("hall_key_work_func   gpio_state===%d\n",gpio_state);
	if(gpio_state)         //// Far 
	{
		hall_state=1;
		input_report_key(ts->hall_input_dev, KPD_HALLKEY_FAR, 1);
		msleep(10);
		input_report_key(ts->hall_input_dev, KPD_HALLKEY_FAR, 0);
		input_sync(ts->hall_input_dev);
	}
	else  ///near
	{
		hall_state=0;
		input_report_key(ts->hall_input_dev, KPD_HALLKEY_CLOSE, 1);
		msleep(10);
		input_report_key(ts->hall_input_dev, KPD_HALLKEY_CLOSE, 0);
		input_sync(ts->hall_input_dev);
	}

	enable_irq(ts->irq);
}




static irqreturn_t hall_key_irq_handler(int irq, void *dev_id)
{
	struct hall_key_data *ts = (struct hall_key_data*)dev_id;

	DEBUG_PRINTK("hall_key_irq_handler\n");
	disable_irq_nosync(ts ->irq);
	queue_work(hall_key_wq, &ts->work);
	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static int hall_key_parse_dt(
                struct device *dev)
{
	struct device_node *np = dev->of_node;

	HALL_INT = of_get_gpio(np, 0);
	if(HALL_INT < 0)
	{
		dev_err(dev, "no gpios of property specified\n");
		return -1;
	}
	return 0;
}
#else
static int hall_key_parse_dt(
                struct device *dev)
{
	return 0;
}
#endif


static ssize_t hall_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	hall_state = (hall_state&HALL_NEAR_MASK)?1:0;
	return scnprintf(buf, PAGE_SIZE, "%d\n", hall_state);	
}


static ssize_t hall_state_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	if (sysfs_streq(buf, "1"))
		hall_state = 1;
	else if (sysfs_streq(buf, "0"))
		hall_state = 0;
	return hall_state;
}

static struct device_attribute hall_state_attribute = __ATTR(hall_state,0444,hall_state_show,hall_state_store);


static struct attribute* sensors_hall_attrs [] =
{
	&hall_state_attribute.attr,
	NULL,
};

static const struct attribute_group hall_attrs_group = {
	.attrs = sensors_hall_attrs,
};


static int hall_key_probe(struct platform_device *pdev)
{
	s32 ret = -1;
	//struct device_node *np = pdev->dev.of_node;
	struct hall_key_data *hall_key;

	printk("hall_key_probe\n");

	hall_key = kzalloc(sizeof(*hall_key), GFP_KERNEL);
	if (hall_key == NULL) {
		printk("Alloc GFP_KERNEL memory failed.");
		return -ENOMEM;
	}

	hall_key_wq = create_singlethread_workqueue("hall_key_wq");
	if (!hall_key_wq) {
		printk("Creat hall_key_wq workqueue failed.");
		ret = -ENOMEM;
		goto err_kzalloc;    
	}
	memset(hall_key, 0, sizeof(*hall_key));

	if (pdev->dev.of_node)
	{
		ret=hall_key_parse_dt(&pdev->dev);
		if(ret<0)
			printk("hall_key_parse_dt get irq gpio fail\n");
	}

	hall_key->hall_input_dev = input_allocate_device();
	if (!hall_key->hall_input_dev)
	{
		return -ENOMEM;
		goto out3;
	}
	platform_set_drvdata(pdev,hall_key);

	__set_bit(KEY_POWER, hall_key->hall_input_dev->keybit);
	__set_bit(KPD_HALLKEY_CLOSE, hall_key->hall_input_dev->keybit);
	__set_bit(KPD_HALLKEY_FAR, hall_key->hall_input_dev->keybit);
	set_bit(EV_ABS, hall_key->hall_input_dev->evbit);
	set_bit(EV_KEY, hall_key->hall_input_dev->evbit);
	hall_key->hall_input_dev->name="hall_key";

	ret = input_register_device(hall_key->hall_input_dev);
	if (ret)
	{
	printk("Register %s input device failed", hall_key->hall_input_dev->name);
	goto out2;
	}

	ret= sysfs_create_group(&(hall_key->hall_input_dev->dev.kobj), &hall_attrs_group);
	if (ret) {
		dev_err(&hall_key->hall_input_dev->dev, "create device file failed!\n");
		ret = -EINVAL;
		goto out2;
	}

	gpio_request(HALL_INT, "hallkey");
	gpio_direction_input(HALL_INT);
	hall_key->irq=gpio_to_irq(HALL_INT);
	printk("hall key request irq success\n");
	INIT_WORK(&hall_key->work, hall_key_work_func);

	ret  = request_irq(hall_key->irq,
	hall_key_irq_handler,
	IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND,
	"hall_key",
	hall_key);
	if(ret)
	{
		printk("Request IRQ failed!ERRNO:%d.", ret);
		goto out1;
	}
	else
	{
		printk("hall key works in interrupt mode.");
		enable_irq(hall_key->irq);
	}

	return 0;

out1:
	input_free_device(hall_key->hall_input_dev);
	free_irq(hall_key->irq, pdev);
	sysfs_remove_group(&(hall_key->hall_input_dev->dev.kobj), &hall_attrs_group);

out2:
	input_unregister_device(hall_key->hall_input_dev);
out3:
	kfree(hall_key);

err_kzalloc:

	return ret;
}


static int hall_key_remove(struct platform_device *pdev)
{
	struct hall_key_data *hall_key=platform_get_drvdata(pdev);
	input_unregister_device(hall_key->hall_input_dev);
	kfree(hall_key);
	return 0;
}

static struct of_device_id hall_match_table[] = {
	{ .compatible = "sprd,hall_key", },
	{ },
};


struct platform_driver hall_key_driver = {
	.probe = hall_key_probe,
	.remove = hall_key_remove,
		.driver = {
		.name	  = "hall_key",
		.owner	  = THIS_MODULE,
		.of_match_table = hall_match_table,
	},
};

struct platform_device hall_key_device = {
	.name	  = "hall_key",
};

static int __init hall_key_init(void)
{
	printk("hall_key_init\n");

	return platform_driver_register(&hall_key_driver);
}

static void __exit hall_key_exit(void)
{
	platform_driver_unregister(&hall_key_driver);
}


module_init(hall_key_init);
module_exit(hall_key_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("freecom.com");
MODULE_DESCRIPTION("hall key driver for freecom:questions contact jinq");
MODULE_ALIAS("platform:hall-key");

