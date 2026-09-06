#include "storage_service.h"

#include "app_task_trace.h"
#include "storage_protocol.h"

#include "cfuture.h"
#include "ff.h"

enum
{
    /* storageServiceTaskEntry()'s queue receive used to block forever
     * (UINT32_MAX) when idle - correct for "wait for work", wrong for
     * supervision: a task that only ever proves it's alive when work
     * happens to arrive is indistinguishable, from the outside, from one
     * that's actually deadlocked. Polling on a bounded wait instead means
     * it checks in at least this often even with zero requests arriving. */
    kStorageServiceIdlePollMs = 1000U,
};

/* Single backing file used as a flat array of kStorageBlockSize-byte blocks;
 * StorageRequest.blockId * kStorageBlockSize is its byte offset. Created
 * (if it doesn't already exist) the first time this task mounts the volume.
 * 8.3 short name - this project's ffconf.h fixes _USE_LFN == 0. */
static const char kBlockFileName[] = "STORAGE.BIN";

static bool mountVolume(FATFS *filesystem, Logger *logger)
{
    FRESULT result = f_mount(filesystem, "", 1U);

    /* A blank/never-formatted disk (a fresh host RAM-disk every run, or a
     * factory-blank USB stick) mounts as FR_NO_FILESYSTEM, not FR_OK - format
     * it once and retry, rather than treating that as a fatal error. Any
     * other f_mount failure (media not ready, I/O error) is left alone. */
    if (result == FR_NO_FILESYSTEM)
    {
        static uint8_t s_mkfsWorkBuffer[kStorageBlockSize];

        loggerLog(logger, kLogLevelEvent, "storage: no filesystem found, formatting");
        if (f_mkfs("", FM_ANY, 0U, s_mkfsWorkBuffer, sizeof(s_mkfsWorkBuffer)) != FR_OK)
        {
            loggerLog(logger, kLogLevelError, "storage: FatFS format failed");
            return false;
        }

        result = f_mount(filesystem, "", 1U);
    }

    if (result != FR_OK)
    {
        loggerLog(logger, kLogLevelError, "storage: FatFS mount failed");
        return false;
    }

    loggerLog(logger, kLogLevelEvent, "storage: FatFS mounted");
    return true;
}

static bool openBlockFile(FIL *file, Logger *logger)
{
    if (f_open(file, kBlockFileName, (BYTE)(FA_READ | FA_WRITE | FA_OPEN_ALWAYS)) != FR_OK)
    {
        loggerLog(logger, kLogLevelError, "storage: block file open failed");
        return false;
    }

    return true;
}

/* FSIZE_t is a 32-bit DWORD in this project's ffconf.h (_FS_EXFAT == 0), so
 * blockId * kStorageBlockSize can overflow it well before blockId itself
 * looks unreasonable - including storage_service.h's own
 * kStorageDemoSlowBlockId sentinel, which is deliberately huge so it can
 * never collide with a real block. Multiply in a wider type first and
 * reject anything that wouldn't survive the cast back down, rather than
 * silently wrapping into a bogus in-range offset. */
static bool computeBlockOffset(uint32_t blockId, FSIZE_t *outOffset)
{
    uint64_t offset = (uint64_t)blockId * (uint64_t)kStorageBlockSize;
    if (offset > (uint64_t)UINT32_MAX)
    {
        return false;
    }

    *outOffset = (FSIZE_t)offset;
    return true;
}

static int32_t handleRead(FIL *file, uint32_t blockId, StorageResult *outResult)
{
    FSIZE_t offset;
    if (!computeBlockOffset(blockId, &offset))
    {
        return kStorageErrorInvalidBlock;
    }

    if (f_lseek(file, offset) != FR_OK)
    {
        return kStorageErrorIoFailure;
    }

    if (f_tell(file) != offset)
    {
        /* lseek clamped at end-of-file: this block was never written. */
        return kStorageErrorInvalidBlock;
    }

    UINT bytesRead = 0U;
    if (f_read(file, outResult->data, (UINT)kStorageBlockSize, &bytesRead) != FR_OK)
    {
        return kStorageErrorIoFailure;
    }

    outResult->blockId = blockId;
    outResult->length = (uint32_t)bytesRead;
    return kStorageErrorOk;
}

static int32_t handleWrite(FIL *file, uint32_t blockId, const uint8_t *data, uint32_t length)
{
    if (length > kStorageBlockSize)
    {
        return kStorageErrorInvalidBlock;
    }

    FSIZE_t offset;
    if (!computeBlockOffset(blockId, &offset))
    {
        return kStorageErrorInvalidBlock;
    }

    if (f_lseek(file, offset) != FR_OK)
    {
        return kStorageErrorIoFailure;
    }

    UINT bytesWritten = 0U;
    if ((f_write(file, data, (UINT)length, &bytesWritten) != FR_OK) || (bytesWritten != length))
    {
        return kStorageErrorIoFailure;
    }

    if (f_sync(file) != FR_OK)
    {
        return kStorageErrorIoFailure;
    }

    return kStorageErrorOk;
}

