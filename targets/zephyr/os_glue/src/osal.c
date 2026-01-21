#include "osal.h"

#include <zephyr/kernel.h>

#include <stdint.h>

enum
{
    /* This app creates a small, known set of tasks: the USB Host process
     * pump (usb_host.c) plus storage_demo.c's 5 tasks (Servicer + 4
     * Requesters) - 8 leaves headroom, matching the ThreadX target's own
     * osal.c s_taskSlots[8] sizing for the same reason. */
    kOsalMaxTasks = 8U,
};

/* ── Task creation ────────────────────────────────────────────────────── */

typedef struct TaskTrampolineArgs
{
    OsalTaskEntryFn entry;
    void *context;
} TaskTrampolineArgs;

static struct k_thread s_taskSlots[kOsalMaxTasks];
static TaskTrampolineArgs s_taskArgs[kOsalMaxTasks];
static uint32_t s_nextTaskSlot;

static void task_trampoline(void *p1, void *p2, void *p3)
{
    (void)p2;
    (void)p3;

    TaskTrampolineArgs *args = (TaskTrampolineArgs *)p1;
    args->entry(args->context);
}

static int osal_priority_to_zephyr_priority(OsalTaskPriority priority)
{
    /* Zephyr preemptible priorities: lower number = more urgent (opposite
     * of osal.h's own Low < Normal < High ordering, same inversion the
     * ThreadX target's osal.c already documents). */
    switch (priority)
    {
        case kOsalPriorityHigh:
            return 5;
        case kOsalPriorityNormal:
            return 7;
        case kOsalPriorityLow:
        default:
            return 9;
    }
}

bool osal_task_create(const OsalTaskConfig *config, OsalTaskHandle *out_handle)
{
    if ((config == NULL) || (config->entry == NULL) || (s_nextTaskSlot >= kOsalMaxTasks))
    {
        return false;
    }

    k_thread_stack_t *stack = k_thread_stack_alloc(config->stackSizeBytes, 0);
    if (stack == NULL)
    {
        return false;
    }

    uint32_t slot = s_nextTaskSlot++;
    s_taskArgs[slot].entry = config->entry;
    s_taskArgs[slot].context = config->context;

    k_tid_t tid = k_thread_create(&s_taskSlots[slot], stack, config->stackSizeBytes, task_trampoline,
                                   &s_taskArgs[slot], NULL, NULL, osal_priority_to_zephyr_priority(config->priority), 0,
                                   K_NO_WAIT);

    if (tid == NULL)
    {
        return false;
    }

    if (config->name != NULL)
    {
        k_thread_name_set(tid, config->name);
    }

    if (out_handle != NULL)
    {
        *out_handle = (OsalTaskHandle)tid;
    }

    return true;
}

void osal_task_exit(void)
{
    /* Confirmed against the vendored kernel/sched.c: z_thread_halt()'s own
     * comment states "aborting _current will not return, obviously" - self-
     * abort permanently halts the calling thread before this call could ever
     * come back. The stack allocated for this thread in osal_task_create()
     * (k_thread_stack_alloc()) is intentionally not freed here - there is no
     * restart path yet that would reuse it, matching this project's current
     * "tasks are torn down, not yet recreated" scope. */
    k_thread_abort(k_current_get());

    /* Unreachable - kept only so this function's own noreturn contract holds
     * even if that were ever untrue. */
    for (;;)
    {
    }
}

/* ── Queue ────────────────────────────────────────────────────────────── */

bool osal_queue_create(uint32_t itemCount, size_t itemSize, OsalQueueHandle *out_handle)
{
    if ((itemCount == 0U) || (itemSize == 0U) || (out_handle == NULL))
    {
        return false;
    }

    struct k_msgq *msgq = k_malloc(sizeof(struct k_msgq));
    if (msgq == NULL)
    {
        return false;
    }

    if (k_msgq_alloc_init(msgq, itemSize, itemCount) != 0)
    {
        k_free(msgq);
        return false;
    }

    *out_handle = (OsalQueueHandle)msgq;
    return true;
}

static k_timeout_t zephyr_timeout_from_ms(uint32_t timeoutMs)
{
    if (timeoutMs == 0U)
    {
        return K_NO_WAIT;
    }

    if (timeoutMs == UINT32_MAX)
    {
        return K_FOREVER;
    }

    return K_MSEC(timeoutMs);
}

bool osal_queue_send(OsalQueueHandle queue, const void *item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (item == NULL))
    {
        return false;
    }

    return k_msgq_put((struct k_msgq *)queue, item, zephyr_timeout_from_ms(timeoutMs)) == 0;
}

bool osal_queue_receive(OsalQueueHandle queue, void *out_item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (out_item == NULL))
    {
        return false;
    }

    return k_msgq_get((struct k_msgq *)queue, out_item, zephyr_timeout_from_ms(timeoutMs)) == 0;
}

/* ── Mutex ────────────────────────────────────────────────────────────── */

bool osal_mutex_create(OsalMutexHandle *out_handle)
{
    if (out_handle == NULL)
    {
        return false;
    }

    struct k_mutex *mutex = k_malloc(sizeof(struct k_mutex));
    if (mutex == NULL)
    {
        return false;
    }

    if (k_mutex_init(mutex) != 0)
    {
        k_free(mutex);
        return false;
    }

    *out_handle = (OsalMutexHandle)mutex;
    return true;
}

bool osal_mutex_lock(OsalMutexHandle mutex, uint32_t timeoutMs)
{
    return k_mutex_lock((struct k_mutex *)mutex, zephyr_timeout_from_ms(timeoutMs)) == 0;
}

void osal_mutex_unlock(OsalMutexHandle mutex)
{
    (void)k_mutex_unlock((struct k_mutex *)mutex);
}

/* ── Delay / time ─────────────────────────────────────────────────────── */

void osal_delay_ms(uint32_t ms)
{
    k_msleep(ms);
}

uint32_t osal_get_time_ms(void)
{
    return k_uptime_get_32();
}

/* ── Heap ─────────────────────────────────────────────────────────────── */

void *osal_malloc(size_t size)
{
    return k_malloc(size);
}

void osal_free(void *ptr)
{
    k_free(ptr);
}
