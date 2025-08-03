#include "client_tasks.h"

#include "app_task_trace.h"
#include "storage_protocol.h"
#include "storage_service.h"

#include <stdio.h>
#include <string.h>

enum
{
    kScenario1BlockId = 1U,
    kScenario2BlockId = 2U,
    kScenario2CanaryValue = 0x11U,
    kScenario2AttemptedValue = 0x22U,
    kScenario4BlockIdA = 41U,
    kScenario4BlockIdB = 42U,

    kClientTaskCadenceMs = 3000U,
    kClientTaskQueueSendTimeoutMs = 200U,
    kClientTaskHappyWaitTimeoutMs = 500U,
    kClientTaskPollTimeoutMs = 0U,       /* cfuture_wait_for's non-blocking-poll convention */
    kClientTaskDequeueSettleMs = 50U,    /* well under kStorageDemoSlowDelayMs's 200ms */
    kClientTaskDropSettleMs = 50U,

    /* All 4 client tasks are started together (app_threads.c's flat
     * registry has no ordering guarantee beyond registration order) and,
     * on a real OS with true thread parallelism (host's pthreads on a
     * multi-core desktop - never observed on the single-core Cortex-M4
     * targets, which can only ever run one of these at a time), their
     * very first iterations can genuinely race on the shared queue before
     * they desync via kClientTaskCadenceMs. Scenario 4 (ABA isolation) is
     * the one whose timing assumptions - "T_S is still busy with my own
     * occupy request" - break if another scenario's concurrent first
     * request lands on the queue in between. Staggering each scenario's
     * very first attempt is enough: after that, differing per-scenario
     * delays already keep them naturally desynced (confirmed - every
     * iteration after the first one passes reliably on both host and
     * hardware). */
    kClientTaskStartStaggerMs = 100U,

    /* occupyServicer()'s caller drains that request once the scenario is
     * done with it. T_S is expected to finish it (plus, for scenarios 3/4,
     * discover and discard whatever was queued behind it) well within this
     * window - it used to wait forever (UINT32_MAX) here, which is exactly
     * backwards for a task under supervision: if T_S is genuinely stuck,
     * this call site would hang forever right along with it instead of
     * reporting anything. Sized generously above T_S's own real worst case
     * (USBH_MSC_Read/Write's ~10s internal bound, see usb_host.c, plus the
     * ~1s idle-poll granularity storage_service.c now uses, plus margin). */
    kClientTaskOccupyDrainTimeoutMs = 20000U,

    kScenarioLogMessageLength = 64U,
};

/* ── Small shared helpers ─────────────────────────────────────────────── */

/* Lets a caller (currently only host/main.c) tell whether any scenario has
 * ever failed, instead of the pass/fail result only ever existing as a log
 * line a human has to read - see clientTasksFailureCount()'s own doc
 * comment. A plain (non-atomic) counter is fine here: this is a diagnostic
 * "did anything fail" signal checked once at the end of a run, not a value
 * anything's correctness depends on - the rare lost increment from two
 * tasks' truly-concurrent writes racing on host would not turn a real
 * failure into an unnoticed zero, only under-count by one. */
static volatile uint32_t s_scenarioFailureCount;

static void logScenarioResult(Logger *logger, const char *scenarioName, bool pass, const char *detail)
{
    char message[kScenarioLogMessageLength];
    (void)snprintf(message, sizeof(message), "%s %s: %s", scenarioName, pass ? "PASS" : "FAIL", detail);
    loggerLog(logger, pass ? kLogLevelEvent : kLogLevelError, message);

    if (!pass)
    {
        s_scenarioFailureCount++;
    }
}

uint32_t clientTasksFailureCount(void)
{
    return s_scenarioFailureCount;
}

/* Synchronous write used by scenarios that just need a known block seeded
 * (e.g. scenario 2's canary) - not itself part of what a scenario proves. */
