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
#ifdef CONFIG_X86
#include "sprd_pmic_wdt.h"
#else
#include <asm/system_misc.h>
#endif
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/mfd/sprd/pmic_glb_reg.h>
#include <linux/mfd/syscon/sprd-glb.h>
#include <linux/mfd/syscon.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
extern struct atomic_notifier_head panic_notifier_list;
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/spi/spi-sprd-adi.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <soc/sprd/sysdump.h>
#include "../base/regmap/internal.h"

#define WDG_INT_EN_BIT			BIT(0)
#define WDG_CNT_EN_BIT			BIT(1)
#define WDG_NEW_VER_EN			BIT(2)
#define WDG_INT_CLEAR_BIT		BIT(0)
#define WDG_RST_CLEAR_BIT		BIT(3)
#define WDG_LD_BUSY_BIT			BIT(4)
#define WDG_RST_EN_BIT			BIT(3)

#define ANA_RST_STATUS			ANA_REG_GLB_POR_RST_MONITOR
#define ANA_AGEN				ANA_REG_GLB_ARM_MODULE_EN
#define ANA_RTC_CLK_EN			ANA_REG_GLB_RTC_CLK_EN
#define AGEN_WDG_EN				BIT_ANA_WDG_EN
#define AGEN_RTC_WDG_EN			BIT_RTC_WDG_EN

#define WDG_CLK						32768
#define WDG_UNLOCK_KEY				0xE551
#define ANA_WDG_LOAD_TIMEOUT_NUM	(10000)

/*
* Definition of ANA_RST_STATUS(ANA_REG_GLB_POR_RST_MONITOR) register
* bit[0..7]: Reboot mode
* bit[9]: SYSDUMP enable/disable flag
*/

#define HWRST_STATUS_RECOVERY		(0x0020)
#define HWRST_STATUS_NORMAL		(0x0040)
#define HWRST_STATUS_ALARM		(0x0050)
#define HWRST_STATUS_SLEEP		(0x0060)
#define HWRST_STATUS_FASTBOOT		(0x0030)
#define HWRST_STATUS_SPECIAL		(0x0070)
#define HWRST_STATUS_PANIC		(0x0080)
#define HWRST_STATUS_CFTREBOOT		(0x0090)
#define HWRST_STATUS_AUTODLOADER	(0x00a0)
#define HWRST_STATUS_IQMODE		(0x00b0)
#define HWRST_STATUS_SPRDISK		(0x00c0)
#define HWRST_STATUS_SYSDUMP		(0x200)

#define HWRST_STATUS_SECURITY		(0x0002)

static struct spi_device *reboot_device;
static DEFINE_MUTEX(reboot_device_lock);
static unsigned long sprd_wdt_base;
unsigned int reboot_mode_flag;
DEFINE_SPINLOCK(reboot_flag_lock);
int sysdump_status; /* used by sysdump.c */

#define WDG_LOAD_LOW		(sprd_wdt_base + 0x00)
#define WDG_LOAD_HIGH		(sprd_wdt_base + 0x04)
#define WDG_CTRL			(sprd_wdt_base + 0x08)
#define WDG_INT_CLR			(sprd_wdt_base + 0x0c)
#define WDG_INT_RAW			(sprd_wdt_base + 0x10)
#define WDG_INT_MSK			(sprd_wdt_base + 0x14)
#define WDG_CNT_LOW			(sprd_wdt_base + 0x18)
#define WDG_CNT_HIGH		(sprd_wdt_base + 0x1c)
#define WDG_LOCK			(sprd_wdt_base + 0x20)

void set_sysdump_enable(int on);
#ifdef CONFIG_SPRD_DMC_DRV
#endif

/* Force reboot into recovery mode if the reboot is caused by a kernel panic */
static int sprd_panic_to_recovery;

static int sprd_panic_notifier_call(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	WRITE_ONCE(sprd_panic_to_recovery, 1);
	pr_emerg("sprd: kernel panic detected - will reboot into RECOVERY\n");
	return NOTIFY_DONE;
}

static struct notifier_block sprd_panic_nb = {
	.notifier_call = sprd_panic_notifier_call,
};

