/* SPDX-License-Identifier: GPL-2.0 */
/* 32-bit compat ABI for the SPRD ISP driver; kept out of the uapi header. */
#ifndef _ISP_DRV_KERNEL_COMPAT_H
#define _ISP_DRV_KERNEL_COMPAT_H

#include <linux/compat.h>
#include <uapi/video/isp_drv_kernel.h>

#ifdef CONFIG_COMPAT
struct compat_isp_irq_param {
	compat_uint_t isp_irq_val;
	compat_uint_t dcam_irq_val;
	compat_uint_t irq_val;
	compat_int_t ret_val;
};

struct compat_isp_reg_bits {
	compat_ulong_t reg_addr;
	compat_ulong_t reg_value;
};

struct compat_isp_reg_param {
	compat_ulong_t reg_param;
	uint32_t counts;
};

#define COMPAT_ISP_IO_READ         \
			_IOR(ISP_IO_MAGIC, 1, struct compat_isp_reg_param)
#define COMPAT_ISP_IO_WRITE        \
			_IOW(ISP_IO_MAGIC, 2, struct compat_isp_reg_param)
#define COMPAT_ISP_IO_LNC_PARAM    \
			_IOW(ISP_IO_MAGIC, 8, struct compat_isp_reg_param)
#define COMPAT_ISP_IO_LNC          \
			_IOW(ISP_IO_MAGIC, 9, struct compat_isp_reg_param)
#define COMPAT_ISP_IO_ALLOC        \
			_IOW(ISP_IO_MAGIC, 10, struct compat_isp_reg_param)

#endif
#endif