static bool writeBlockSync(const ClientTaskConfig *config, uint32_t blockId, uint8_t value)
{
    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(config->pool, &promise, &future))
    {
        return false;
    }

    StorageRequest request = {0};
    request.command = kStorageCommandWrite;
    request.blockId = blockId;
    request.length = kStorageBlockSize;
    (void)memset(request.writeData, (int)value, kStorageBlockSize);
    request.promise = promise;

    if (!osal_queue_send(config->queue, &request, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&future);
        return false;
    }

    StorageResult result;
    int32_t errorCode = kStorageErrorOk;
    bool completed = cfuture_wait_for(&future, kClientTaskHappyWaitTimeoutMs, &result, &errorCode);
    return completed && (errorCode == kStorageErrorOk);
}

static bool readBlockByteSync(const ClientTaskConfig *config, uint32_t blockId, uint8_t *outValue)
{
    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(config->pool, &promise, &future))
    {
        return false;
    }

    StorageRequest request = {0};
    request.command = kStorageCommandRead;
    request.blockId = blockId;
    request.promise = promise;

    if (!osal_queue_send(config->queue, &request, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&future);
        return false;
    }

    StorageResult result;
    int32_t errorCode = kStorageErrorOk;
    bool completed = cfuture_wait_for(&future, kClientTaskHappyWaitTimeoutMs, &result, &errorCode);
    if (!completed || (errorCode != kStorageErrorOk) || (result.length == 0U))
    {
        return false;
    }

    *outValue = result.data[0];
    return true;
}

/* Pushes a cheap GetStatus request tagged with storage_service.h's
 * kStorageDemoSlowBlockId, so T_S is guaranteed busy for
 * kStorageDemoSlowDelayMs after it dequeues it - used by scenarios 2 and 4
 * to make "still sitting in the queue, undequeued" deterministic instead of
 * a real-scheduling race. Returns false (nothing was queued, nothing to
 * drain) if the pool/queue couldn't accept it. */
static bool occupyServicer(const ClientTaskConfig *config, cfuture_t *outOccupyFuture)
{
    cpromise_t occupyPromise;
    if (!cfuture_create(config->pool, &occupyPromise, outOccupyFuture))
    {
        return false;
    }

    StorageRequest occupyRequest = {0};
    occupyRequest.command = kStorageCommandGetStatus;
    occupyRequest.blockId = (uint32_t)kStorageDemoSlowBlockId;
    occupyRequest.promise = occupyPromise;

    if (!osal_queue_send(config->queue, &occupyRequest, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(outOccupyFuture);
        return false;
    }

    osal_delay_ms(kClientTaskDequeueSettleMs);
    return true;
}

/* Drains the "occupy" request already sent via occupyServicer() so its slot
 * recycles, once a scenario is done using it to keep T_S busy. Bounded, not
 * infinite - see kClientTaskOccupyDrainTimeoutMs. A timeout here is a real,
 * logged signal that T_S itself may be unhealthy, not something to silently
 * wait out or ignore (the old (void)cfuture_wait_for(..., UINT32_MAX, ...)
 * did both). */
static void drainOccupyFuture(const ClientTaskConfig *config, const char *taskName, cfuture_t *occupyFuture)
{
    appTaskTraceCheckpoint(taskName, "draining occupy request");

    int32_t errorCode = kStorageErrorOk;
    bool completed = cfuture_wait_for(occupyFuture, (uint32_t)kClientTaskOccupyDrainTimeoutMs, NULL, &errorCode);
    if (!completed)
    {
        appTaskTraceCheckpoint(taskName, "T_S did not respond draining occupy request");
        loggerLog(config->logger, kLogLevelError, "T_S did not respond within timeout - possible stall");
    }
}

/* ── Scenario 1: happy path ──────────────────────────────────────────── */

static void runHappyPathScenario(const ClientTaskConfig *config)
{
    uint8_t pattern[kStorageBlockSize];
    (void)memset(pattern, 0xA5, sizeof(pattern));

    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(config->pool, &promise, &future))
    {
        logScenarioResult(config->logger, "scenario1", false, "pool exhausted");
        return;
    }

    StorageRequest writeRequest = {0};
    writeRequest.command = kStorageCommandWrite;
    writeRequest.blockId = kScenario1BlockId;
    writeRequest.length = kStorageBlockSize;
    (void)memcpy(writeRequest.writeData, pattern, sizeof(pattern));
    writeRequest.promise = promise;

    if (!osal_queue_send(config->queue, &writeRequest, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&future);
        logScenarioResult(config->logger, "scenario1", false, "write send failed");
        return;
    }

    StorageResult writeResult;
    int32_t writeError = kStorageErrorOk;
    bool writeOk = cfuture_wait_for(&future, kClientTaskHappyWaitTimeoutMs, &writeResult, &writeError);
    if (!writeOk || (writeError != kStorageErrorOk))
    {
        logScenarioResult(config->logger, "scenario1", false, "write did not complete");
        return;
    }

    if (!cfuture_create(config->pool, &promise, &future))
    {
        logScenarioResult(config->logger, "scenario1", false, "pool exhausted on read");
        return;
    }

    StorageRequest readRequest = {0};
    readRequest.command = kStorageCommandRead;
    readRequest.blockId = kScenario1BlockId;
    readRequest.promise = promise;

    if (!osal_queue_send(config->queue, &readRequest, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&future);
        logScenarioResult(config->logger, "scenario1", false, "read send failed");
        return;
    }

    StorageResult readResult;
    int32_t readError = kStorageErrorOk;
    bool readOk = cfuture_wait_for(&future, kClientTaskHappyWaitTimeoutMs, &readResult, &readError);

    bool pass = readOk && (readError == kStorageErrorOk) && (readResult.length == kStorageBlockSize) &&
                (memcmp(readResult.data, pattern, kStorageBlockSize) == 0);

    logScenarioResult(config->logger, "scenario1", pass,
                       pass ? "write/read roundtrip verified" : "readback mismatch");
}

