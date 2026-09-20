#include "diagnostics/cfuture_selftest.h"

#include "diagnostics/app_task_trace.h"
#include "cfuture_pal.h"
#include "osal/osal.h"

#include <stdint.h>
#include <stdio.h>

enum
{
    kSelfTestPoolCapacity = 2U,
    kSelfTestQueueDepth = 2U,
    kSelfTestStackBytes = 3072U,
    kSelfTestPauseMs = 2000U,
    kSelfTestPoison = 0xDEADDEADU,
};

typedef enum SelfTestProducerMode
{
    kSelfTestProduceFromTask = 0,
    kSelfTestProduceFromIsr = 1,
} SelfTestProducerMode;

typedef struct SelfTestRequest
{
    cpromise_t promise;
    uint32_t delayMs;
    uint32_t value;
    SelfTestProducerMode mode;
} SelfTestRequest;

typedef struct SelfTestIsrContext
{
    cpromise_t promise;
    uint32_t value;
} SelfTestIsrContext;

static const char *const kWaiterTaskName = "CfstWaiter";
static const char *const kProducerTaskName = "CfstProducer";

static cfuture_pool_t s_pool;
CFUTURE_DEFINE_STATIC_BUFFERS(s_pool, uint32_t, kSelfTestPoolCapacity);

static OsalQueueHandle s_queue;
static Logger *s_logger;
static CfutureSelfTestRaiseIrqFn s_raiseIrq;
static SelfTestIsrContext s_isrContext;
static const cfuture_sync_ops_t *s_syncOps;

/* When the producer actually resolved the promise (task or ISR). Lets the delivery checks
 * measure the library's wake-up latency separately from how late the producer itself ran. */
static volatile uint32_t s_resolvedAtMs;

static void report(const char *test, bool passed, const char *detail, uint32_t a, uint32_t b)
{
    char message[112];
    (void)snprintf(message, sizeof(message), "cfst %s %s: %s (%lu, %lu)", test, passed ? "PASS" : "FAIL", detail,
                   (unsigned long)a, (unsigned long)b);
    loggerLog(s_logger, passed ? kLogLevelEvent : kLogLevelError, message);
}

/* Runs in real interrupt context on targets that provide raiseIrq. */
static void isrFulfil(void *context)
{
    SelfTestIsrContext *isr = (SelfTestIsrContext *)context;
    s_resolvedAtMs = osal_get_time_ms();
    cpromise_set_value_from_isr(&isr->promise, &isr->value, 0);
}

static void producerTaskEntry(void *context)
{
    (void)context;

    for (;;)
    {
        appTaskTraceLoopStart(kProducerTaskName);
        if (appTaskTraceShouldStop(kProducerTaskName))
        {
            break;
        }

        SelfTestRequest request;
        if (osal_queue_receive(s_queue, &request, 500U))
        {
            osal_delay_ms(request.delayMs);

            if (request.mode == kSelfTestProduceFromIsr && s_raiseIrq != NULL)
            {
                s_isrContext.promise = request.promise;
                s_isrContext.value = request.value;
                s_raiseIrq(isrFulfil, &s_isrContext);
            }
            else
            {
                s_resolvedAtMs = osal_get_time_ms();
                cpromise_set_value(&request.promise, &request.value, 0);
            }
        }

        appTaskTraceLoopEnd(kProducerTaskName);
    }

    appTaskTraceMarkStopped(kProducerTaskName);
    osal_task_exit();
}

/* Controls: the same interval measured without the library in the way. They exist to expose
 * gross scheduler starvation (hundreds of ms), so they tolerate the same 50 ms of ordinary
 * scheduling slack as the timeout checks. */
