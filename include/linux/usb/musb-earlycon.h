/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_USB_MUSB_EARLYCON_H
#define __LINUX_USB_MUSB_EARLYCON_H

#include <linux/types.h>

#ifdef CONFIG_USB_MUSB_EARLYCON
void musb_earlycon_init(void);
void musb_earlycon_release_phy(phys_addr_t phy_base);
#else
static inline void musb_earlycon_init(void) {}
static inline void musb_earlycon_release_phy(phys_addr_t phy_base) {}
#endif

#endif
