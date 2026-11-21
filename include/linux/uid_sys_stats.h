/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UID_SYS_STATS_H
#define _LINUX_UID_SYS_STATS_H

struct task_struct;


void uid_stat_task_exit(struct task_struct *task);

#endif
