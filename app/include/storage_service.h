#pragma once

#include <stdint.h>

#include "logger.h"
#include "osal.h"

/* The Servicer task (T_S): owns a single mounted FatFS volume and one
 * backing file (kBlockFileName in storage_service.c) used as a flat array
 * of kStorageBlockSize-byte blocks - storage_protocol.h's StorageRequest.
 * blockId directly addresses an offset into that file. Pulls StorageRequest
 * items off an OsalQueue, checks the request's libcfuture promise is still
 * active (cpromise_is_active) before doing any potentially slow f_read/
 * f_write, then resolves it (cpromise_set_value) or discards it
 * (cpromise_drop) per the Servicer/Requester choreography this whole demo
 * exists to exercise.
 *
 * Never touches a concrete peripheral directly, and never touches a
 * PalStorage directly either - fatfsDiskioBind() (fatfs_diskio.h) must be
 * called once by the composition root (storage_demo.c) before this task's
 * first f_mount(), binding the PalStorage that FatFS's diskio glue talks to;
 * from then on this task only calls plain FatFS (ff.h) API, which is
 * intentionally the only OS/peripheral-shaped thing it depends on besides
 * osal.h - keeping it identical whether the linked PalStorage/OSAL adapter
 * pair is host or FreeRTOS. */

enum
{
    /* Reserved blockId that makes this task insert an artificial
     * kStorageDemoSlowDelayMs delay between its cpromise_is_active() check
     * and the actual FatFS I/O + cpromise_set_value()/cpromise_drop() call.
     * Used only by client_tasks.c's timeout-race demo scenarios, to turn an
     * otherwise real-scheduling-dependent race into a deterministic one -
     * never sent by a real caller, and not a production feature. Kept well
     * inside signed-int range (unlike UINT32_MAX-ish sentinels) so this stays
     * a valid C enum constant. */
    kStorageDemoSlowBlockId = 0x0FFFFFFF,
    kStorageDemoSlowDelayMs = 200U,
};

typedef struct StorageServiceConfig
{
    OsalQueueHandle queue; /* holds StorageRequest items, see storage_protocol.h */
    Logger *logger;
} StorageServiceConfig;

/* Task entry point matching osal.h's OsalTaskEntryFn - runs forever pulling
 * requests off config->queue and never returns. context must point at a
 * StorageServiceConfig that outlives the task (a static instance, per this
 * project's no-dynamic-allocation rule - see storage_demo.c). */
void storageServiceTaskEntry(void *context);
