#pragma once

#include "cfuture.h"
#include "osal/osal.h"
#include "pal/pal_led.h"
#include "pal/pal_log_sink.h"
#include "pal/pal_storage.h"
#include "pal/pal_time.h"

/* Everything the application layer needs from the outside world, built by
 * each target's own composition root (targets/{freertos,threadx}/pal/src/
 * board_start_app.c, or targets/{host,zephyr}/main.c) and injected here in
 * one shot - this struct is the dependency-injection boundary. Nothing in
 * app/ ever references a concrete OS or peripheral API directly; every
 * target-specific decision (which GPIO is the LED, which OS call creates a
 * thread, ...) lives on the other side of one of these fields. */
typedef struct AppDependencies
{
    PalLed led;
    PalLogSink logSink;
    PalTimeSource timeSource;
    PalStorage storage;
    const cfuture_sync_ops_t *cfutureSyncOps;

    /* USB Host processing thread. Leave entry NULL if this target has no
     * USB Host at all (the host build) or if its USB Host library pumps
     * itself on its own internally-spawned thread (FreeRTOS's
     * USBH_USE_OS=1) - appRun() only registers this thread when entry is
     * non-NULL. */
    OsalTaskEntryFn usbHostProcessEntry;
    void *usbHostProcessContext;
    uint32_t usbHostProcessStackBytes;

    /* Watchdog-refresh thread (crash_dump.h's crashDumpWatchdogTaskEntry on
     * the three embedded targets, which must also have already called
     * crashDumpEarlyInit() before appRun()). Leave entry NULL on host - no
     * hardware watchdog exists there. appRun() passes it the shared Logger
     * itself (for health-warning logging, see crash_dump.c) - there is
     * nothing target-specific to inject here. */
    OsalTaskEntryFn watchdogTaskEntry;
    uint32_t watchdogTaskStackBytes;
} AppDependencies;

/* The ONE place every application thread is started, for every target.
 * Builds the Logger, logs "System start", registers Blinky + (if
 * applicable) the USB Host process thread + the storage showcase's 5
 * threads into one AppThreadRegistry (app_threads.h), then starts all of
 * them with a single appThreadRegistryStartAll() call - no module besides
 * this one ever calls osal_task_create() itself.
 *
 * Call once, after board bring-up, with every field of *deps already
 * built. Returns false (and logs why) if anything failed to register or
 * start - this is not something to retry, it is a boot-time fault. */
bool appRun(const AppDependencies *deps) __attribute__((warn_unused_result));
