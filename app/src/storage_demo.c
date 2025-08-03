#include "storage_demo.h"

#include "client_tasks.h"
#include "osal.h"
#include "storage_protocol.h"
#include "storage_service.h"

#include "fatfs_diskio.h"

enum
{
    /* All four Requester tasks share one cadence with no explicit stagger,
     * so their scenarios can momentarily overlap: worst case (scenario2 and
     * scenario4 both mid-flight) needs up to ~7 queue items/pool slots live
     * at once. Sized with headroom above that rather than the single-digit
     * minimum, so cross-scenario contention never shows up as a spurious
     * "pool exhausted"/"send failed" FAIL log - kEventPoolSize
     * (cfuture_osal_freertos.c) is 16, so kStoragePoolCapacity still leaves
     * plenty of room. */
    kStorageQueueDepth = 8U,
    kStoragePoolCapacity = 12U,

    /* Original 4096/3072 estimates were never measured against real hardware
     * and turned out too tight: scenario 4 (ABA isolation) holds a stale
     * StorageResult plus a second StorageRequest/StorageResult pair (~530B
     * each) live simultaneously across several nested calls
     * (occupyServicer/cfuture_wait_for/osal_queue_send each add their own
     * frame), and vApplicationStackOverflowHook() was an empty no-op, so an
     * overflow silently corrupted adjacent heap memory (traced on real
     * hardware via JLink to a FreeRTOS queue control block getting a garbage
     * itemSize, causing prvCopyDataFromQueue's memcpy to run off the end of
     * SRAM) instead of being caught. Doubled with margin; the hook now also
     * actually traps instead of silently continuing - see freertos.c. */
    kStorageServiceStackBytes = 8192U,
    kClientTaskStackBytes = 6144U,
};

static OsalQueueHandle s_storageQueue;

static cfuture_pool_t s_storagePool;
CFUTURE_DEFINE_STATIC_BUFFERS(s_storagePool, StorageResult, kStoragePoolCapacity);

static StorageServiceConfig s_storageServiceConfig;

/* One config instance shared by all four Requester tasks - see
 * client_tasks.h's ClientTaskConfig doc comment for why that's safe. */
static ClientTaskConfig s_clientTaskConfig;

static bool registerTask(AppThreadRegistry *registry, const char *name, OsalTaskEntryFn entry, void *context,
                          uint32_t stackSizeBytes, OsalTaskPriority priority)
{
    OsalTaskConfig config = {0};
    config.name = name;
    config.entry = entry;
    config.context = context;
    config.stackSizeBytes = stackSizeBytes;
    config.priority = priority;

    return appThreadRegistryAdd(registry, &config);
}

bool storageDemoRegister(AppThreadRegistry *registry, PalStorage *storage, Logger *logger,
                          const cfuture_sync_ops_t *syncOps)
{
    fatfsDiskioBind(storage);

    if (!osal_queue_create(kStorageQueueDepth, sizeof(StorageRequest), &s_storageQueue))
    {
        loggerLog(logger, kLogLevelError, "storageDemo: queue create failed");
        return false;
    }

    if (!cfuture_pool_init(&s_storagePool, kStoragePoolCapacity, sizeof(StorageResult), s_storagePool_slots,
                            s_storagePool_payload, syncOps))
    {
        loggerLog(logger, kLogLevelError, "storageDemo: promise pool init failed");
        return false;
    }

    s_storageServiceConfig.queue = s_storageQueue;
    s_storageServiceConfig.logger = logger;

    s_clientTaskConfig.queue = s_storageQueue;
    s_clientTaskConfig.pool = &s_storagePool;
    s_clientTaskConfig.logger = logger;

    bool registered = registerTask(registry, "StorageSvc", storageServiceTaskEntry, &s_storageServiceConfig,
                                    kStorageServiceStackBytes, kOsalPriorityHigh);
    registered = registerTask(registry, "StorageDemoHappy", clientTaskHappyPathEntry, &s_clientTaskConfig,
                               kClientTaskStackBytes, kOsalPriorityNormal) && registered;
    registered = registerTask(registry, "StorageDemoQTimeout", clientTaskQueueTimeoutEntry, &s_clientTaskConfig,
                               kClientTaskStackBytes, kOsalPriorityNormal) && registered;
    registered = registerTask(registry, "StorageDemoLateDone", clientTaskLateCompletionEntry, &s_clientTaskConfig,
                               kClientTaskStackBytes, kOsalPriorityNormal) && registered;
    registered = registerTask(registry, "StorageDemoAbaIso", clientTaskAbaIsolationEntry, &s_clientTaskConfig,
                               kClientTaskStackBytes, kOsalPriorityNormal) && registered;

    if (!registered)
    {
        loggerLog(logger, kLogLevelError, "storageDemo: task registration failed");
        return false;
    }

    loggerLog(logger, kLogLevelEvent, "storageDemo: registered");
    return true;
}
