#include "app.h"

#include "app_threads.h"
#include "blinky_task.h"
#include "logger.h"
#include "storage_demo.h"

enum
{
    kBlinkyStackBytes = 128U * 4U * 4U,
    kBlinkyOnTimeMs = 500U,
    kBlinkyOffTimeMs = 500U,
};

static Logger s_logger;
static BlinkyTaskConfig s_blinkyConfig;

/* Copied by value from *deps (see appRun()) rather than aliased via
 * &deps->storage - deps itself is typically a target's local/stack-built
 * AppDependencies that does not outlive the call to appRun(), but
 * storageDemoRegister()'s fatfsDiskioBind() keeps this pointer for the
 * lifetime of the program. PalStorage is a plain struct of function
 * pointers + one context pointer (pal_storage.h), safe to copy - same
 * reasoning loggerInit() already applies to LogSink/TimeSource above. */
static PalStorage s_storage;

static bool registerBlinky(AppThreadRegistry *registry, LedDevice led)
{
    s_blinkyConfig.led = led;
    s_blinkyConfig.logger = &s_logger;
    s_blinkyConfig.onTimeMs = kBlinkyOnTimeMs;
    s_blinkyConfig.offTimeMs = kBlinkyOffTimeMs;

    OsalTaskConfig config = {0};
    config.name = "Blinky";
    config.entry = blinkyTaskEntry;
    config.context = &s_blinkyConfig;
    config.stackSizeBytes = kBlinkyStackBytes;
    config.priority = kOsalPriorityNormal;

    return appThreadRegistryAdd(registry, &config);
}

static bool registerUsbHostProcess(AppThreadRegistry *registry, const AppDependencies *deps)
{
    if (deps->usbHostProcessEntry == NULL)
    {
        return true; /* not applicable on this target - not a failure */
    }

    OsalTaskConfig config = {0};
    config.name = "UsbHostProcess";
    config.entry = deps->usbHostProcessEntry;
    config.context = deps->usbHostProcessContext;
    config.stackSizeBytes = deps->usbHostProcessStackBytes;
    config.priority = kOsalPriorityHigh;

    return appThreadRegistryAdd(registry, &config);
}

static bool registerWatchdog(AppThreadRegistry *registry, const AppDependencies *deps)
{
    if (deps->watchdogTaskEntry == NULL)
    {
        return true; /* not applicable on this target (host) - not a failure */
    }

    OsalTaskConfig config = {0};
    config.name = "Watchdog";
    config.entry = deps->watchdogTaskEntry;
    config.context = &s_logger; /* for health-warning logging, see crash_dump.c */
    config.stackSizeBytes = deps->watchdogTaskStackBytes;
    config.priority = kOsalPriorityHigh;

    return appThreadRegistryAdd(registry, &config);
}

bool appRun(const AppDependencies *deps)
{
    loggerInit(&s_logger, deps->logSink, deps->timeSource);
    loggerLog(&s_logger, kLogLevelEvent, "System start");

    AppThreadRegistry registry;
    appThreadRegistryInit(&registry);

    s_storage = deps->storage;

    bool registered = registerBlinky(&registry, deps->led);
    registered = registerUsbHostProcess(&registry, deps) && registered;
    registered = registerWatchdog(&registry, deps) && registered;
    registered = storageDemoRegister(&registry, &s_storage, &s_logger, deps->cfutureSyncOps) && registered;

    if (!registered)
    {
        loggerLog(&s_logger, kLogLevelError, "appRun: thread registration failed");
        return false;
    }

    if (!appThreadRegistryStartAll(&registry))
    {
        loggerLog(&s_logger, kLogLevelError, "appRun: thread start failed");
        return false;
    }

    return true;
}
