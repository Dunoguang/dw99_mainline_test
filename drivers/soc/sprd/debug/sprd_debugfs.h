#ifndef _SPRD_DEBUGFS_H_
#define _SPRD_DEBUGFS_H_
enum sprd_debug_type {
	SCHED,
	MEM,
	COMM,
	TIMER,
	IRQ,
	IO,
	CPU,
	MISC,
	TYPE_COUNT
};

struct dentry *sprd_debugfs_entry(enum sprd_debug_type type);

struct task_struct;

void sprd_update_cpu_usage(struct task_struct *prev, struct task_struct *next);

#endif
