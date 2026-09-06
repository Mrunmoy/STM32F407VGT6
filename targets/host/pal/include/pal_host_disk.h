#pragma once

#include "pal_storage.h"

#include <stdint.h>

/* Host-only in-memory block device satisfying app/include/pal_storage.h's
 * PalStorage interface, for the host/POSIX build target (build.py --target
 * host). This is desktop test tooling, not device firmware: unlike the
 * board's USBH MSC adapter, pal_host_disk_create() below is allowed to malloc
 * its backing buffer once at startup - the repo's "no dynamic allocation"
 * rule targets app/ library and firmware code (bounded MCU RAM), not
 * this host-only test harness running as a normal Linux process.
 *
 * FatFS (external/fatfs/) is vendored and already runs against this exact
 * adapter - app/src/storage_service.c's mountVolume() calls f_mount()/
 * f_mkfs() through app/src/fatfs_diskio.c's diskio.h glue, which sits
 * directly on getSectorCount/getSectorSize/read/write/sync below, on every
 * target including this one. */

enum
{
    kPalHostDiskSectorSize = 512U,
    kPalHostDiskDefaultSectorCount = 131072U, /* 64 MiB @ 512 B/sector */
};

typedef struct PalHostDisk
{
    uint8_t *storage; /* sectorCount * kPalHostDiskSectorSize bytes, zero-filled */
    uint32_t sectorCount;
    bool ready;
} PalHostDisk;

/* Allocates disk's backing buffer (sectorCount * kPalHostDiskSectorSize
 * bytes, zero-filled) and fills in *outStorage as a PalStorage view over it.
 * *disk must outlive *outStorage - outStorage->context points back into it.
 * PalStorage.init (outStorage->init) must still be called before status/
 * read/write are meaningful, matching every other PalStorage adapter's
 * init-then-use contract. Returns false, leaving *disk and *outStorage
 * untouched, if sectorCount is 0 or the allocation fails. */
bool pal_host_disk_create(PalHostDisk *disk, uint32_t sectorCount, PalStorage *outStorage) __attribute__((warn_unused_result));

/* Frees the backing buffer. Any PalStorage handed out by the matching
 * pal_host_disk_create() call must not be used again afterward. Safe to call
 * on a PalHostDisk whose pal_host_disk_create() never succeeded (storage ==
 * NULL, e.g. a zero-initialized struct). */
void pal_host_disk_destroy(PalHostDisk *disk);
