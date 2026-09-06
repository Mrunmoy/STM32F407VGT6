#include "osal.h"

#include "osal_byte_pool.h"

#include <stdlib.h>
#include <string.h>

/* ThreadX/osal.h implementation notes:
 *
 * - Priorities are inverted vs FreeRTOS (0 = highest urgency in ThreadX).
 *   kOsalPriorityHigh/Normal/Low map to fixed ThreadX priority numbers below,
 *   chosen to sit alongside app_threadx.c's existing heartbeat_thread(10)/
 *   app_thread(5) without colliding.
 *
 * - tx_thread_create() takes a plain ULONG "initial parameter", not a
 *   void* context, and its entry function is void(*)(ULONG), not osal.h's
 *   void(*)(void*) - task_trampoline()/TaskTrampolineArgs bridge that.
 *
 * - TX_QUEUE messages are capped at 16 ULONGs (64 bytes) - far smaller than
 *   StorageRequest (~532 bytes, see storage_protocol.h). Queues here are
 *   NOT a thin wrapper over tx_queue_create(): each osal_queue_create()
 *   allocates its own itemCount*itemSize byte-pool buffer plus TWO native
 *   ThreadX queues - one ("free") pre-filled with slot indices 0..itemCount-1
 *   acting as a thread-safe free-list, one ("filled") carrying the slot
 *   index of whichever item is actually queued. osal_queue_send() acquires a
 *   free slot, memcpy()s the caller's item into that slot's real storage,
 *   then posts the slot index to "filled"; osal_queue_receive() is the mirror.
 *   This keeps the "queue owns a real copy" contract storage_service.c/
 *   client_tasks.c depend on (see osal.h's own doc comment - the whole
 *   point of this project is never handing a pointer to a caller's
 *   stack-local struct across a task boundary) instead of just sending a
 *   pointer, which ThreadX's small message size would otherwise tempt. */

enum
{
    kOsalPriorityHighValue = 8U,
    kOsalPriorityNormalValue = 12U,
    kOsalPriorityLowValue = 16U,

    kMaxOsalTasks = 8U,
    kMaxOsalQueues = 4U,
    kMaxOsalMutexes = 4U,
};

static TX_BYTE_POOL *s_pool;

/* ── Task creation ────────────────────────────────────────────────────── */

typedef struct TaskTrampolineArgs
{
    OsalTaskEntryFn entry;
    void *context;
} TaskTrampolineArgs;

static TX_THREAD s_taskSlots[kMaxOsalTasks];
static uint32_t s_taskSlotsUsed;

static UINT osal_priority_to_threadx_priority(OsalTaskPriority priority)
{
    UINT result = kOsalPriorityNormalValue;

    switch (priority)
    {
        case kOsalPriorityHigh:
            result = kOsalPriorityHighValue;
            break;

        case kOsalPriorityLow:
            result = kOsalPriorityLowValue;
            break;

        case kOsalPriorityNormal:
        default:
            result = kOsalPriorityNormalValue;
            break;
    }

    return result;
}

static void task_trampoline(ULONG arg)
{
    TaskTrampolineArgs *args = (TaskTrampolineArgs *)arg;
    args->entry(args->context);
}

