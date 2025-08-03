#include "fatfs_diskio.h"

#include "diskio.h"

#include <stddef.h>

/* Single-volume design: this project's ffconf.h fixes _VOLUMES == 1, so
 * there is exactly one physical drive and exactly one PalStorage bound to
 * it - no ff_gen_drv-style multi-driver dispatch table is needed. */
enum
{
    kFatfsDiskioDrive = 0U,
};

static PalStorage *s_storage = NULL;

void fatfsDiskioBind(PalStorage *storage)
{
    s_storage = storage;
}

/* PalStorageResult's numeric values are chosen to match DRESULT exactly
 * (see pal_storage.h), but the two are still distinct enum types - translate
 * explicitly rather than casting, so a future value added to one enum
 * without the other trips a compiler warning here instead of misbehaving
 * silently. */
static DRESULT toDresult(PalStorageResult result)
{
    switch (result)
    {
        case kPalStorageOk:
            return RES_OK;
        case kPalStorageWriteProtected:
            return RES_WRPRT;
        case kPalStorageNotReady:
            return RES_NOTRDY;
        case kPalStorageParamError:
            return RES_PARERR;
        case kPalStorageError:
        default:
            return RES_ERROR;
    }
}

DSTATUS disk_status(BYTE pdrv)
{
    if ((pdrv != kFatfsDiskioDrive) || (s_storage == NULL))
    {
        return STA_NOINIT;
    }

    return s_storage->status(s_storage->context) ? 0U : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if ((pdrv != kFatfsDiskioDrive) || (s_storage == NULL))
    {
        return STA_NOINIT;
    }

    if (!s_storage->init(s_storage->context))
    {
        return STA_NOINIT;
    }

    return s_storage->status(s_storage->context) ? 0U : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv != kFatfsDiskioDrive) || (s_storage == NULL))
    {
        return RES_NOTRDY;
    }

    return toDresult(s_storage->read(s_storage->context, buff, (uint32_t)sector, (uint32_t)count));
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv != kFatfsDiskioDrive) || (s_storage == NULL))
    {
        return RES_NOTRDY;
    }

    return toDresult(s_storage->write(s_storage->context, buff, (uint32_t)sector, (uint32_t)count));
}

static DRESULT diskIoctlGetSectorCount(void *buff)
{
    uint32_t sectorCount = 0U;

    if (!s_storage->getSectorCount(s_storage->context, &sectorCount))
    {
        return RES_ERROR;
    }

    *(DWORD *)buff = (DWORD)sectorCount;
    return RES_OK;
}

static DRESULT diskIoctlGetSectorSize(void *buff)
{
    uint32_t sectorSize = 0U;

    if (!s_storage->getSectorSize(s_storage->context, &sectorSize))
    {
        return RES_ERROR;
    }

    *(WORD *)buff = (WORD)sectorSize;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if ((pdrv != kFatfsDiskioDrive) || (s_storage == NULL))
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
        case CTRL_SYNC:
            return toDresult(s_storage->sync(s_storage->context));
        case GET_SECTOR_COUNT:
            return diskIoctlGetSectorCount(buff);
        case GET_SECTOR_SIZE:
            return diskIoctlGetSectorSize(buff);
        case GET_BLOCK_SIZE:
            /* PalStorage exposes no erase-block-size query; report a
             * conservative single-sector block so f_mkfs() stays correct
             * (just less optimally aligned) rather than failing outright. */
            *(DWORD *)buff = 1U;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
