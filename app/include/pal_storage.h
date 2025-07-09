#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Result codes for read/write/sync. Numeric values are chosen to match
 * FatFS's diskio.h DRESULT exactly (RES_OK=0, RES_ERROR=1, RES_WRPRT=2,
 * RES_NOTRDY=3, RES_PARERR=4) so the diskio.c glue that sits on top of a
 * PalStorage can return this value as a DRESULT with a plain cast - no
 * translation table needed. This header still does not include any FatFS
 * header, so PalStorage stays usable from host tests / other consumers that
 * know nothing about FatFS. */
typedef enum PalStorageResult
{
    kPalStorageOk = 0,
    kPalStorageError = 1,
    kPalStorageWriteProtected = 2,
    kPalStorageNotReady = 3,
    kPalStorageParamError = 4,
} PalStorageResult;

/* init: brings the underlying media up far enough for status/read/write to be
 * meaningful (e.g. wait for USBH_MSC_UnitIsReady, or open the backing host
 * file). Returns false on failure without side effects the caller must undo -
 * callers should treat false as "media not present/ready", not fatal. */
typedef bool (*PalStorageInitFn)(void *context);

/* status: returns true if the media is present and ready for read/write right
 * now (e.g. a USB stick is enumerated and its LUN is ready). FatFS's diskio.c
 * glue calls this on every disk_status(); it must be cheap/non-blocking. */
typedef bool (*PalStorageStatusFn)(void *context);

/* read/write: sector-addressed, matching FatFS's disk_read/disk_write
 * exactly - sector is a 0-based LBA, count is a number of consecutive
 * sectors, buffer holds count * sectorSize bytes. sectorSize is whatever
 * getSectorSize reports (512 for every implementation this project ships). */
typedef PalStorageResult (*PalStorageReadFn)(void *context, uint8_t *buffer, uint32_t sector, uint32_t count);
typedef PalStorageResult (*PalStorageWriteFn)(void *context, const uint8_t *buffer, uint32_t sector, uint32_t count);

/* sync: flush any write cache to the physical/logical media. Implementations
 * with no write cache (e.g. USBH_MSC's synchronous BOT transfers) may just
 * return kPalStorageOk unconditionally. */
typedef PalStorageResult (*PalStorageSyncFn)(void *context);

/* getSectorCount/getSectorSize: capacity queries FatFS needs for f_mkfs /
 * f_getfree (GET_SECTOR_COUNT/GET_SECTOR_SIZE diskio_ioctl commands). Return
 * false if capacity isn't known yet (e.g. media not ready) - callers must not
 * trust *outXxx in that case. */
typedef bool (*PalStorageGetSectorCountFn)(void *context, uint32_t *outSectorCount);
typedef bool (*PalStorageGetSectorSizeFn)(void *context, uint32_t *outSectorSize);

/* Abstract block-storage device (dependency inversion): FatFS's diskio glue
 * depends on this, never on a concrete USBH MSC class driver or host-side
 * RAM/file disk directly. One PalStorage instance per physical/logical
 * drive - the two concrete adapters this project ships (a host RAM/file disk
 * for the host build, a USBH MSC disk for the FreeRTOS/board build) each fill
 * in one of these and are otherwise unknown to FatFS's diskio.c glue. */
typedef struct PalStorage
{
    PalStorageInitFn init;
    PalStorageStatusFn status;
    PalStorageReadFn read;
    PalStorageWriteFn write;
    PalStorageSyncFn sync;
    PalStorageGetSectorCountFn getSectorCount;
    PalStorageGetSectorSizeFn getSectorSize;
    void *context;
} PalStorage;