void clientTaskHappyPathEntry(void *context)
{
    static const char kTaskName[] = "StorageDemoHappy";

    const ClientTaskConfig *config = (const ClientTaskConfig *)context;

    osal_delay_ms(0U * kClientTaskStartStaggerMs);
    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);
        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }
        runHappyPathScenario(config);
        appTaskTraceLoopEnd(kTaskName);
        osal_delay_ms(kClientTaskCadenceMs);
    }

    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}

/* ── Scenario 2: queue-timeout cancellation ──────────────────────────── */

static void runQueueTimeoutScenario(const ClientTaskConfig *config, const char *taskName)
{
    if (!writeBlockSync(config, kScenario2BlockId, kScenario2CanaryValue))
    {
        logScenarioResult(config->logger, "scenario2", false, "canary seed failed");
        return;
    }

    cfuture_t occupyFuture;
    if (!occupyServicer(config, &occupyFuture))
    {
        logScenarioResult(config->logger, "scenario2", false, "could not occupy T_S");
        return;
    }

    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(config->pool, &promise, &future))
    {
        logScenarioResult(config->logger, "scenario2", false, "pool exhausted");
        drainOccupyFuture(config, taskName, &occupyFuture);
        return;
    }

    StorageRequest request = {0};
    request.command = kStorageCommandWrite;
    request.blockId = kScenario2BlockId;
    request.length = kStorageBlockSize;
    (void)memset(request.writeData, kScenario2AttemptedValue, kStorageBlockSize);
    request.promise = promise;

    if (!osal_queue_send(config->queue, &request, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&future);
        logScenarioResult(config->logger, "scenario2", false, "send failed");
        drainOccupyFuture(config, taskName, &occupyFuture);
        return;
    }

    /* T_S is still occupied, so this request cannot have been dequeued yet -
     * a 0ms poll must observe it still PENDING and time out immediately. */
    StorageResult result;
    (void)memset(&result, 0x55, sizeof(result));
    int32_t errorCode = kStorageErrorOk;
    bool completed = cfuture_wait_for(&future, kClientTaskPollTimeoutMs, &result, &errorCode);

    bool payloadUntouched = (result.data[0] == 0x55U);
    bool timedOutAsExpected = (!completed) && (errorCode == CFUTURE_ERR_TIMEOUT);

    /* Let T_S finish the occupy request and then discover/discard the
     * now-timed-out write on its own before we check storage content. */
    drainOccupyFuture(config, taskName, &occupyFuture);
    osal_delay_ms(kClientTaskDropSettleMs);

    uint8_t readBack = 0U;
    bool blockUnchanged = readBlockByteSync(config, kScenario2BlockId, &readBack) &&
                           (readBack == kScenario2CanaryValue);

    bool pass = timedOutAsExpected && payloadUntouched && blockUnchanged;
    logScenarioResult(config->logger, "scenario2", pass,
                       pass ? "write timed out before T_S dequeued it, block unchanged" : "unexpected result");
}

