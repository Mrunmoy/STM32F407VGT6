#include "usbh_msc_disk.h"

#include "storage_protocol.h"
#include "usb_host.h"

#include "usbh_msc.h"
#include "usbh_msc_scsi.h"

enum
{
    /* This project only ever talks to a single USB mass-storage stick, not a
     * hub or multi-LUN enclosure - LUN 0 is the only one ever queried. */
    kUsbhMscLun = 0U,
};

/* Maps a failed USBH_MSC_Read/Write onto a PalStorageResult by inspecting the
 * SCSI sense data USBH_MSC_GetLUNInfo() reports - same mapping ST's own
 * usbh_diskio_dma.c reference uses for FatFS's DRESULT, kept here 1:1 since
 * PalStorageResult's values are numerically identical to DRESULT. isWrite
 * additionally distinguishes SCSI_ASC_WRITE_PROTECTED, which only makes
 * sense as a write failure. */
static PalStorageResult usbhMscDiskTranslateError(bool isWrite)
{
    MSC_LUNTypeDef info = {0};

    if (!usbHostLock())
    {
        /* Couldn't even ask why the read/write that got us here failed -
         * report not-ready rather than guessing at a more specific cause. */
        return kPalStorageNotReady;
    }
    USBH_StatusTypeDef status = USBH_MSC_GetLUNInfo(&hUsbHostFS, kUsbhMscLun, &info);
    usbHostUnlock();

    if (status != USBH_OK)
    {
        return kPalStorageError;
    }

    if (isWrite && (info.sense.asc == SCSI_ASC_WRITE_PROTECTED))
    {
        return kPalStorageWriteProtected;
    }

    switch (info.sense.asc)
    {
        case SCSI_ASC_LOGICAL_UNIT_NOT_READY:
        case SCSI_ASC_MEDIUM_NOT_PRESENT:
        case SCSI_ASC_NOT_READY_TO_READY_CHANGE:
            return kPalStorageNotReady;

        default:
            return kPalStorageError;
    }
}

/* Fetches the LUN's current capacity (block count + block size). Returns
 * false if the info isn't available (not ready) OR if the device's real
 * block size doesn't match kStorageBlockSize (storage_protocol.h) - every
 * other layer of this project (FatFS's ffconf.h _MIN_SS/_MAX_SS,
 * storage_service.c's s_mkfsWorkBuffer, StorageRequest's data[]/writeData[]
 * arrays) is hardcoded to exactly kStorageBlockSize-byte sectors. A device
 * that reports a different block size (e.g. a 4Kn "Advanced Format" drive)
 * would otherwise drive USBH_MSC_Read/Write to transfer
 * count*device_block_size bytes into/out of buffers sized for
 * count*kStorageBlockSize - an out-of-bounds read or write. Rejecting
 * unsupported media here is the correct fix, not a workaround: this project
 * has no code path that could safely operate on a non-512-byte-sector
 * device regardless. */
static bool usbhMscQueryCapacity(MSC_LUNTypeDef *outInfo)
{
    if (usbHostGetMscStatus() != kUsbHostMscReady)
    {
        return false;
    }

    if (!usbHostLock())
    {
        return false;
    }
    USBH_StatusTypeDef status = USBH_MSC_GetLUNInfo(&hUsbHostFS, kUsbhMscLun, outInfo);
    usbHostUnlock();

    if (status != USBH_OK)
    {
        return false;
    }

    return outInfo->capacity.block_size == (uint16_t)kStorageBlockSize;
}

static bool usbhMscDiskInitFn(void *context)
{
    MSC_LUNTypeDef info = {0};

    (void)context;

    /* Real bring-up (USBH_Init/USBH_RegisterClass/USBH_Start) runs lazily
     * from usbHostProcessTaskEntry()'s own thread (app/src/usb_host.c) - not
     * synchronously "at boot" the way an older version of this comment
     * claimed. Calling MX_USB_HOST_Init() any earlier than that (e.g. from
     * this target's own pre-scheduler composition root) hung real hardware
     * inside HAL_Delay() with interrupts masked - see board_start_app.c's
     * own comment. This hook has nothing to bring up itself; it just reports
     * whether enumeration has reached kUsbHostMscReady yet with a supported
     * block size. */
    return usbhMscQueryCapacity(&info);
}

