#include "board_start_app.h"

#include "app.h"
#include "board_led.h"
#include "cfuture_sync_ops.h"
#include "crash_dump.h"
#include "log_sink_impl.h"
#include "rtc_time_source.h"
#include "usb_host.h"
#include "usbh_msc_disk.h"
#include "time_source_impl.h"

enum
{
    /* Matches ThreadX/Zephyr's sizing for the identical shared
     * usbHostProcessTaskEntry()/USBH_Process() call chain - this used to be
     * 2048 bytes here (128*4*4) while the other two targets already used
     * 4096 for the same code path, an unexplained, unjustified divergence
     * this project has already been bitten by once for an underestimated
     * task stack (see storage_demo.c's own comment on that incident). */
    kUsbHostProcessStackBytes = 4096U,
    kWatchdogStackBytes = 128U * 4U * 4U,
};

static PalStorage s_mscStorage;

void board_start_app(void)
{
    /* First thing, before anything else can fault: route MemManage/Bus/
     * UsageFault to their own handlers and arm the independent watchdog -
     * see crash_dump.h's own doc comment. */
    crashDumpEarlyInit();

    AppDependencies deps = {0};

    if (!board_led_init(&deps.led))
    {
        /* No logger yet at this point - nothing to log to, but this is
         * still a boot-time fault worth stopping for rather than silently
         * continuing into osKernelStart() with an incomplete/empty thread
         * registry (deps.led is zero-initialized, its .set is NULL -
         * blinky_task.c would crash calling through a NULL function
         * pointer). crashDumpHalt() gives the same visible signal (LED
         * blink pattern, then reset) any other boot-time fault gets. */
        crashDumpHalt();
    }

    log_sink_init(&deps.logSink);

    if (!rtc_time_source_init(&deps.timeSource))
    {
        time_source_init(&deps.timeSource);
    }

    usbhMscDiskInit(&s_mscStorage);
    deps.storage = s_mscStorage;

    deps.cfutureSyncOps = cfuture_sync_ops_get();

    /* usbHostProcessTaskEntry() (shared app/) calls MX_USB_HOST_Init() from
     * inside this task's own running context, then pumps USBH_Process()
     * forever - required for correctness on real hardware, not just
     * consistency: HAL_HCD_Init()'s HAL_Delay() loops hung when
     * MX_USB_HOST_Init() was ever called from board_start_app()'s own
     * pre-osKernelStart() context (confirmed via JLink: stuck inside
     * HAL_Delay() with interrupts left masked). Registering it as a task
     * here means it only actually runs once osKernelStart() has begun and
     * this task is scheduled, exactly like every other target's identical
     * usbHostProcessTaskEntry registration. */
    deps.usbHostProcessEntry = usbHostProcessTaskEntry;
    deps.usbHostProcessContext = NULL;
    deps.usbHostProcessStackBytes = kUsbHostProcessStackBytes;

    deps.watchdogTaskEntry = crashDumpWatchdogTaskEntry;
    deps.watchdogTaskStackBytes = kWatchdogStackBytes;

    if (!appRun(&deps))
    {
        /* Same treatment as the board_led_init() failure above - a thread
         * that couldn't be registered/started is a boot-time fault, not
         * something to silently keep running half-started from. */
        crashDumpHalt();
    }
}
