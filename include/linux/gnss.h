/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GNSS receiver support
 *
 * Copyright (C) 2018 Johan Hovold <johan@kernel.org>
 */

#ifndef _LINUX_GNSS_H
#define _LINUX_GNSS_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <linux/wait.h>

struct gnss_device;

enum gnss_type {
	GNSS_TYPE_NMEA = 0,
	GNSS_TYPE_SIRF,
	GNSS_TYPE_UBX,
	GNSS_TYPE_MTK,

	GNSS_TYPE_COUNT
};

struct gnss_operations {
	int (*open)(struct gnss_device *gdev);
	void (*close)(struct gnss_device *gdev);
	int (*write_raw)(struct gnss_device *gdev, const unsigned char *buf,
				size_t count);
};

struct gnss_device {
	struct device dev;
	struct cdev cdev;
	int id;

	enum gnss_type type;
	unsigned long flags;

	struct rw_semaphore rwsem;
	const struct gnss_operations *ops;
	unsigned int count;
	unsigned int disconnected:1;

	struct mutex read_mutex;
	struct kfifo read_fifo;
	wait_queue_head_t read_queue;

	struct mutex write_mutex;
	char *write_buf;
};

struct gnss_device *gnss_allocate_device(struct device *parent);
void gnss_put_device(struct gnss_device *gdev);
int gnss_register_device(struct gnss_device *gdev);
void gnss_deregister_device(struct gnss_device *gdev);

int gnss_insert_raw(struct gnss_device *gdev, const unsigned char *buf,
			size_t count);

static inline void gnss_set_drvdata(struct gnss_device *gdev, void *data)
{
	dev_set_drvdata(&gdev->dev, data);
}

static inline void *gnss_get_drvdata(struct gnss_device *gdev)
{
	return dev_get_drvdata(&gdev->dev);
}

#endif /* _LINUX_GNSS_H */

/* Spreadtrum DW99 additions (ported from 4.4) */
#define __GNSS_H__
#define FALSE								(0)
#define TRUE								(1)
#define GNSS_CACHE_FLAG_ADDR		(0x0014F000)
#define GNSS_CACHE_FLAG_VALUE		(0xCDCDDCDC)
#define GNSS_CACHE_END_VALUE		(0xEFEFFEFE)
#define GNSS_STATUS_OFFSET		   (0x0014F004)
#define GNSS_STATUS_SIZE		   (4)
#define GNSS_REC_AON_CHIPID_OFFSET (0x00150000) /* sharkle or pike2 */
#define GNSS_REC_AON_CHIPID_SIZE (8)
#define GNSS_EFUSE_DATA_OFFSET (0x00150008)
#define GNSS_EFUSE_DATA_SIZE  12
#define AON_CLK_CORE   0x402d0200
#define CGM_WCN_SHARKLE_CFG    0xd4
#define CGM_WCN_PIKE2_CFG    0xd8
