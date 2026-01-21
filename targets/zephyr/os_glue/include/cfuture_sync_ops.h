#pragma once

#include "cfuture.h"

/* Zephyr-backed implementation of libcfuture's cfuture_sync_ops_t contract
 * (cfuture.h), using one shared struct k_event with each pool slot
 * permanently owning one bit (CFUTURE_MAX_CAPACITY is 32, exactly the
 * width of one k_event's uint32_t event-set, same reasoning the ThreadX
 * target's cfuture_sync_ops.c already documents for its
 * TX_EVENT_FLAGS_GROUP). event_create()/event_destroy() are a simple bump
 * allocator - this app's cfuture pools are created once at boot and never
 * torn down, so slots are never actually freed.
 *
 * Every target exposes this same cfuture_sync_ops_get() accessor name from
 * its own os_glue/ - composition roots never need a target-specific
 * function name here, only each target's own composition root (src/main.c
 * here) ever calls it, all identically. */
const cfuture_sync_ops_t *cfuture_sync_ops_get(void);
