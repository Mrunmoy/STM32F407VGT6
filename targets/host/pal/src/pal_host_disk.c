#include "pal_host_disk.h"

#include <stdlib.h>
#include <string.h>

static bool pal_host_disk_init(void *context)
{
    PalHostDisk *disk = (PalHostDisk *)context;
    if ((disk == NULL) || (disk->storage == NULL))
    {
        return false;
    }

    /* Nothing to bring up on an in-memory buffer - it is allocated and
     * zero-filled at pal_host_disk_create() time already. This just flips the
     * same "media present and ready" latch every other PalStorage adapter's
     * init() flips, so status()/read()/write() behave consistently whether
     * or not the caller bothered to call init() first. */
    disk->ready = true;
    return true;
}

static bool pal_host_disk_status(void *context)
{
    const PalHostDisk *disk = (const PalHostDisk *)context;
    return (disk != NULL) && disk->ready;
}

static PalStorageResult pal_host_disk_read(void *context, uint8_t *buffer, uint32_t sector, uint32_t count)
{
    PalHostDisk *disk = (PalHostDisk *)context;
    if ((disk == NULL) || (buffer == NULL) || (count == 0U))
    {
        return kPalStorageParamError;
    }
    if (!disk->ready)
    {
        return kPalStorageNotReady;
    }
    if (((uint64_t)sector + (uint64_t)count) > (uint64_t)disk->sectorCount)
    {
        return kPalStorageParamError;
    }

    const size_t offset = (size_t)sector * (size_t)kPalHostDiskSectorSize;
    const size_t length = (size_t)count * (size_t)kPalHostDiskSectorSize;
    memcpy(buffer, disk->storage + offset, length);
    return kPalStorageOk;
}

static PalStorageResult pal_host_disk_write(void *context, const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    PalHostDisk *disk = (PalHostDisk *)context;
    if ((disk == NULL) || (buffer == NULL) || (count == 0U))
    {
        return kPalStorageParamError;
    }
    if (!disk->ready)
    {
        return kPalStorageNotReady;
    }
    if (((uint64_t)sector + (uint64_t)count) > (uint64_t)disk->sectorCount)
    {
        return kPalStorageParamError;
    }

    const size_t offset = (size_t)sector * (size_t)kPalHostDiskSectorSize;
    const size_t length = (size_t)count * (size_t)kPalHostDiskSectorSize;
    memcpy(disk->storage + offset, buffer, length);
    return kPalStorageOk;
}

static PalStorageResult pal_host_disk_sync(void *context)
{
    const PalHostDisk *disk = (const PalHostDisk *)context;
    if (disk == NULL)
    {
        return kPalStorageParamError;
    }
    if (!disk->ready)
    {
        return kPalStorageNotReady;
    }

    /* No write cache in front of the in-memory buffer - writes are already
     * durable the moment pal_host_disk_write()'s memcpy returns. */
    return kPalStorageOk;
}

static bool pal_host_disk_get_sector_count(void *context, uint32_t *outSectorCount)
{
    const PalHostDisk *disk = (const PalHostDisk *)context;
    if ((disk == NULL) || (disk->storage == NULL) || (outSectorCount == NULL))
    {
        return false;
    }

    *outSectorCount = disk->sectorCount;
    return true;
}

static bool pal_host_disk_get_sector_size(void *context, uint32_t *outSectorSize)
{
    const PalHostDisk *disk = (const PalHostDisk *)context;
    if ((disk == NULL) || (disk->storage == NULL) || (outSectorSize == NULL))
    {
        return false;
    }

    *outSectorSize = (uint32_t)kPalHostDiskSectorSize;
    return true;
}

bool pal_host_disk_create(PalHostDisk *disk, uint32_t sectorCount, PalStorage *outStorage)
{
    if ((disk == NULL) || (outStorage == NULL) || (sectorCount == 0U))
    {
        return false;
    }

    const size_t totalBytes = (size_t)sectorCount * (size_t)kPalHostDiskSectorSize;
    uint8_t *storage = (uint8_t *)calloc(1U, totalBytes);
    if (storage == NULL)
    {
        return false;
    }

    disk->storage = storage;
    disk->sectorCount = sectorCount;
    disk->ready = false;

    outStorage->init = pal_host_disk_init;
    outStorage->status = pal_host_disk_status;
    outStorage->read = pal_host_disk_read;
    outStorage->write = pal_host_disk_write;
    outStorage->sync = pal_host_disk_sync;
    outStorage->getSectorCount = pal_host_disk_get_sector_count;
    outStorage->getSectorSize = pal_host_disk_get_sector_size;
    outStorage->context = disk;

    return true;
}

void pal_host_disk_destroy(PalHostDisk *disk)
{
    if (disk == NULL)
    {
        return;
    }

    free(disk->storage);
    disk->storage = NULL;
    disk->sectorCount = 0U;
    disk->ready = false;
}