bool osal_task_create(const OsalTaskConfig *config, OsalTaskHandle *out_handle)
{
    if ((config == NULL) || (config->entry == NULL) || (s_taskSlotsUsed >= kMaxOsalTasks))
    {
        return false;
    }

    CHAR *stackPointer = NULL;
    if (tx_byte_allocate(s_pool, (VOID **)&stackPointer, config->stackSizeBytes, TX_NO_WAIT) != TX_SUCCESS)
    {
        return false;
    }

    TaskTrampolineArgs *trampolineArgs = NULL;
    if (tx_byte_allocate(s_pool, (VOID **)&trampolineArgs, sizeof(TaskTrampolineArgs), TX_NO_WAIT) != TX_SUCCESS)
    {
        (void)tx_byte_release(stackPointer);
        return false;
    }
    trampolineArgs->entry = config->entry;
    trampolineArgs->context = config->context;

    TX_THREAD *thread = &s_taskSlots[s_taskSlotsUsed];
    UINT priority = osal_priority_to_threadx_priority(config->priority);

    if (tx_thread_create(thread, (CHAR *)config->name, task_trampoline, (ULONG)trampolineArgs, stackPointer,
                          config->stackSizeBytes, priority, priority, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
    {
        (void)tx_byte_release(trampolineArgs);
        (void)tx_byte_release(stackPointer);
        return false;
    }

    s_taskSlotsUsed++;

    if (out_handle != NULL)
    {
        *out_handle = (OsalTaskHandle)thread;
    }

    return true;
}

void osal_task_exit(void)
{
    /* Confirmed against the vendored tx_thread_terminate.c: terminating the
     * CALLING thread (TX_READY state) sets it TX_TERMINATED and suspends it
     * via _tx_thread_system_suspend() - a terminated thread is never
     * rescheduled, so this call does not return in practice. (tx_thread_
     * delete() cannot be used instead here - confirmed against tx_thread_
     * delete.c, it rejects any thread not already TX_COMPLETED/TX_TERMINATED,
     * so a thread cannot delete itself in one step while still running.) The
     * stack/trampoline-args byte-pool allocations made for this thread in
     * osal_task_create() are intentionally not released here - there is no
     * restart path yet that would reuse them, matching this project's
     * current "tasks are torn down, not yet recreated" scope. */
    (void)tx_thread_terminate(tx_thread_identify());

    /* Unreachable - kept only so this function's own noreturn contract holds
     * even if that were ever untrue. */
    for (;;)
    {
    }
}

/* ── Queue ────────────────────────────────────────────────────────────── */

typedef struct OsalQueueImpl
{
    TX_QUEUE filled;
    TX_QUEUE free;
    uint8_t *storage;
    size_t itemSize;
} OsalQueueImpl;

static OsalQueueImpl s_queueSlots[kMaxOsalQueues];
static uint32_t s_queueSlotsUsed;

static ULONG ms_to_ticks(uint32_t timeoutMs)
{
    ULONG ticks = TX_WAIT_FOREVER;

    if (timeoutMs == 0U)
    {
        ticks = TX_NO_WAIT;
    }
    else if (timeoutMs != UINT32_MAX)
    {
        /* Round up so a caller never gets LESS than it asked for at this
         * platform's 10ms tick granularity (TX_TIMER_TICKS_PER_SECOND). */
        ticks = (ULONG)((timeoutMs + (1000U / TX_TIMER_TICKS_PER_SECOND) - 1U) / (1000U / TX_TIMER_TICKS_PER_SECOND));
        if (ticks == 0U)
        {
            ticks = 1U;
        }
    }

    return ticks;
}

bool osal_queue_create(uint32_t itemCount, size_t itemSize, OsalQueueHandle *out_handle)
{
    if ((itemCount == 0U) || (itemSize == 0U) || (out_handle == NULL) || (s_queueSlotsUsed >= kMaxOsalQueues))
    {
        return false;
    }

    OsalQueueImpl *impl = &s_queueSlots[s_queueSlotsUsed];

    if (tx_byte_allocate(s_pool, (VOID **)&impl->storage, (ULONG)(itemCount * itemSize), TX_NO_WAIT) != TX_SUCCESS)
    {
        return false;
    }
    impl->itemSize = itemSize;

    ULONG *filledBuffer = NULL;
    if (tx_byte_allocate(s_pool, (VOID **)&filledBuffer, itemCount * sizeof(ULONG), TX_NO_WAIT) != TX_SUCCESS)
    {
        (void)tx_byte_release(impl->storage);
        return false;
    }
    if (tx_queue_create(&impl->filled, "OsalQueueFilled", TX_1_ULONG, filledBuffer, itemCount * sizeof(ULONG)) !=
        TX_SUCCESS)
    {
        (void)tx_byte_release(filledBuffer);
        (void)tx_byte_release(impl->storage);
        return false;
    }

    ULONG *freeBuffer = NULL;
    if (tx_byte_allocate(s_pool, (VOID **)&freeBuffer, itemCount * sizeof(ULONG), TX_NO_WAIT) != TX_SUCCESS)
    {
        (void)tx_queue_delete(&impl->filled);
        (void)tx_byte_release(filledBuffer);
        (void)tx_byte_release(impl->storage);
        return false;
    }
    if (tx_queue_create(&impl->free, "OsalQueueFree", TX_1_ULONG, freeBuffer, itemCount * sizeof(ULONG)) !=
        TX_SUCCESS)
    {
        (void)tx_byte_release(freeBuffer);
        (void)tx_queue_delete(&impl->filled);
        (void)tx_byte_release(filledBuffer);
        (void)tx_byte_release(impl->storage);
        return false;
    }

    /* Pre-fill the free-slot pool with every slot index - this doubles as
     * the queue's capacity limiter (osal_queue_send can never claim more
     * slots than itemCount). */
    for (uint32_t i = 0U; i < itemCount; i++)
    {
        ULONG slot = i;
        if (tx_queue_send(&impl->free, &slot, TX_NO_WAIT) != TX_SUCCESS)
        {
            (void)tx_queue_delete(&impl->free);
            (void)tx_byte_release(freeBuffer);
            (void)tx_queue_delete(&impl->filled);
            (void)tx_byte_release(filledBuffer);
            (void)tx_byte_release(impl->storage);
            return false;
        }
    }

    s_queueSlotsUsed++;
    *out_handle = (OsalQueueHandle)impl;
    return true;
}

bool osal_queue_send(OsalQueueHandle queue, const void *item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (item == NULL))
    {
        return false;
    }

    OsalQueueImpl *impl = (OsalQueueImpl *)queue;

    ULONG slot;
    if (tx_queue_receive(&impl->free, &slot, ms_to_ticks(timeoutMs)) != TX_SUCCESS)
    {
        return false;
    }

    (void)memcpy(impl->storage + (slot * impl->itemSize), item, impl->itemSize);

    /* A free slot was just claimed 1:1 against this queue's capacity, so
     * there is always room here - TX_NO_WAIT, never expected to fail. */
    if (tx_queue_send(&impl->filled, &slot, TX_NO_WAIT) != TX_SUCCESS)
    {
        (void)tx_queue_send(&impl->free, &slot, TX_NO_WAIT);
        return false;
    }

    return true;
}

bool osal_queue_receive(OsalQueueHandle queue, void *out_item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (out_item == NULL))
    {
        return false;
    }

    OsalQueueImpl *impl = (OsalQueueImpl *)queue;

    ULONG slot;
    if (tx_queue_receive(&impl->filled, &slot, ms_to_ticks(timeoutMs)) != TX_SUCCESS)
    {
        return false;
    }

    (void)memcpy(out_item, impl->storage + (slot * impl->itemSize), impl->itemSize);

    (void)tx_queue_send(&impl->free, &slot, TX_NO_WAIT);

    return true;
}