/* Runs the potentially-slow FatFS work for one already-active request and
 * fulfills its promise. file is only dereferenced once volumeReady has
 * already been confirmed true by the caller. Returns whether the volume
 * should still be considered mounted afterward - see the kStorageErrorIoFailure
 * check below. */
static bool serviceRequest(FIL *file, bool volumeReady, StorageRequest *request)
{
    if (!cpromise_is_active(&request->promise))
    {
        cpromise_drop(&request->promise, kStorageErrorCancelled);
        return volumeReady;
    }

    if (!volumeReady)
    {
        cpromise_drop(&request->promise, kStorageErrorNotReady);
        return volumeReady;
    }

    /* Demo-only hook (see storage_service.h) - lets client_tasks.c stage a
     * deterministic timeout race instead of depending on real scheduling
     * jitter. Placed after the is_active/volumeReady checks above so it only
     * ever fires on a request T_S has already committed to servicing,
     * exactly like a real slow hardware operation would. */
    if (request->blockId == (uint32_t)kStorageDemoSlowBlockId)
    {
        osal_delay_ms(kStorageDemoSlowDelayMs);
    }

    StorageResult result = {0};
    int32_t errorCode;

    switch (request->command)
    {
        case kStorageCommandRead:
            errorCode = handleRead(file, request->blockId, &result);
            break;

        case kStorageCommandWrite:
            errorCode = handleWrite(file, request->blockId, request->writeData, request->length);
            break;

        case kStorageCommandGetStatus:
            result.ready = true;
            errorCode = kStorageErrorOk;
            break;

        default:
            errorCode = kStorageErrorInvalidBlock;
            break;
    }

    if (errorCode == kStorageErrorOk)
    {
        cpromise_set_value(&request->promise, &result, kStorageErrorOk);
    }
    else
    {
        cpromise_drop(&request->promise, errorCode);
    }

    /* kStorageErrorIoFailure means FatFS's disk_read/disk_write reported a
     * real I/O error via fatfs_diskio.c - most likely the media was removed
     * mid-operation (usbhMscDiskReadFn/WriteFn returns kPalStorageNotReady
     * the moment usbHostGetMscStatus() drops to disconnected, which maps to
     * RES_NOTRDY -> FR_DISK_ERR here). Force a remount attempt on the next
     * request rather than keep trusting a volume that no longer responds -
     * this is what actually makes the "recovers from an unplug/replug"
     * claim below true, instead of latching volumeReady=true forever after
     * the first successful mount. */
    return errorCode != kStorageErrorIoFailure;
}

void storageServiceTaskEntry(void *context)
{
    static const char kTaskName[] = "StorageSvc";

    StorageServiceConfig *config = (StorageServiceConfig *)context;

    FATFS filesystem;
    FIL file;
    bool volumeReady = false;

    /* Tracked separately from volumeReady, which serviceRequest() can drop
     * back to false after an I/O failure (see its own comment) without the
     * file actually having been closed - these two only ever go false->true,
     * so teardown below always knows exactly what to release regardless of
     * volumeReady's current value. */
    bool mounted = false;
    bool fileOpen = false;

    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);

        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }

        StorageRequest request;
        appTaskTraceCheckpoint(kTaskName, "waiting for queue");
        if (!osal_queue_receive(config->queue, &request, (uint32_t)kStorageServiceIdlePollMs))
        {
            /* Nothing arrived within the poll window - not an error, just
             * idle. Still counts as a completed loop iteration for check-in
             * purposes, which is the whole point of polling here instead of
             * blocking forever. */
            appTaskTraceLoopEnd(kTaskName);
            continue;
        }

        /* Mounting isn't retried just once at task start: on the FreeRTOS/
         * USBH target the underlying PalStorage typically isn't ready yet
         * at boot (USB enumeration is still in progress on another task,
         * see app.c/usb_host.c) - a mount attempt made only once here would
         * permanently latch volumeReady=false and drop every request
         * forever, well after the drive actually became ready (confirmed on
         * real hardware: usbHostGetMscStatus() reached kUsbHostMscReady
         * seconds after this task had already tried and failed once). Retry
         * lazily whenever a request arrives and the volume isn't mounted
         * yet - this also means the demo recovers from an unplug/replug,
         * not just from slow boot-time enumeration. */
        if (!volumeReady)
        {
            appTaskTraceCheckpoint(kTaskName, "mounting");
            volumeReady = mountVolume(&filesystem, config->logger);
            if (volumeReady)
            {
                mounted = true;
                volumeReady = openBlockFile(&file, config->logger);
                fileOpen = volumeReady;
            }
        }

        appTaskTraceCheckpoint(kTaskName, "servicing request");
        volumeReady = serviceRequest(&file, volumeReady, &request);

        appTaskTraceLoopEnd(kTaskName);
    }

    appTaskTraceCheckpoint(kTaskName, "stopping");
    if (fileOpen)
    {
        (void)f_close(&file);
    }
    if (mounted)
    {
        /* Unregisters this drive's FatFS work area - the FatFS-documented
         * way to cleanly detach a volume (f_mount with a NULL FATFS*). */
        (void)f_mount(NULL, "", 0U);
    }
    loggerLog(config->logger, kLogLevelEvent, "storage: Servicer stopped");
    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}
