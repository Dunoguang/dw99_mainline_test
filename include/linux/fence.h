/* SPDX-License-Identifier: GPL-2.0-only */
/* 4.4 Android fence API shim on top of 7.2 dma_fence (DW99 port) */
#ifndef _LINUX_FENCE_H
#define _LINUX_FENCE_H

#include <linux/dma-fence.h>

#define fence		dma_fence
#define fence_cb	dma_fence_cb
#define fence_ops	dma_fence_ops

#define fence_init		dma_fence_init
#define fence_signal		dma_fence_signal
#define fence_signal_locked	dma_fence_signal_locked
#define fence_put		dma_fence_put
#define fence_get		dma_fence_get
#define fence_get_rcu		dma_fence_get_rcu
#define fence_add_callback	dma_fence_add_callback
#define fence_remove_callback	dma_fence_remove_callback
#define fence_is_signaled	dma_fence_is_signaled
#define fence_is_signaled_locked dma_fence_is_signaled_locked
#define fence_enable_sw_signaling dma_fence_enable_sw_signaling
#define fence_context_alloc	dma_fence_context_alloc
#define fence_default_wait	dma_fence_default_wait
#define fence_get_status	dma_fence_get_status
#define fence_set_error		dma_fence_set_error
#define fence_get_timestamp	dma_fence_timestamp

#endif
