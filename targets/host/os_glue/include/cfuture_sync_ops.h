#pragma once

#include "cfuture.h"

/* Concrete cfuture_sync_ops_t provider for this target - delegates to
 * libcfuture's own POSIX adapter (external/cfuture's
 * adapters/cfuture_posix.h). Every target exposes this same
 * cfuture_sync_ops_get() accessor name from its own os_glue/, so
 * composition roots never need a target-specific function name here - only
 * each target's own composition root (main.c here) ever calls it, all
 * identically. */
const cfuture_sync_ops_t *cfuture_sync_ops_get(void);
