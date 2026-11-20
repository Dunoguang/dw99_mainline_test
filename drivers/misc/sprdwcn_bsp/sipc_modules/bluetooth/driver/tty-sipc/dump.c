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

#include <linux/mutex.h>
#include <linux/timekeeping.h>
#include "dump.h"

static bt_host_data_dump dump_data;
static bt_host_data_dump * const data_dump = &dump_data;
static DEFINE_MUTEX(dump_lock);

void bt_host_data_reset(void)
{
	mutex_lock(&dump_lock);
	memset(data_dump, 0, sizeof(*data_dump));
	mutex_unlock(&dump_lock);
}

static void data_left_shift(unsigned char data_inout)
{
    unsigned char loop_count_i = 0;
    switch (data_inout) {
        case BT_DATA_OUT:
            for(; loop_count_i < BT_MAX_DUMP_FRAME_LEN - 1; loop_count_i++) {
                memcpy(data_dump->txtime_t + loop_count_i, data_dump->txtime_t + loop_count_i + 1, sizeof(bt_host_time));
                memcpy(data_dump->tx[loop_count_i], data_dump->tx[loop_count_i + 1], BT_MAX_DUMP_DATA_LEN);
            }
        break;
        case BT_DATA_IN:
            for(; loop_count_i < BT_MAX_DUMP_FRAME_LEN - 1; loop_count_i++) {
                memcpy(data_dump->rxtime_t + loop_count_i, data_dump->rxtime_t + loop_count_i + 1, sizeof(bt_host_time));
                memcpy(data_dump->rx[loop_count_i], data_dump->rx[loop_count_i + 1], BT_MAX_DUMP_DATA_LEN);
            }
        break;
        default:
            break;
    }
}

static void get_time(unsigned char direction)
{
	bt_host_time *last;

	if (direction == BT_DATA_OUT)
		last = &data_dump->txtime_t[BT_MAX_DUMP_FRAME_LEN - 1];
	else
		last = &data_dump->rxtime_t[BT_MAX_DUMP_FRAME_LEN - 1];

	ktime_get_real_ts64(&last->tv);
	rtc_time64_to_tm(last->tv.tv_sec, &last->rtc_t);
}

void bt_host_data_save(const unsigned char *buf, int count, unsigned char data_inout) {
    if (count < 1)
        return;
    if ((buf[0] == HCI_COMMAND) ||
        (count >= 2 && (buf[0] == HCI_EVENT) && (buf[1] == HCI_COMMAND_STATUS)) ||
        (count >= 2 && (buf[0] == HCI_EVENT) && (buf[1] == HCI_COMMAND_COMPELET))) {
        mutex_lock(&dump_lock);
        pr_debug("bt_host_data_save: data %d \n", data_inout);
        data_left_shift(data_inout);
        get_time(data_inout);
    } else {
        return;
    }
    if (count <= BT_MAX_DUMP_DATA_LEN) {
        switch (data_inout) {
            case BT_DATA_OUT:
                memset(data_dump->tx[BT_MAX_DUMP_FRAME_LEN - 1], 0, BT_MAX_DUMP_DATA_LEN);
                memcpy(data_dump->tx[BT_MAX_DUMP_FRAME_LEN - 1], buf, count);
                break;
            case BT_DATA_IN:
                memset(data_dump->rx[BT_MAX_DUMP_FRAME_LEN - 1], 0, BT_MAX_DUMP_DATA_LEN);
                memcpy(data_dump->rx[BT_MAX_DUMP_FRAME_LEN - 1], buf, count);
                break;
            default:
                break;
        }
    } else {
            switch (data_inout) {
            case BT_DATA_OUT:
                memcpy(data_dump->tx[BT_MAX_DUMP_FRAME_LEN - 1], buf, BT_MAX_DUMP_DATA_LEN);
                break;
            case BT_DATA_IN:
                memcpy(data_dump->rx[BT_MAX_DUMP_FRAME_LEN - 1], buf, BT_MAX_DUMP_DATA_LEN);
                break;
            default:
                break;
        }
    }
    mutex_unlock(&dump_lock);
}

void bt_host_data_printf(void) {
    unsigned char loop_count_i = 0, loop_count_j = 0;

    mutex_lock(&dump_lock);
    for(; loop_count_j < BT_MAX_DUMP_FRAME_LEN; loop_count_j++) {
        printk("bt_host_data_printf txdata[%d]: ", loop_count_j + 1);
        printk("%d-%d-%d %d:%d:%d.%06ld ", 1900 + data_dump->txtime_t[loop_count_j].rtc_t.tm_year,
        1 + data_dump->txtime_t[loop_count_j].rtc_t.tm_mon, data_dump->txtime_t[loop_count_j].rtc_t.tm_mday,
        data_dump->txtime_t[loop_count_j].rtc_t.tm_hour, data_dump->txtime_t[loop_count_j].rtc_t.tm_min,
        data_dump->txtime_t[loop_count_j].rtc_t.tm_sec, data_dump->txtime_t[loop_count_j].tv.tv_nsec / NSEC_PER_USEC);
        while(loop_count_i < BT_MAX_DUMP_DATA_LEN) {
            printk("%02X ", data_dump->tx[loop_count_j][loop_count_i++]);
        }
        printk("\n");
        loop_count_i = 0;
    }
    loop_count_j = 0;
    for(; loop_count_j < BT_MAX_DUMP_FRAME_LEN; loop_count_j++) {
        printk("bt_host_data_printf rxdata[%d]: ", loop_count_j + 1);
        printk("%d-%d-%d %d:%d:%d.%06ld ", 1900 + data_dump->rxtime_t[loop_count_j].rtc_t.tm_year,
        1 + data_dump->rxtime_t[loop_count_j].rtc_t.tm_mon, data_dump->rxtime_t[loop_count_j].rtc_t.tm_mday,
        data_dump->rxtime_t[loop_count_j].rtc_t.tm_hour, data_dump->rxtime_t[loop_count_j].rtc_t.tm_min,
        data_dump->rxtime_t[loop_count_j].rtc_t.tm_sec, data_dump->rxtime_t[loop_count_j].tv.tv_nsec / NSEC_PER_USEC);
        while(loop_count_i < BT_MAX_DUMP_DATA_LEN) {
            printk("%02X ", data_dump->rx[loop_count_j][loop_count_i++]);
        }
        printk("\n");
        loop_count_i = 0;
    }
    mutex_unlock(&dump_lock);
}
