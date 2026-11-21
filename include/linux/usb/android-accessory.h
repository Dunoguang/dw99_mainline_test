/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_USB_ANDROID_ACCESSORY_H
#define _LINUX_USB_ANDROID_ACCESSORY_H

struct usb_composite_dev;
struct usb_ctrlrequest;


int acc_ctrlrequest(struct usb_composite_dev *cdev, const struct usb_ctrlrequest *ctrl);
void acc_disconnect(void);

#endif
