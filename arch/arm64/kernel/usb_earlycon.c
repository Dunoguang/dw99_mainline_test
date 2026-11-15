// SPDX-License-Identifier: GPL-2.0-only
/*
 * Early console writing directly to the MUSB bulk IN endpoint that the
 * bootloader configured.  Only needs the register mapping set up by the
 * pi code; no USB stack, no interrupts, no clock handling.
 */

#include <linux/console.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <asm/memory.h>

#define MUSB_IDXREG	0x0e
#define MUSB_TXCSR	0x12	/* indexed window after index select */
#define MUSB_FIFO	0x34	/* fifox[EP] */
#define MUSB_EP	5
#define MUSB_PKTSZ	64

static void musb_ec_write(struct console *con, const char *s, unsigned int n)
{
	void __iomem *base = (void __iomem *)MUSB_EARLYCON_VA;

	writeb(MUSB_EP, base + MUSB_IDXREG);
	while (n) {
		unsigned int chunk = (n < MUSB_PKTSZ) ? n : MUSB_PKTSZ;
		u16 csr;
		unsigned int timeout = 1000000;

		/* Wait for the host to drain the previous packet. */
		do {
			csr = readw(base + MUSB_TXCSR);
			if (!(csr & 0x0001))
				break;
		} while (--timeout);
		if (!timeout)
			return;		/* host not reading, drop the rest */

		while (chunk--)
			writeb(*s++, base + MUSB_FIFO);
		csr = readw(base + MUSB_TXCSR);
		writew(csr | 0x0001, base + MUSB_TXCSR);
		n -= (n < MUSB_PKTSZ) ? n : MUSB_PKTSZ;
	}
}

static struct console musb_early_console = {
	.name	= "musbec",
	.write	= musb_ec_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT | CON_ENABLED,
	.index	= 0,
};

void __init musb_earlycon_init(void)
{
	register_console(&musb_early_console);
}
