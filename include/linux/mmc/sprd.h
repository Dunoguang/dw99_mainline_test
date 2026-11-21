/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMC_SPRD_H
#define _LINUX_MMC_SPRD_H

struct mmc_host;


int sprd_sdhc_set_nonremovable(struct mmc_host *host);

#endif
