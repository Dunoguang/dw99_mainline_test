/* SPDX-License-Identifier: GPL-2.0 */
/*
 * struct timeval was removed from upstream kernel (uapi only defines
 * it for !__KERNEL__). The vendor camera stack (ported from 4.4) still
 * uses it; provide a kernel-side definition with classic semantics.
 */
#ifndef _LINUX_SPRD_TIME_H
#define _LINUX_SPRD_TIME_H

#include <linux/types.h>

struct timeval {
	__kernel_old_time_t	tv_sec;		/* seconds */
	__kernel_suseconds_t	tv_usec;	/* microseconds */
};

#endif /* _LINUX_SPRD_TIME_H */
