/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOC_SPRD_BOOTMODE_H
#define _SOC_SPRD_BOOTMODE_H

#include <linux/types.h>

bool in_calibration(void);
bool in_charger(void);
bool in_autotest(void);

#endif
