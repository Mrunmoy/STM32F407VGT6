#pragma once

#include "app_threads.h"
#include "cfuture.h"
#include "logger.h"
#include "pal_storage.h"

/* Sets up the storage-service showcase (binds the given PalStorage to
 * FatFS's diskio glue, creates the Servicer's queue and the shared cfuture
 * promise pool) and registers its 5 threads (the Servicer, storage_
 * service.c, and 4 Requester demo tasks, client_tasks.c) into *registry -
 * it does NOT start them itself. app.c's appRun() is the only place that
 * ever starts a thread (via appThreadRegistryStartAll()); every other
 * module, this one included, only registers.
 *
 * syncOps: the libcfuture OSAL adapter for this build target - every
 * target's osal/src/cfuture_sync_ops.c exposes the same
 * cfuture_sync_ops_get() accessor name. This file must stay identical
 * across every target (that is the whole point of this showcase), so the
 * OS-specific accessor is injected by the caller rather than picked here.
 *
 * storage need not be init()'d before this call - FatFS's own f_mount() ->
 * disk_initialize() does that via the diskio glue (see fatfs_diskio.c).
 * storage and logger must outlive the demo (pass static/file-scope
 * instances, per this project's no-dynamic-allocation rule). Returns false
 * (and logs why) on setup or registration failure. */
bool storageDemoRegister(AppThreadRegistry *registry, PalStorage *storage, Logger *logger,
                          const cfuture_sync_ops_t *syncOps) __attribute__((warn_unused_result));
