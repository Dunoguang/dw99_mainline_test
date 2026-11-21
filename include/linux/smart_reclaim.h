/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SMART_RECLAIM_H
#define _LINUX_SMART_RECLAIM_H

struct mm_struct;


#ifdef CONFIG_SMART_RECLAIM
void smart_soft_shrink(struct mm_struct *mm);
#else
static inline void smart_soft_shrink(struct mm_struct *mm) { }
#endif

#endif