static void checkControls(void)
{
    uint32_t start = osal_get_time_ms();
    uint32_t palStart = cfuture_pal_time_ms();
    osal_delay_ms(20U);
    uint32_t elapsed = osal_get_time_ms() - start;
    uint32_t palElapsed = cfuture_pal_time_ms() - palStart;
    report("ctrl-delay20", elapsed <= 70U, "osal elapsed ms, PAL elapsed ms", elapsed, palElapsed);

    if (s_syncOps != NULL && s_syncOps->event_create != NULL && s_syncOps->event_wait != NULL)
    {
        void *event = s_syncOps->event_create();
        if (event != NULL)
        {
            start = osal_get_time_ms();
            palStart = cfuture_pal_time_ms();
            const bool signalled = s_syncOps->event_wait(event, 20U);
            elapsed = osal_get_time_ms() - start;
            palElapsed = cfuture_pal_time_ms() - palStart;
            report("ctrl-rawwait20", !signalled && elapsed <= 70U, "osal elapsed ms, PAL elapsed ms", elapsed,
                   palElapsed);
            if (s_syncOps->event_destroy != NULL)
            {
                s_syncOps->event_destroy(event);
            }
        }
    }

}

static void checkTimeoutAccuracy(uint32_t timeoutMs)
{
    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(&s_pool, &promise, &future))
    {
        report("timeout", false, "pool exhausted", timeoutMs, 0U);
        return;
    }

    int32_t status = 0;
    const uint32_t start = osal_get_time_ms();
    const bool ok = cfuture_wait_for(&future, timeoutMs, NULL, &status);
    const uint32_t elapsed = osal_get_time_ms() - start;
    cpromise_drop(&promise, CFUTURE_ERR_DROPPED);

    /* Never early; late by at most scheduling slack, never multiplied. */
    const bool passed = !ok && status == CFUTURE_ERR_TIMEOUT && elapsed >= timeoutMs && elapsed <= timeoutMs + 50U;
    report("timeout", passed, "requested ms, elapsed ms", timeoutMs, elapsed);
}

static void checkDelivery(const char *test, SelfTestProducerMode mode, uint32_t delayMs, uint32_t waitMs)
{
    if (mode == kSelfTestProduceFromIsr && s_raiseIrq == NULL)
    {
        loggerLog(s_logger, kLogLevelEvent, "cfst isr SKIP: target provides no test interrupt");
        return;
    }

    SelfTestRequest request = {0};
    cfuture_t future;
    if (!cfuture_create(&s_pool, &request.promise, &future))
    {
        report(test, false, "pool exhausted", 0U, 0U);
        return;
    }

    request.delayMs = delayMs;
    request.value = 0xC0FFEE00U + delayMs;
    request.mode = mode;

    /* Timestamp before the hand-off: the higher-priority producer starts its delay the moment
     * the request is queued, which can be before this task runs again. */
    const uint32_t start = osal_get_time_ms();
    if (!osal_queue_send(s_queue, &request, 0U))
    {
        const bool cancelled = cfuture_cancel(&request.promise, &future);
        report(test, false, "queue full, cancelled", (uint32_t)cancelled, 0U);
        return;
    }

    uint32_t value = 0U;
    int32_t status = -1;
    const bool ok = cfuture_wait_for(&future, waitMs, &value, &status);
    const uint32_t now = osal_get_time_ms();
    const uint32_t elapsed = now - start;
    const uint32_t wakeLatency = now - s_resolvedAtMs;

    /* The value must not arrive before the producer's delay, and the waiter must wake promptly
     * once the producer resolved. How late the producer itself ran is scheduling, not libcfuture. */
    const bool passed = ok && status == 0 && value == request.value && (elapsed + 1U) >= delayMs && wakeLatency <= 20U;
    report(test, passed, "elapsed ms, wake latency ms after resolve", elapsed, wakeLatency);
}

static void checkStaleHandle(void)
{
    cpromise_t promise;
    cfuture_t future;
    if (!cfuture_create(&s_pool, &promise, &future))
    {
        report("stale", false, "pool exhausted", 0U, 0U);
        return;
    }

    cpromise_t stale = promise;
    const uint8_t slot = promise.slot_id;
    uint32_t value = 1U;
    cpromise_set_value(&promise, &value, 0);
    (void)cfuture_wait_for(&future, 0U, &value, NULL);

    cpromise_t nextPromise;
    cfuture_t nextFuture;
    if (!cfuture_create(&s_pool, &nextPromise, &nextFuture))
    {
        report("stale", false, "pool exhausted on reuse", 0U, 0U);
        return;
    }

    const bool sameSlot = nextPromise.slot_id == slot;
    const bool staleInactive = !cpromise_is_active(&stale);
    uint32_t poison = kSelfTestPoison;
    cpromise_set_value(&stale, &poison, -1);

    const bool stillActive = cpromise_is_active(&nextPromise);
    uint32_t real = 42U;
    cpromise_set_value(&nextPromise, &real, 0);

    uint32_t out = 0U;
    const bool ok = cfuture_wait_for(&nextFuture, 0U, &out, NULL);

    report("stale", sameSlot && staleInactive && stillActive && ok && out == 42U,
           "slot reused, value seen by next occupant", (uint32_t)sameSlot, out);
}