void sprd_set_reboot_mode(const char *cmd)
{
	pr_info("sprd_set_reboot_mode:cmd=%s\n", cmd);

	spin_lock(&reboot_flag_lock);
	if (READ_ONCE(sprd_panic_to_recovery)) {
		/* kernel panic: force reboot into recovery */
		reboot_mode_flag = HWRST_STATUS_RECOVERY;
	} else if (cmd && !(strncmp(cmd, "recovery", 8)))
		reboot_mode_flag |= HWRST_STATUS_RECOVERY;
	else if (cmd && !strncmp(cmd, "alarm", 5))
		reboot_mode_flag |= HWRST_STATUS_ALARM;
	else if (cmd && !strncmp(cmd, "fastsleep", 9))
		reboot_mode_flag |= HWRST_STATUS_SLEEP;
	else if (cmd && !strncmp(cmd, "bootloader", 10))
		reboot_mode_flag |= HWRST_STATUS_FASTBOOT;
	else if (cmd && !strncmp(cmd, "panic", 5))
		reboot_mode_flag |= HWRST_STATUS_PANIC;
	else if (cmd && !strncmp(cmd, "special", 7))
		reboot_mode_flag |= HWRST_STATUS_SPECIAL;
	else if (cmd && !strncmp(cmd, "cftreboot", 9))
		reboot_mode_flag |= HWRST_STATUS_CFTREBOOT;
	else if (cmd && !strncmp(cmd, "autodloader", 11))
		reboot_mode_flag |= HWRST_STATUS_AUTODLOADER;
	else if (cmd && !strncmp(cmd, "iqmode", 6))
		reboot_mode_flag |= HWRST_STATUS_IQMODE;
	else if (cmd && !strncmp(cmd, "sprdisk", 7))
		reboot_mode_flag |= HWRST_STATUS_SPRDISK;
	else if (cmd && !strncmp(cmd, "dumpenable", 10)) {
		pr_emerg("sprd_set_reboot_mode: enable sysdump!\n");
		reboot_mode_flag |= HWRST_STATUS_SYSDUMP;
	} else if (cmd && !strncmp(cmd, "dumpdisable", 11)) {
		pr_emerg("sprd_set_reboot_mode: disable sysdump!\n");
		reboot_mode_flag &= ~(HWRST_STATUS_SYSDUMP);
	} else if (cmd && !strncmp(cmd, "tospanic", 8)) {
		reboot_mode_flag |= HWRST_STATUS_SECURITY;
	} else
		reboot_mode_flag = HWRST_STATUS_NORMAL;

	spin_unlock(&reboot_flag_lock);
}

void set_sysdump_enable(int on)
{
	unsigned int val = 0;

	mutex_lock(&reboot_device_lock);
	if (!reboot_device) {
		mutex_unlock(&reboot_device_lock);
		return;
	}
	val = sprd_adi_read(reboot_device, ANA_RST_STATUS);
	pr_emerg("set_sysdump_enable: get rst mode  value is = %x\n", val);

	if (on) {
		pr_emerg("sprd_set_reboot_mode: enable sysdump!\n");
		val |= HWRST_STATUS_SYSDUMP;
		sprd_adi_raw_write(reboot_device, ANA_RST_STATUS, val);
	} else {
		pr_emerg("sprd_set_reboot_mode: disable sysdump!\n");
		val &= ~(HWRST_STATUS_SYSDUMP);
		sprd_adi_raw_write(reboot_device, ANA_RST_STATUS, val);
	}
	mutex_unlock(&reboot_device_lock);
}

/* The FDL/U-Boot reset load is a raw watchdog count, not milliseconds. */
#define SPRD_RESTART_WDG_COUNT 0x50

static u32 sprd_restart_mode(const char *cmd, bool panic)
{
	if (panic)
		return HWRST_STATUS_RECOVERY;
	if (!cmd)
		return HWRST_STATUS_NORMAL;
	if (!strncmp(cmd, "recovery", 8))
		return HWRST_STATUS_RECOVERY;
	if (!strncmp(cmd, "alarm", 5))
		return HWRST_STATUS_ALARM;
	if (!strncmp(cmd, "fastsleep", 9))
		return HWRST_STATUS_SLEEP;
	if (!strncmp(cmd, "bootloader", 10))
		return HWRST_STATUS_FASTBOOT;
	if (!strncmp(cmd, "panic", 5))
		return HWRST_STATUS_RECOVERY;
	if (!strncmp(cmd, "special", 7))
		return HWRST_STATUS_SPECIAL;
	if (!strncmp(cmd, "cftreboot", 9))
		return HWRST_STATUS_CFTREBOOT;
	if (!strncmp(cmd, "autodloader", 11))
		return HWRST_STATUS_AUTODLOADER;
	if (!strncmp(cmd, "iqmode", 6))
		return HWRST_STATUS_IQMODE;
	if (!strncmp(cmd, "sprdisk", 7))
		return HWRST_STATUS_SPRDISK;
	if (!strncmp(cmd, "tospanic", 8))
		return HWRST_STATUS_SECURITY;
	return HWRST_STATUS_NORMAL;
}

