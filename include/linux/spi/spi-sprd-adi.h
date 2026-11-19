/* * Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SPRD_ADI_H__
#define __SPRD_ADI_H__

#include <linux/spi/spi.h>
#include <linux/printk.h>

/*
 * All register addresses are byte offsets in the ADI slave window, not AP
 * physical/virtual addresses.  The supplied device must stay alive throughout
 * the call.  The caller must not modify this structure while it is copied.
 *
 * mode_mask selects bits changed in reset_status_reg; unmasked bits are
 * preserved.  For SC2721 use 0xff for an ordinary reboot to preserve the
 * sysdump bit (bit 9); include bit 9 only for an explicit sysdump change.
 * The optional RTC latch writes are full 16-bit values: hi first, then lo,
 * followed by a 300 us delay, before writing the reboot mode.
 */
struct sprd_adi_restart_config {
	u32 wdg_base;
	u32 module_en_reg;
	u32 rtc_clk_en_reg;
	u32 reset_status_reg;
	u32 module_en_mask;
	u32 rtc_clk_en_mask;
	u32 mode_value;
	u32 mode_mask;
	u32 count;
	bool write_latches;
	u32 latch_hi_reg;
	u32 latch_hi_value;
	u32 latch_lo_reg;
	u32 latch_lo_value;
};

#if IS_REACHABLE(CONFIG_SPI_SPRD_ADI_SHARKLE)
int sprd_adi_restart_atomic(struct spi_device *adi_dev,
			    const struct sprd_adi_restart_config *config);
#else
static inline int
sprd_adi_restart_atomic(struct spi_device *adi_dev,
			const struct sprd_adi_restart_config *config)
{
	return -ENODEV;
}
#endif

#define TRANS_BUF_SIZE			(10)

/* ADI write type */
enum ADI_WRITE_MODE {
	SET_MODE = 0,
	FAST_SET_MODE,
};

extern int sprd_adi_bufs(struct spi_device *adi_dev, struct spi_transfer *t);

/* read adi register */
static inline int sprd_adi_read(struct spi_device *adi_dev, unsigned long reg)
{
	unsigned long rx_buf[TRANS_BUF_SIZE];
	struct spi_transfer t;
	int ret, val;

	if (!adi_dev) {
		pr_err("ADI read device is NULL!\n");
		return -ENODEV;
	}

	rx_buf[0] = reg;
	t.rx_buf = rx_buf;
	t.tx_buf = NULL;
	t.len = 1;

	ret = sprd_adi_bufs(adi_dev, &t);
	if (ret < 0) {
		pr_err("ADI read: spi_sync --> %d\n", ret);
		return ret;
	}

	val = (int)(*(unsigned long *)(rx_buf));
	return val;
}

/* write adi register */
static inline int sprd_adi_write(struct spi_device *adi_dev, unsigned long reg,
				unsigned long or_val, unsigned long clear_msk)
{
	unsigned long tx_buf[TRANS_BUF_SIZE];
	struct spi_transfer t;
	int ret;

	if (!adi_dev) {
		pr_err("ADI write device is NULL!\n");
		return -ENODEV;
	}

	tx_buf[0] = reg;
	tx_buf[1] = SET_MODE;
	tx_buf[2] = or_val;
	tx_buf[3] = clear_msk;
	t.tx_buf = tx_buf;
	t.rx_buf = NULL;
	t.len = 4;

	ret = sprd_adi_bufs(adi_dev, &t);
	if (ret < 0)
		pr_err("ADI write: spi_sync --> %d\n", ret);

	return ret;
}

/* fast write adi register */
static inline int sprd_adi_write_fast(struct spi_device *adi_dev,
				unsigned long reg, unsigned long val, int sync)
{
	unsigned long tx_buf[TRANS_BUF_SIZE];
	struct spi_transfer t;
	int ret;

	if (!adi_dev) {
		pr_err("ADI fast write device is NULL!\n");
		return -ENODEV;
	}

	tx_buf[0] = reg;
	tx_buf[1] = FAST_SET_MODE;
	tx_buf[2] = val;
	t.tx_buf = tx_buf;
	t.rx_buf = NULL;
	t.len = 3;

	ret = sprd_adi_bufs(adi_dev, &t);
	if (ret < 0)
		pr_err("ADI fast write:spi_async --> %d\n", ret);

	return ret;
}

static inline int sprd_adi_raw_write(struct spi_device *adi_dev,
				unsigned long reg, unsigned long val)
{
	return sprd_adi_write_fast(adi_dev, reg, val, 1);
}

static inline int sprd_adi_set(struct spi_device *adi_dev,
				unsigned long reg, unsigned long bits)
{
	return sprd_adi_write(adi_dev, reg, bits, 0);
}

static inline int sprd_adi_clr(struct spi_device *adi_dev,
				unsigned long reg, unsigned long bits)
{
	return sprd_adi_write(adi_dev, reg, 0, bits);
}

#endif
