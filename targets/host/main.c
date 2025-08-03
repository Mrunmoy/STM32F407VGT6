#include "app.h"
#include "client_tasks.h"
#include "osal.h"
#include "pal_host_disk.h"

#include "board_led.h"
#include "cfuture_sync_ops.h"
#include "log_sink_impl.h"
#include "time_source_impl.h"

#include <stdio.h>

enum
{
    kDemoRunSeconds = 5U,
};

int main(void)
{
    static PalHostDisk disk;
    PalStorage storage;
    if (!pal_host_disk_create(&disk, kPalHostDiskDefaultSectorCount, &storage))
    {
        fprintf(stderr, "host: failed to create backing disk\n");
        return 1;
    }

    AppDependencies deps = {0};
    if (!board_led_init(&deps.led))
    {
        fprintf(stderr, "host: LED device not ready\n");
        pal_host_disk_destroy(&disk);
        return 1;
    }
    log_sink_init(&deps.logSink);
    time_source_init(&deps.timeSource);
    deps.storage = storage;
    deps.cfutureSyncOps = cfuture_sync_ops_get();
    /* No USB Host on this target - deps.usbHostProcessEntry stays NULL. */

    if (!appRun(&deps))
    {
        pal_host_disk_destroy(&disk);
        return 1;
    }

    /* appRun() spawns detached background tasks (pthreads on this target)
     * and returns immediately - osal.h exposes no join/wait, so we just let
     * the scenarios run for a fixed window before exiting. */
    osal_delay_ms(kDemoRunSeconds * 1000U);

    /* Turn "did any of the 4 concurrency scenarios fail" into a real exit
     * code instead of a log line nobody's necessarily reading - without
     * this, a genuine regression (e.g. scenario2 logging FAIL every run)
     * still exits 0, so `build.py --target host --run` or any CI wrapping
     * it would report success. */
    uint32_t failureCount = clientTasksFailureCount();
    if (failureCount > 0U)
    {
        fprintf(stderr, "host: %u scenario failure(s) logged - see EVENT/ERROR lines above\n",
                (unsigned)failureCount);
        pal_host_disk_destroy(&disk);
        return 1;
    }

    printf("host: storage demo run complete, all scenarios passing\n");
    pal_host_disk_destroy(&disk);

    return 0;
}
