/* SPDX-License-Identifier: GPL-2.0 */
/* 32-bit compat ABI for the SPRD VSP driver; kept out of the uapi header. */
#ifndef _SPRD_VSP_COMPAT_H
#define _SPRD_VSP_COMPAT_H

#include <linux/compat.h>

#ifdef CONFIG_COMPAT
#define COMPAT_VSP_GET_IOVA    _IOWR(SPRD_VSP_IOCTL_MAGIC, 11, struct compat_vsp_iommu_map_data)
#define COMPAT_VSP_FREE_IOVA   _IOW(SPRD_VSP_IOCTL_MAGIC, 12, struct compat_vsp_iommu_map_data)

struct compat_vsp_iommu_map_data {
	compat_int_t fd;
	compat_size_t size;
	compat_ulong_t iova_addr;
};
#endif
#endif
