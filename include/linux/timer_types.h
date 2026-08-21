/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMER_TYPES_H
#define _LINUX_TIMER_TYPES_H

#include <linux/lockdep_types.h>
#include <linux/types.h>

struct timer_list {
	/*
	 * All fields that change during normal runtime grouped to the
	 * same cacheline
	 */
	struct hlist_node	entry;
	unsigned long		expires;
	void			(*function)(struct timer_list *);
	u32			flags;

#ifdef CONFIG_TIMER_STATS
	int			start_pid;
	void			*start_site;
	char			start_comm[16];
#endif
#ifdef CONFIG_LOCKDEP
	struct lockdep_map	lockdep_map;
#endif
};

#endif /* _LINUX_TIMER_TYPES_H */
