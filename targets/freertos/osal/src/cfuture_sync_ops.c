#include "cfuture_sync_ops.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>

/* Structural template: janus's src/adapters/cfuture_posix.c (statically
 * pools CFUTURE_POSIX_MAX_EVENTS pthread mutex/cond pairs, hands out
 * pointers into that pool from event_create(), never touches heap). This
 * file does the same thing with FreeRTOS binary semaphores.
 *
 * A cfuture_pool_t calls event_create() once per slot at cfuture_pool_init()
 * time (not per-request) and event_destroy() once per slot at
 * cfuture_pool_destroy() time - so the number of events actually consumed at
 * any moment equals the sum of every live cfuture_pool_t's capacity, not the
 * request rate. storage_protocol.h's StorageRequest/StorageResult are ~520
 * bytes each, so this project's pools are expected to stay in the single
 * digits of capacity (per the libcfuture API notes) - kEventPoolSize leaves
 * headroom for more than one such pool without exhausting the static array. */
enum
{
    kEventPoolSize = 16U,
};

typedef struct Event
{
    StaticSemaphore_t storage;
    SemaphoreHandle_t handle;
    bool inUse;
} Event;

static Event s_events[kEventPoolSize];
static bool s_eventPoolInitialized = false;

/* Lazily zeroes the pool's bookkeeping on first use. Only touches plain
 * bools/pointers (no FreeRTOS API calls) so it is safe to run inside the
 * same critical section that claims a slot. */
static void ensure_pool_initialized(void)
{
    if (!s_eventPoolInitialized)
    {
        for (uint32_t i = 0U; i < kEventPoolSize; ++i)
        {
            s_events[i].handle = NULL;
            s_events[i].inUse = false;
        }

        s_eventPoolInitialized = true;
    }
}

static Event *claim_slot(void)
{
    Event *claimed = NULL;

    taskENTER_CRITICAL();

    ensure_pool_initialized();

    for (uint32_t i = 0U; i < kEventPoolSize; ++i)
    {
        if (!s_events[i].inUse)
        {
            s_events[i].inUse = true;
            claimed = &s_events[i];
            break;
        }
    }

    taskEXIT_CRITICAL();

    return claimed;
}

static void release_slot(Event *event)
{
    taskENTER_CRITICAL();
    event->inUse = false;
    taskEXIT_CRITICAL();
}

static void *event_create(void)
{
    Event *event = claim_slot();
    if (event == NULL)
    {
        return NULL;
    }

    /* Created "empty" (must be given before it can be taken) - equivalent to
     * the POSIX adapter's freshly-created signaled = false state. Called
     * outside the critical section above: no other caller can observe or
     * reclaim this slot before event->inUse is set, and
     * xSemaphoreCreateBinaryStatic() must not run with interrupts masked. */
    event->handle = xSemaphoreCreateBinaryStatic(&event->storage);
    if (event->handle == NULL)
    {
        release_slot(event);
        return NULL;
    }

    return event;
}

static void event_destroy(void *eventHandle)
{
    if (eventHandle == NULL)
    {
        return;
    }

    Event *event = (Event *)eventHandle;

    if (event->handle != NULL)
    {
        vSemaphoreDelete(event->handle);
        event->handle = NULL;
    }

    release_slot(event);
}

static void event_set(void *eventHandle)
{
    if (eventHandle == NULL)
    {
        return;
    }

    Event *event = (Event *)eventHandle;
    (void)xSemaphoreGive(event->handle);
}

static bool event_wait(void *eventHandle, uint32_t timeoutMs)
{
    if (eventHandle == NULL)
    {
        return false;
    }

    Event *event = (Event *)eventHandle;

    /* UINT32_MAX is libcfuture's/osal.h's "block forever" sentinel; FreeRTOS
     * has its own native infinite-wait sentinel (portMAX_DELAY) rather than
     * needing the POSIX adapter's defensive 10-minute cap, so pass it
     * straight through. Any other value converts 1:1 (configTICK_RATE_HZ is
     * fixed at 1000 in this project, see FreeRTOSConfig.h). */
    TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

    return (xSemaphoreTake(event->handle, ticks) == pdTRUE);
}

static void event_reset(void *eventHandle)
{
    if (eventHandle == NULL)
    {
        return;
    }

    Event *event = (Event *)eventHandle;

    /* Non-blocking drain of any stale "already signaled" state left over
     * from this slot's previous occupant before cfuture_create() hands it to
     * a new caller - a binary semaphore otherwise has no separate "reset"
     * operation. */
    (void)xSemaphoreTake(event->handle, 0U);
}

static void event_set_from_isr(void *eventHandle)
{
    if (eventHandle == NULL)
    {
        return;
    }

    Event *event = (Event *)eventHandle;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    (void)xSemaphoreGiveFromISR(event->handle, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

static const cfuture_sync_ops_t s_ops = {
    .event_create = event_create,
    .event_destroy = event_destroy,
    .event_set = event_set,
    .event_wait = event_wait,
    .event_reset = event_reset,
    .event_set_from_isr = event_set_from_isr,
};

const cfuture_sync_ops_t *cfuture_sync_ops_get(void)
{
    return &s_ops;
}