/* ── Mutex ────────────────────────────────────────────────────────────── */

static TX_MUTEX s_mutexSlots[kMaxOsalMutexes];
static uint32_t s_mutexSlotsUsed;

bool osal_mutex_create(OsalMutexHandle *out_handle)
{
    if ((out_handle == NULL) || (s_mutexSlotsUsed >= kMaxOsalMutexes))
    {
        return false;
    }

    TX_MUTEX *mutex = &s_mutexSlots[s_mutexSlotsUsed];
    if (tx_mutex_create(mutex, "OsalMutex", TX_NO_INHERIT) != TX_SUCCESS)
    {
        return false;
    }

    s_mutexSlotsUsed++;
    *out_handle = (OsalMutexHandle)mutex;
    return true;
}

bool osal_mutex_lock(OsalMutexHandle mutex, uint32_t timeoutMs)
{
    return tx_mutex_get((TX_MUTEX *)mutex, ms_to_ticks(timeoutMs)) == TX_SUCCESS;
}

void osal_mutex_unlock(OsalMutexHandle mutex)
{
    (void)tx_mutex_put((TX_MUTEX *)mutex);
}

/* ── Delay / time ─────────────────────────────────────────────────────── */

void osal_delay_ms(uint32_t ms)
{
    (void)tx_thread_sleep(ms_to_ticks(ms));
}

uint32_t osal_get_time_ms(void)
{
    return (uint32_t)(tx_time_get() * (1000U / TX_TIMER_TICKS_PER_SECOND));
}

/* ── Init ─────────────────────────────────────────────────────────────── */

void osal_byte_pool_init(TX_BYTE_POOL *pool)
{
    s_pool = pool;
}

/* ── Heap ─────────────────────────────────────────────────────────────── */

/* newlib's malloc()/free() are already thread-safe under ThreadX
 * (Core/ThreadSafe/, STM32_THREAD_SAFE_STRATEGY=2), so plain malloc/free -
 * not the TX_BYTE_POOL above - are used directly here, same tolerance this
 * project already gives vendor middleware elsewhere. */
void *osal_malloc(size_t size)
{
    return malloc(size);
}

void osal_free(void *ptr)
{
    free(ptr);
}
