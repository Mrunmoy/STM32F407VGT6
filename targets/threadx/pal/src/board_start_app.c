#include "board_start_app.h"

#include "app.h"
#include "board_led.h"
#include "cfuture_sync_ops.h"
#include "crash_dump.h"
#include "log_sink_impl.h"
#include "osal_byte_pool.h"
#include "usb_host.h"
#include "usbh_msc_disk.h"
#include "time_source_impl.h"

enum
{
    /* USB Host's own state machine needs headroom for descriptor/SCSI
     * buffers on its stack, not just its own frame. */
    kUsbHostProcessStackBytes = 4096U,
    kWatchdogStackBytes = 128U * 4U * 4U,
};

static PalStorage s_mscStorage;

void board_start_app(TX_BYTE_POOL *bytePool)
{
    /* First thing, before anything else can fault: route MemManage/Bus/
     * UsageFault to their own handlers and arm the independent watchdog -
     * see crash_dump.h's own doc comment. */
    crashDumpEarlyInit();

    /* Must run before any osal_task_create()/osal_queue_create() call - every
     * concrete OSAL allocation in this target comes out of this same byte
     * pool. */
    osal_byte_pool_init(bytePool);

    AppDependencies deps = {0};

    if (!board_led_init(&deps.led))
    {
        /* Same boot-time-fault treatment as freertos's board_start_app.c -
         * a visible LED blink pattern then reset, instead of silently
         * letting tx_kernel_enter() continue with an incomplete/empty
         * thread registry. */
        crashDumpHalt();
    }

    log_sink_init(&deps.logSink);
    time_source_init(&deps.timeSource);

    usbhMscDiskInit(&s_mscStorage);
    deps.storage = s_mscStorage;

    deps.cfutureSyncOps = cfuture_sync_ops_get();

    /* USBH_USE_OS=0 on this target (usbh_conf.h) - nothing else drives the
     * USB Host state machine forward, so appRun() must start a thread that
     * pumps USBH_Process() forever. */
    deps.usbHostProcessEntry = usbHostProcessTaskEntry;
    deps.usbHostProcessContext = NULL;
    deps.usbHostProcessStackBytes = kUsbHostProcessStackBytes;

    deps.watchdogTaskEntry = crashDumpWatchdogTaskEntry;
    deps.watchdogTaskStackBytes = kWatchdogStackBytes;

    if (!appRun(&deps))
    {
        crashDumpHalt();
    }
}