void clientTaskQueueTimeoutEntry(void *context)
{
    static const char kTaskName[] = "StorageDemoQTimeout";

    const ClientTaskConfig *config = (const ClientTaskConfig *)context;

    osal_delay_ms(1U * kClientTaskStartStaggerMs);
    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);
        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }
        runQueueTimeoutScenario(config, kTaskName);
        appTaskTraceLoopEnd(kTaskName);
        osal_delay_ms(kClientTaskCadenceMs);
    }

    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}

/* ── Scenario 3: late-completion discard ─────────────────────────────── */

static void runLateCompletionScenario(const ClientTaskConfig *config)
{
    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(config->pool, &promise, &future))
    {
        logScenarioResult(config->logger, "scenario3", false, "pool exhausted");
        return;
    }

    StorageRequest request = {0};
    request.command = kStorageCommandWrite;
    request.blockId = (uint32_t)kStorageDemoSlowBlockId;
    request.length = kStorageBlockSize;
    (void)memset(request.writeData, 0x33, kStorageBlockSize);
    request.promise = promise;

    if (!osal_queue_send(config->queue, &request, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&future);
        logScenarioResult(config->logger, "scenario3", false, "send failed");
        return;
    }

    /* Give T_S time to dequeue this request, see the promise is still
     * active, and commit to its kStorageDemoSlowDelayMs artificial delay -
     * i.e. to have "already started the operation" - before we give up on
     * it below. kClientTaskDequeueSettleMs is well under that delay, so T_S
     * is guaranteed still mid-flight, not yet finished, at that point. */
    osal_delay_ms(kClientTaskDequeueSettleMs);

    StorageResult result;
    (void)memset(&result, 0x55, sizeof(result));
    int32_t errorCode = kStorageErrorOk;
    bool completed = cfuture_wait_for(&future, kClientTaskPollTimeoutMs, &result, &errorCode);

    bool payloadUntouched = (result.data[0] == 0x55U);
    bool timedOutAsExpected = (!completed) && (errorCode == CFUTURE_ERR_TIMEOUT);

    /* Let T_S's artificial delay finish and discover the now-timed-out
     * promise on its own (cpromise_set_value silently skips the payload
     * copy for a slot already >= TIMEOUT) - nothing further to wait on;
     * our own ref was already released by cfuture_wait_for above. */
    osal_delay_ms(kStorageDemoSlowDelayMs + kClientTaskDropSettleMs);

    bool pass = timedOutAsExpected && payloadUntouched;
    logScenarioResult(config->logger, "scenario3", pass,
                       pass ? "late completion safely discarded" : "unexpected result");
}

void clientTaskLateCompletionEntry(void *context)
{
    static const char kTaskName[] = "StorageDemoLateDone";

    const ClientTaskConfig *config = (const ClientTaskConfig *)context;

    osal_delay_ms(2U * kClientTaskStartStaggerMs);
    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);
        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }
        runLateCompletionScenario(config);
        appTaskTraceLoopEnd(kTaskName);
        osal_delay_ms(kClientTaskCadenceMs);
    }

    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}

/* ── Scenario 4: ABA slot isolation ──────────────────────────────────── */

