#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cfuture.h"

/* Request/response shapes storage_service.c (the Servicer/T_S task) and
 * client_tasks.c (Requester/T_A-style tasks) pass through the OsalQueue
 * (osal.h) that fronts the storage service, plus the libcfuture promise/
 * future pair that carries the response back out-of-band. See the libcfuture
 * API notes gathered for this task for the full Servicer/Requester
 * choreography (cpromise_is_active before doing work, cpromise_set_value /
 * cpromise_drop to finish, cfuture_wait_for on the requester side) - this
 * header only defines the data shapes, not the control flow. */

enum
{
    /* One PalStorage sector's worth of payload (pal_storage.h fixes sector
     * size at 512 across every implementation this project ships). Both
     * StorageRequest.writeData and StorageResult.data are sized to this so
     * a single cfuture_pool_t (fixed payload_size = sizeof(StorageResult))
     * covers every StorageCommandKind. */
    kStorageBlockSize = 512U,
};

typedef enum StorageCommandKind
{
    kStorageCommandRead = 0,        /* read one block; result carries the data */
    kStorageCommandWrite = 1,       /* write request.writeData; result carries no data */
    kStorageCommandGetStatus = 2,   /* query mount/ready state; result.ready is the answer */
} StorageCommandKind;

/* Fallback definitions for platforms/headers without full POSIX errno macros.
 * Scoped under STORAGE_FALLBACK_ prefix to avoid polluting the global reserved errno namespace. */
#define STORAGE_FALLBACK_ENODEV 19
#define STORAGE_FALLBACK_EIO 5
#define STORAGE_FALLBACK_EINVAL 22
#define STORAGE_FALLBACK_ECANCELED 125

/* App-defined status codes:
 * - kStorageErrorOk (0) is passed to cpromise_set_value() on success.
 * - Negative error codes are passed to cpromise_drop() on failure, and
 *   retrieved via cfuture_wait_for's out_status.
 * Follows standard UNIX negative errno conventions (-ENODEV, -EIO, -EINVAL, -ECANCELED). */
typedef enum StorageErrorCode
{
    kStorageErrorOk = 0,
#if defined(ENODEV)
    kStorageErrorNotReady = -ENODEV,       /* PalStorage not mounted/ready */
#else
    kStorageErrorNotReady = -STORAGE_FALLBACK_ENODEV,
#endif
#if defined(EIO)
    kStorageErrorIoFailure = -EIO,        /* PalStorage read/write/sync returned an error */
#else
    kStorageErrorIoFailure = -STORAGE_FALLBACK_EIO,
#endif
#if defined(EINVAL)
    kStorageErrorInvalidBlock = -EINVAL,    /* blockId/length out of range */
#else
    kStorageErrorInvalidBlock = -STORAGE_FALLBACK_EINVAL,
#endif
#if defined(ECANCELED)
    kStorageErrorCancelled = -ECANCELED,      /* cpromise_is_active() was false; work was skipped */
#else
    kStorageErrorCancelled = -STORAGE_FALLBACK_ECANCELED,
#endif
} StorageErrorCode;

/* Result payload copied into the caller's cfuture slot by
 * cpromise_set_value(&request.promise, &result, kStorageErrorOk). Same struct
 * shape for every StorageCommandKind:
 *   - kStorageCommandRead:       blockId/length/data are the block read back.
 *   - kStorageCommandWrite:      blockId/length/data are unused (left zeroed);
 *                                success/failure is conveyed via cfuture_wait_for
 *                                return value and out_status.
 *   - kStorageCommandGetStatus:  only `ready` is meaningful; blockId/length/
 *                                data are unused (left zeroed).
 * Pass this type (not StorageRequest) as the payload_type to
 * CFUTURE_DEFINE_STATIC_BUFFERS / cfuture_pool_init's payload_size. */
typedef struct StorageResult
{
    uint32_t blockId;
    uint32_t length;                    /* bytes valid in data[], <= kStorageBlockSize */
    uint8_t data[kStorageBlockSize];
    bool ready;                         /* kStorageCommandGetStatus only */
} StorageResult;

/* One request, embedded by value into the OsalQueue that fronts
 * storage_service.c - never send a pointer to a requester task's stack-local
 * StorageRequest (that is exactly the dangling-pointer trap libcfuture
 * exists to avoid). storage_service.c dequeues its own local copy via
 * osal_queue_receive() and from then on operates on &request.promise out of
 * that local copy, per the libcfuture API notes. */
typedef struct StorageRequest
{
    StorageCommandKind command;
    uint32_t blockId;
    uint32_t length;                    /* bytes valid in writeData[], <= kStorageBlockSize;
                                            ignored for kStorageCommandRead/kStorageCommandGetStatus */
    uint8_t writeData[kStorageBlockSize]; /* kStorageCommandWrite only */
    cpromise_t promise;                 /* fulfilled with a StorageResult payload, see above */
} StorageRequest;