static bool usbhMscDiskStatusFn(void *context)
{
    MSC_LUNTypeDef info = {0};

    (void)context;

    if (!usbhMscQueryCapacity(&info))
    {
        return false;
    }

    if (!usbHostLock())
    {
        return false;
    }
    bool ready = USBH_MSC_UnitIsReady(&hUsbHostFS, kUsbhMscLun) != 0U;
    usbHostUnlock();

    return ready;
}

static PalStorageResult usbhMscDiskReadFn(void *context, uint8_t *buffer, uint32_t sector, uint32_t count)
{
    MSC_LUNTypeDef info = {0};

    (void)context;

    if (!usbhMscQueryCapacity(&info))
    {
        return kPalStorageNotReady;
    }

    if (((uint64_t)sector + (uint64_t)count) > (uint64_t)info.capacity.block_nbr)
    {
        return kPalStorageParamError;
    }

    if (!usbHostLock())
    {
        return kPalStorageNotReady;
    }
    USBH_StatusTypeDef status = USBH_MSC_Read(&hUsbHostFS, kUsbhMscLun, sector, buffer, count);
    usbHostUnlock();

    if (status == USBH_OK)
    {
        return kPalStorageOk;
    }

    return usbhMscDiskTranslateError(false);
}

static PalStorageResult usbhMscDiskWriteFn(void *context, const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    MSC_LUNTypeDef info = {0};

    (void)context;

    if (!usbhMscQueryCapacity(&info))
    {
        return kPalStorageNotReady;
    }

    if (((uint64_t)sector + (uint64_t)count) > (uint64_t)info.capacity.block_nbr)
    {
        return kPalStorageParamError;
    }

    /* USBH_MSC_Write's pbuf parameter is non-const (the BOT/SCSI layer never
     * writes through it) - cast away const at this one boundary rather than
     * widen PalStorageWriteFn's signature. */
    if (!usbHostLock())
    {
        return kPalStorageNotReady;
    }
    USBH_StatusTypeDef status = USBH_MSC_Write(&hUsbHostFS, kUsbhMscLun, sector, (uint8_t *)buffer, count);
    usbHostUnlock();

    if (status == USBH_OK)
    {
        return kPalStorageOk;
    }

    return usbhMscDiskTranslateError(true);
}

static PalStorageResult usbhMscDiskSyncFn(void *context)
{
    (void)context;

    /* USBH_MSC_Write's Bulk-Only Transport transfers are synchronous - there
     * is no write cache at this layer to flush. */
    return kPalStorageOk;
}

static bool usbhMscDiskGetSectorCountFn(void *context, uint32_t *outSectorCount)
{
    MSC_LUNTypeDef info = {0};

    (void)context;

    if (!usbhMscQueryCapacity(&info))
    {
        return false;
    }

    *outSectorCount = info.capacity.block_nbr;
    return true;
}

static bool usbhMscDiskGetSectorSizeFn(void *context, uint32_t *outSectorSize)
{
    MSC_LUNTypeDef info = {0};

    (void)context;

    if (!usbhMscQueryCapacity(&info))
    {
        return false;
    }

    *outSectorSize = info.capacity.block_size;
    return true;
}

void usbhMscDiskInit(PalStorage *outStorage)
{
    usbHostLockInit();

    outStorage->init = usbhMscDiskInitFn;
    outStorage->status = usbhMscDiskStatusFn;
    outStorage->read = usbhMscDiskReadFn;
    outStorage->write = usbhMscDiskWriteFn;
    outStorage->sync = usbhMscDiskSyncFn;
    outStorage->getSectorCount = usbhMscDiskGetSectorCountFn;
    outStorage->getSectorSize = usbhMscDiskGetSectorSizeFn;
    outStorage->context = NULL;
}
