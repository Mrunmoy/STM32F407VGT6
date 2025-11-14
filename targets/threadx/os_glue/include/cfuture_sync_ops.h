#pragma once

#include "cfuture.h"

/* ThreadX-backed implementation of libcfuture's cfuture_sync_ops_t contract
 * (cfuture.h), using one shared TX_EVENT_FLAGS_GROUP with each pool slot
 * permanently owning one bit (CFUTURE_MAX_CAPACITY is 32, exactly the width
 * of one ThreadX event flags group on this target) - unlike FreeRTOS's
 * dynamic per-slot semaphore-pool adapter, event_create()/event_destroy()
 * here are a simple bump allocator: this app's cfuture pools are created
 * once at boot and never torn down, so slots are never actually freed.
 *
 * Every target exposes this same cfuture_sync_ops_get() accessor name from
 * its own os_glue/ - composition roots never need a target-specific
 * function name here, only main.c/board_start_app.c ever calls it, all
 * identically. */
const cfuture_sync_ops_t *cfuture_sync_ops_get(void);
