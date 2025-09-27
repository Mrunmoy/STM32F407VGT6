#pragma once

#include "cfuture.h"

/* FreeRTOS-backed implementation of libcfuture's cfuture_sync_ops_t contract
 * (cfuture.h) - the OSAL adapter cfuture_pool_init() needs to support
 * cfuture_wait_for()/cpromise_set_value() blocking/waking across tasks.
 * Backed by a static pool of FreeRTOS binary semaphores instead of pthread
 * mutex/condvar pairs, since this project links exactly one OSAL adapter
 * per build target and forbids dynamic allocation.
 *
 * Every target exposes this same cfuture_sync_ops_get() accessor name from
 * its own os_glue/ - composition roots never need a target-specific
 * function name here, only main.c/board_start_app.c ever calls it, all
 * identically. */
const cfuture_sync_ops_t *cfuture_sync_ops_get(void);