static void runAbaIsolationScenario(const ClientTaskConfig *config, const char *taskName)
{
    cfuture_t occupyFuture;
    if (!occupyServicer(config, &occupyFuture))
    {
        logScenarioResult(config->logger, "scenario4", false, "could not occupy T_S");
        return;
    }

    cpromise_t stalePromise;
    cfuture_t staleFuture;
    if (!cfuture_create(config->pool, &stalePromise, &staleFuture))
    {
        logScenarioResult(config->logger, "scenario4", false, "pool exhausted (stale)");
        drainOccupyFuture(config, taskName, &occupyFuture);
        return;
    }
    uint8_t staleSlotId = staleFuture.slot_id;

    StorageRequest staleRequest = {0};
    staleRequest.command = kStorageCommandGetStatus;
    staleRequest.blockId = kScenario4BlockIdA;
    staleRequest.promise = stalePromise;

    if (!osal_queue_send(config->queue, &staleRequest, kClientTaskQueueSendTimeoutMs))
    {
        cfuture_abandon(&staleFuture);
        logScenarioResult(config->logger, "scenario4", false, "stale send failed");
        drainOccupyFuture(config, taskName, &occupyFuture);
        return;
    }

    /* T_S is still occupied, so staleRequest cannot have been dequeued yet:
     * a 0ms poll times it out while its slot is still allocated - "still
     * in-flight" from the pool allocator's point of view. */
    int32_t staleError = kStorageErrorOk;
    bool staleCompleted = cfuture_wait_for(&staleFuture, kClientTaskPollTimeoutMs, NULL, &staleError);
    bool staleTimedOut = (!staleCompleted) && (staleError == CFUTURE_ERR_TIMEOUT);

    /* A second, concurrent claim must land on a different slot and be able
     * to run to completion fully independently of the still-draining first
     * one - this is the actual ABA-isolation property being demonstrated. */
    cpromise_t secondPromise;
    cfuture_t secondFuture;
    bool secondClaimed = cfuture_create(config->pool, &secondPromise, &secondFuture);
    bool distinctSlot = secondClaimed && (secondFuture.slot_id != staleSlotId);

    bool secondWriteOk = false;
    if (secondClaimed)
    {
        StorageRequest secondRequest = {0};
        secondRequest.command = kStorageCommandWrite;
        secondRequest.blockId = kScenario4BlockIdB;
        secondRequest.length = kStorageBlockSize;
        (void)memset(secondRequest.writeData, 0x44, kStorageBlockSize);
        secondRequest.promise = secondPromise;

        if (osal_queue_send(config->queue, &secondRequest, kClientTaskQueueSendTimeoutMs))
        {
            StorageResult secondResult;
            int32_t secondError = kStorageErrorOk;
            secondWriteOk = cfuture_wait_for(&secondFuture, kClientTaskHappyWaitTimeoutMs, &secondResult, &secondError) &&
                            (secondError == kStorageErrorOk);
        }
        else
        {
            cfuture_abandon(&secondFuture);
        }
    }

    /* Drain the occupy request (and, behind it, the now-stale request T_S
     * will find already timed out) so their slots recycle instead of
     * leaking - this task loops forever. */
    drainOccupyFuture(config, taskName, &occupyFuture);
    osal_delay_ms(kClientTaskDropSettleMs);

    bool pass = staleTimedOut && distinctSlot && secondClaimed && secondWriteOk;
    logScenarioResult(config->logger, "scenario4", pass,
                       pass ? "second requester got an isolated slot" : "unexpected result");
}

void clientTaskAbaIsolationEntry(void *context)
{
    static const char kTaskName[] = "StorageDemoAbaIso";

    const ClientTaskConfig *config = (const ClientTaskConfig *)context;

    osal_delay_ms(3U * kClientTaskStartStaggerMs);
    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);
        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }
        runAbaIsolationScenario(config, kTaskName);
        appTaskTraceLoopEnd(kTaskName);
        osal_delay_ms(kClientTaskCadenceMs);
    }

    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}
