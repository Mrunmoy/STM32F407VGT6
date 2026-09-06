#include "usb_host.h"

#include "app_task_trace.h"
#include "osal.h"

#include "usbh_core.h"
#include "usbh_msc.h"

void Error_Handler(void);

enum
{
    kUsbHostId = 0U, /* single OTG_FS Host instance - id is otherwise unused
                       * by this project (passed through to phost->id/pUser) */
    kUsbHostProcessPollMs = 10U,

    /* USBH_MSC_Read/Write's own internal bound is ~10s per sector (see
     * external/usb_host_lib/Class/MSC/Src/usbh_msc.c - phost->Timer, driven
     * by HAL_HCD_SOF_Callback(), a USB hardware SOF interrupt independent
     * of any task's health). This lock is only ever held for one of those
     * calls or for one USBH_Process() tick (fast), so a timeout comfortably
     * past that vendor bound means a timeout here is a genuine signal, not
     * a false alarm from normal contention. */
    kUsbHostLockTimeoutMs = 12000U,
};

/* USB Host Core handle declaration. */
USBH_HandleTypeDef hUsbHostFS;

/* See usb_host.h's own doc comment - serializes USBH_Process() (below)
 * against usbh_msc_disk.c's direct USBH_MSC_Read/Write calls. */
static OsalMutexHandle s_lock;

void usbHostLockInit(void)
{
    if (s_lock == NULL)
    {
        (void)osal_mutex_create(&s_lock);
    }
}

bool usbHostLock(void)
{
    /* Fail closed: if the lock was somehow never created, treat that as
     * "could not acquire" rather than silently operating unprotected -
     * usbHostLockInit() is always called from the single-threaded
     * composition root before either caller of this function can possibly
     * run, so s_lock should never actually be NULL here in practice. */
    if (s_lock == NULL)
    {
        return false;
    }

    return osal_mutex_lock(s_lock, (uint32_t)kUsbHostLockTimeoutMs);
}

void usbHostUnlock(void)
{
    if (s_lock != NULL)
    {
        osal_mutex_unlock(s_lock);
    }
}

/* Connect/ready/disconnect state, written only from usbHostUserProcess()
 * (called synchronously from USBH_Process(), itself only ever pumped by
 * usbHostProcessTaskEntry()'s own thread) and read by any task polling
 * usbHostGetMscStatus() - volatile is sufficient synchronization here:
 * this is a single word-sized enum, and Cortex-M word loads/stores are
 * atomic. */
static volatile UsbHostMscStatus s_mscStatus = kUsbHostMscDisconnected;

static void usbHostUserProcess(USBH_HandleTypeDef *phost, uint8_t id);

void MX_USB_HOST_Init(void)
{
    if (USBH_Init(&hUsbHostFS, usbHostUserProcess, kUsbHostId) != USBH_OK)
    {
        Error_Handler();
    }

    if (USBH_RegisterClass(&hUsbHostFS, USBH_MSC_CLASS) != USBH_OK)
    {
        Error_Handler();
    }

    if (USBH_Start(&hUsbHostFS) != USBH_OK)
    {
        Error_Handler();
    }
}

void usbHostProcessTaskEntry(void *context)
{
    static const char kTaskName[] = "UsbHostProcess";

    (void)context;

    appTaskTraceCheckpoint(kTaskName, "MX_USB_HOST_Init");
    MX_USB_HOST_Init();

    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);

        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }

        /* A lock timeout here just means usbh_msc_disk.c is mid-transfer
         * (bounded at ~10s, see kUsbHostLockTimeoutMs) - skip this tick and
         * try again next time rather than proceeding unprotected. */
        appTaskTraceCheckpoint(kTaskName, "locking");
        if (usbHostLock())
        {
            appTaskTraceCheckpoint(kTaskName, "USBH_Process");
            (void)USBH_Process(&hUsbHostFS);
            usbHostUnlock();
        }

        appTaskTraceLoopEnd(kTaskName);
        osal_delay_ms(kUsbHostProcessPollMs);
    }

    /* Nothing owned across iterations to release - the lock above is never
     * held past a single tick. Stopping this task does mean USBH_Process()
     * never runs again: usbh_msc_disk.c's direct USBH_MSC_* calls still go
     * through usbHostLock() (bounded, see kUsbHostLockTimeoutMs) so they
     * fail closed with a timeout rather than hang - this task stopping is
     * safe, just means USB stops working, which is the expected effect of
     * stopping it. */
    appTaskTraceCheckpoint(kTaskName, "stopped");
    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}

UsbHostMscStatus usbHostGetMscStatus(void)
{
    return s_mscStatus;
}

/**
  * @brief  USBH_UserProcess callback registered with USBH_Init(). Called
  *         synchronously from USBH_Process(), i.e. from
  *         usbHostProcessTaskEntry()'s own thread - kept non-blocking/short
  *         per ST's own convention (only updates s_mscStatus, never touches
  *         FatFS or blocks on anything).
  * @param  phost: Host handle
  * @param  id: HOST_USER_* event id (usbh_core.h)
  * @retval None
  */
static void usbHostUserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
    (void)phost;

    switch (id)
    {
        case HOST_USER_CONNECTION:
            s_mscStatus = kUsbHostMscConnected;
            break;

        case HOST_USER_CLASS_ACTIVE:
            /* The MSC class driver has finished its own internal SCSI
             * INQUIRY/READ CAPACITY/TEST UNIT READY sequence - LUN(s) are
             * usable from here on. Do not call USBH_MSC_Read/Write or
             * f_mount before this fires. */
            s_mscStatus = kUsbHostMscReady;
            break;

        case HOST_USER_DISCONNECTION:
            s_mscStatus = kUsbHostMscDisconnected;
            break;

        case HOST_USER_UNRECOVERED_ERROR:
            /* BOT/SCSI phase error exhausted retries - treat the same as a
             * disconnect: the media can no longer be trusted. */
            s_mscStatus = kUsbHostMscDisconnected;
            break;

        default:
            /* HOST_USER_SELECT_CONFIGURATION / HOST_USER_CLASS_SELECTED:
             * no action needed for a single-configuration MSC device. */
            break;
    }
}
