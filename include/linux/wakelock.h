/* SPDX-License-Identifier: GPL-2.0-only */
/* 4.4 Android wakelock API shim on top of 7.2 wakeup_source (DW99 port) */
#ifndef _LINUX_WAKELOCK_H
#define _LINUX_WAKELOCK_H

#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>

enum {
	WAKE_LOCK_SUSPEND, /* Prevent suspend */
	WAKE_LOCK_TYPE_COUNT
};

struct wake_lock {
	struct wakeup_source *ws;
};

static inline void wake_lock_init(struct wake_lock *lock, int type,
				  const char *name)
{
	lock->ws = wakeup_source_register(NULL, name);
}

static inline void wake_lock_destroy(struct wake_lock *lock)
{
	if (lock->ws)
		wakeup_source_unregister(lock->ws);
	lock->ws = NULL;
}

static inline void wake_lock(struct wake_lock *lock)
{
	if (lock->ws)
		__pm_stay_awake(lock->ws);
}

static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	if (lock->ws)
		__pm_wakeup_event(lock->ws, jiffies_to_msecs(timeout));
}

static inline void wake_unlock(struct wake_lock *lock)
{
	if (lock->ws)
		__pm_relax(lock->ws);
}

static inline int wake_lock_active(struct wake_lock *lock)
{
	return lock->ws ? lock->ws->active : 0;
}

/* 7.2 dropped wakeup_source_init/trash; provide inline shims for
 * drivers embedding struct wakeup_source (sprd sdiohal etc.)
 */
static inline void wakeup_source_init(struct wakeup_source *ws,
				      const char *name)
{
	memset(ws, 0, sizeof(*ws));
	ws->name = name;
	spin_lock_init(&ws->lock);
	INIT_LIST_HEAD(&ws->entry);
	timer_setup(&ws->timer, NULL, 0);
	ws->last_time = ktime_get();
}

static inline void wakeup_source_trash(struct wakeup_source *ws)
{
	__pm_relax(ws);
	timer_delete(&ws->timer);
}

#endif