static void checkCancelAndLeaks(void)
{
    cpromise_t promise;
    cfuture_t future;
    bool ok = cfuture_create(&s_pool, &promise, &future);
    ok = ok && cfuture_cancel(&promise, &future);
    ok = ok && promise.pool == NULL && future.pool == NULL;

    /* Every slot must be claimable again: nothing leaked by any check above. */
    cpromise_t promises[kSelfTestPoolCapacity];
    cfuture_t futures[kSelfTestPoolCapacity];
    uint32_t claimed = 0U;
    for (uint32_t i = 0U; i < kSelfTestPoolCapacity; ++i)
    {
        if (cfuture_create(&s_pool, &promises[i], &futures[i]))
        {
            ++claimed;
        }
    }
    for (uint32_t i = 0U; i < claimed; ++i)
    {
        ok = cfuture_cancel(&promises[i], &futures[i]) && ok;
    }

    report("cancel", ok && claimed == kSelfTestPoolCapacity, "slots claimable after all checks, capacity", claimed,
           kSelfTestPoolCapacity);
}

static void waiterTaskEntry(void *context)
{
    (void)context;
    uint32_t round = 0U;

    for (;;)
    {
        appTaskTraceLoopStart(kWaiterTaskName);
        if (appTaskTraceShouldStop(kWaiterTaskName))
        {
            break;
        }

        ++round;
        report("round", true, "starting battery", round, 0U);

        checkControls();
        checkTimeoutAccuracy(20U);
        checkTimeoutAccuracy(200U);
        checkTimeoutAccuracy(1000U);
        checkDelivery("task", kSelfTestProduceFromTask, 50U, 2000U);
        checkDelivery("forever", kSelfTestProduceFromTask, 100U, UINT32_MAX);
        checkDelivery("isr", kSelfTestProduceFromIsr, 30U, 2000U);
        checkStaleHandle();
        checkCancelAndLeaks();

        appTaskTraceLoopEnd(kWaiterTaskName);
        osal_delay_ms(kSelfTestPauseMs);
    }

    appTaskTraceMarkStopped(kWaiterTaskName);
    osal_task_exit();
}

static bool registerTask(AppThreadRegistry *registry, const char *name, OsalTaskEntryFn entry,
                         OsalTaskPriority priority)
{
    OsalTaskConfig config = {0};
    config.name = name;
    config.entry = entry;
    config.context = NULL;
    config.stackSizeBytes = kSelfTestStackBytes;
    config.priority = priority;

    return appThreadRegistryAdd(registry, &config);
}

bool cfutureSelfTestRegister(AppThreadRegistry *registry, Logger *logger, const cfuture_sync_ops_t *syncOps,
                             CfutureSelfTestRaiseIrqFn raiseIrq)
{
    s_logger = logger;
    s_raiseIrq = raiseIrq;
    s_syncOps = syncOps;

    if (!osal_queue_create(kSelfTestQueueDepth, sizeof(SelfTestRequest), &s_queue))
    {
        loggerLog(logger, kLogLevelError, "cfst: queue create failed");
        return false;
    }

    if (!cfuture_pool_init(&s_pool, kSelfTestPoolCapacity, sizeof(uint32_t), s_pool_slots, s_pool_payload, syncOps))
    {
        loggerLog(logger, kLogLevelError, "cfst: pool init failed");
        return false;
    }

    /* Producer above the waiter so a resolved promise is signalled while the waiter blocks. */
    bool registered = registerTask(registry, kProducerTaskName, producerTaskEntry, kOsalPriorityHigh);
    registered = registerTask(registry, kWaiterTaskName, waiterTaskEntry, kOsalPriorityNormal) && registered;
    return registered;
}