static bool sprd_restart_first;

static int sprd_restart_handle(const char *cmd)
{
	bool panic = READ_ONCE(sprd_panic_to_recovery);
	struct sprd_adi_restart_config config = {
		.wdg_base = sprd_wdt_base,
		.module_en_reg = ANA_AGEN,
		.rtc_clk_en_reg = ANA_RTC_CLK_EN,
		.reset_status_reg = ANA_RST_STATUS,
		.module_en_mask = AGEN_WDG_EN,
		.rtc_clk_en_mask = AGEN_RTC_WDG_EN,
		.mode_value = sprd_restart_mode(cmd, panic),
		/* Bootloader decodes the low byte; preserve the sysdump bit. */
		.mode_mask = 0xff,
		.count = SPRD_RESTART_WDG_COUNT,
	};
	int ret;

	if (!READ_ONCE(reboot_device))
		return -ENODEV;

	/* The BSL latches select normal boot; do not override other modes. */
	if (cmd && !panic &&
	    !strncmp(cmd, "dumpenable", 10)) {
		config.mode_mask = 0x2ff;
		config.mode_value = HWRST_STATUS_NORMAL | HWRST_STATUS_SYSDUMP;
	} else if (cmd && !panic &&
		   !strncmp(cmd, "dumpdisable", 11)) {
		config.mode_mask = 0x2ff;
		config.mode_value = HWRST_STATUS_NORMAL;
	}

	config.write_latches = true;
	config.latch_lo_reg = 0xf08;
	config.latch_hi_reg = 0xf0c;
	if ((config.mode_value & 0xff) == HWRST_STATUS_NORMAL) {
		config.latch_lo_value = 0x55;
		config.latch_hi_value = 0xffaa;
	} else {
		/* Clear any old normal-boot latch before setting another mode. */
		config.latch_lo_value = 0xa596;
		config.latch_hi_value = 0x5a69;
	}

	ret = sprd_adi_restart_atomic(reboot_device, &config);
	if (ret)
		return ret;

	/* 0x50 ticks at 32768 Hz is about 2.5 ms; allow time for reset. */
	mdelay(100);
	return -ETIMEDOUT;
}

/* Preserve the pre-existing programming sequence for non-opt-in PMICs. */
static void sprd_restart_legacy(enum reboot_mode mode, const char *cmd)
{
	u32 cnt;

	sprd_set_reboot_mode(cmd);
	sprd_adi_raw_write(reboot_device, ANA_RST_STATUS, reboot_mode_flag);
	cnt = ((mode == REBOOT_WDT ? 400 : 50) * 1000) / WDG_CLK;
	sprd_adi_set(reboot_device, ANA_AGEN, AGEN_WDG_EN);
	sprd_adi_set(reboot_device, ANA_RTC_CLK_EN, AGEN_RTC_WDG_EN);
	sprd_adi_raw_write(reboot_device, WDG_LOCK, WDG_UNLOCK_KEY);
	sprd_adi_set(reboot_device, WDG_CTRL, WDG_NEW_VER_EN);
	sprd_adi_raw_write(reboot_device, WDG_LOAD_HIGH, (cnt >> 16) & 0xffff);
	sprd_adi_raw_write(reboot_device, WDG_LOAD_LOW, cnt & 0xffff);
	sprd_adi_set(reboot_device, WDG_CTRL, WDG_CNT_EN_BIT | WDG_RST_EN_BIT);
	sprd_adi_raw_write(reboot_device, WDG_LOCK, (u16)~WDG_UNLOCK_KEY);
#ifdef CONFIG_SPRD_DMC_DRV
	sprd_ddr_force_light_sleep();
#endif
	mdelay(1000);
}

#ifdef CONFIG_X86
static void sprd_restart_x86(enum reboot_mode mode, const char *cmd)
{
	if (sprd_restart_first)
		sprd_restart_handle(cmd);
	else
		sprd_restart_legacy(mode, cmd);
}

static void sprd_restart_prepare(const char *cmd)
{
	sprd_set_reboot_mode(cmd);
}
#endif

