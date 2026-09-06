#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app.h"
#include "board_led.h"
#include "cfuture_sync_ops.h"
#include "crash_dump.h"
#include "log_sink_impl.h"
#include "usb_host.h"
#include "usbh_msc_disk.h"
#include "time_source_impl.h"

enum
{
    kUsbHostProcessStackBytes = 4096U,
    kWatchdogStackBytes = 128U * 4U * 4U,
};

static PalStorage s_mscStorage;

int main(void)
{
    /* First thing, before anything else can fault: route MemManage/Bus/
     * UsageFault to their own handlers and arm the independent watchdog -
     * see crash_dump.h's own doc comment. Zephyr's own fault path
     * (k_sys_fatal_error_handler(), pal/src/crash_dump_zephyr.c) still
     * gets first look at any fault regardless; this only affects the raw
     * CPU vector routing / watchdog arming, both boot-time, both orthogonal
     * to that. */
    crashDumpEarlyInit();

    AppDependencies deps = {0};

    if (!board_led_init(&deps.led))
    {
        printk("[ERROR] LED device not ready\r\n");
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

    return 0;
}
