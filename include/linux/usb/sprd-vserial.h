/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_USB_SPRD_VSERIAL_H
#define _LINUX_USB_SPRD_VSERIAL_H

#include <linux/types.h>


void kernel_vser_register_callback(void *function);
ssize_t vser_iq_write(char *buf, size_t count);
int in_iqmode(void);

#endif