static int sprd_restart_notify(struct notifier_block *nb, unsigned long mode,
			       void *cmd)
{
	if (sprd_restart_first)
		sprd_restart_handle(cmd);
	else
		sprd_restart_legacy(mode, cmd);
	/* Continue to the next restart handler if the PMIC did not reset us. */
	return NOTIFY_DONE;
}

static struct notifier_block sprd_restart_nb = {
	.notifier_call = sprd_restart_notify,
};

static struct of_device_id sprd_reboot_of_match[] = {
	{.compatible = "sprd,sc2723t-wdt",},
	{.compatible = "sprd,sc2731-wdt",},
	{.compatible = "sprd,sc2721-wdt",},
	{.compatible = "sprd,sc2720-wdt",},
	{}
};

static int sprd_reboot_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 value = 0;
	struct device_node *node = pdev->dev.of_node;
	struct regmap *sprd_reboot;

	mutex_lock(&reboot_device_lock);
	if (reboot_device) {
		ret = -EBUSY;
		goto out_unlock;
	}

	sprd_reboot = dev_get_regmap(pdev->dev.parent, NULL);
	if (!sprd_reboot) {
		dev_err(&pdev->dev, "sprd reboot probe failed!\n");
		ret = -EINVAL;
		goto out_unlock;
	}
	if (!sprd_reboot->dev) {
		ret = -ENODEV;
		goto out_unlock;
	}

	reboot_device = to_spi_device(sprd_reboot->dev);
	if (!reboot_device) {
		dev_err(&pdev->dev, "sprd reboot device probe failed!\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = of_property_read_u32(node, "reg", &value);
	if (ret) {
		dev_err(&pdev->dev, "sprd reboot probe failed!\n");
		reboot_device = NULL;
		ret = -EINVAL;
		goto out_unlock;
	}

	sprd_wdt_base = (unsigned long)value;
	reboot_mode_flag = 0;
	sprd_restart_first = of_device_is_compatible(node, "sprd,sc2721-wdt") &&
		of_property_read_bool(node, "sprd,pmic-restart-first");
	sprd_restart_nb.priority = sprd_restart_first ? 130 : 0;
	ret = atomic_notifier_chain_register(&panic_notifier_list, &sprd_panic_nb);
	if (ret) {
		reboot_device = NULL;
		goto out_unlock;
	}
	ret = register_restart_handler(&sprd_restart_nb);
	if (ret) {
		atomic_notifier_chain_unregister(&panic_notifier_list, &sprd_panic_nb);
		reboot_device = NULL;
		goto out_unlock;
	}
#ifdef CONFIG_X86
	x86_pm_restart_prepare = sprd_restart_prepare;
	x86_pm_restart = sprd_restart_x86;
#endif
	dev_info(&pdev->dev, "PMIC Watchdog probe OK!\n");
	mutex_unlock(&reboot_device_lock);

#if defined(CONFIG_SPRD_DEBUG) && defined(CONFIG_SPRD_SYSDUMP)
	pr_emerg("userdebug enable sysdump in default !!!\n");
	sprd_set_reboot_mode("dumpenable");
	set_sysdump_enable(1);
	sysdump_status = 1;
#endif
	return 0;
out_unlock:
	mutex_unlock(&reboot_device_lock);
	return ret;
}

static void sprd_reboot_remove(struct platform_device *pdev)
{
	mutex_lock(&reboot_device_lock);
	unregister_restart_handler(&sprd_restart_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list, &sprd_panic_nb);
#ifdef CONFIG_X86
	if (x86_pm_restart_prepare == sprd_restart_prepare)
		x86_pm_restart_prepare = NULL;
	if (x86_pm_restart == sprd_restart_x86)
		x86_pm_restart = NULL;
#endif
	WRITE_ONCE(reboot_device, NULL);
	sprd_wdt_base = 0;
	sprd_restart_first = false;
	WRITE_ONCE(sprd_panic_to_recovery, 0);
	mutex_unlock(&reboot_device_lock);
}

static struct platform_driver sprd_reboot_driver = {
	.probe = sprd_reboot_probe,
	.remove = sprd_reboot_remove,
	.driver = {
		.name = "sprd-pmic-wdt",
		.of_match_table = sprd_reboot_of_match,
	},
};

static int __init sprd_reboot_init(void)
{
	return platform_driver_register(&sprd_reboot_driver);
}

/* Register before the PMIC creates its child devices at subsys_initcall_sync. */
subsys_initcall(sprd_reboot_init);
MODULE_DESCRIPTION("SPREADTRUM PMIC Watchdog Driver");
MODULE_LICENSE("GPL v2");
