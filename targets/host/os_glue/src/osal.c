#define _POSIX_C_SOURCE 200809L

#include "osal.h"

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

/* POSIX/host implementation of app/include/osal.h (pthreads + condition
 * variables), matching build.py's --target host. This is host desktop
 * test tooling, not device firmware: the "no dynamic allocation" rule that
 * governs app/ is a firmware constraint (bounded RAM, no libc heap worth
 * trusting on a microcontroller) that does not apply to a Linux process, so
 * task/queue bookkeeping structs below are malloc'd once at create time.
 * Queues/mutexes are still expected to outlive the process (nothing tears
 * those down); tasks are the exception - a task that calls osal_task_exit()
 * (below) does get its OsalPosixTask struct freed, since a task's own
 * lifecycle is no longer guaranteed to be "forever" (see osal.h).
 *
 * cfuture_sync_ops.c (this same directory) is where this target's
 * cfuture_sync_ops_get() lives - it delegates to libcfuture's own POSIX
 * adapter (external/cfuture's adapters/cfuture_posix.h). */

/* ── Task creation ────────────────────────────────────────────────────── */

/* Floor for pthread_attr_setstacksize(). Deliberately a fixed constant
 * rather than PTHREAD_STACK_MIN: glibc >= 2.34 can define that macro as a
 * sysconf() call gated behind feature-test macros that vary by libc/version,
 * so a plain enum constant here is both simpler and more portable across the
 * host build environments this target actually runs on. 16 KiB matches
 * glibc's own historical static PTHREAD_STACK_MIN value on this arch. */
enum
{
    kOsalPosixMinStackBytes = 16384U,
};

typedef struct OsalPosixTask
{
    pthread_t thread;
    OsalTaskEntryFn entry;
    void *context;
} OsalPosixTask;

/* Lets osal_task_exit() (below), called from deep inside the task's own
 * entry function with no handle in hand, find and free the OsalPosixTask
 * malloc'd for THIS thread by osal_task_create() - pthread's own
 * thread-local storage is the standard way to recover that without every
 * osal.h caller having to thread a handle through. */
static pthread_key_t s_taskKey;
static pthread_once_t s_taskKeyOnce = PTHREAD_ONCE_INIT;

static void osal_posix_make_task_key(void)
{
    (void)pthread_key_create(&s_taskKey, NULL);
}

static void *osal_posix_task_trampoline(void *arg)
{
    OsalPosixTask *task = (OsalPosixTask *)arg;

    (void)pthread_setspecific(s_taskKey, task);

    /* Well-behaved entries break out of their own loop and call
     * osal_task_exit() (osal.h's contract) - osal_task_exit() never returns,
     * so control normally never comes back here. If an entry plainly
     * returned anyway, free the struct here rather than leak it, matching
     * what osal_task_exit() itself would have done. */
    task->entry(task->context);
    free(task);
    return NULL;
}

static int osal_posix_sched_priority(OsalTaskPriority priority)
{
    switch (priority)
    {
        case kOsalPriorityHigh:
            return 20;

        case kOsalPriorityNormal:
            return 10;

        case kOsalPriorityLow:
        default:
            return 1;
    }
}

bool osal_task_create(const OsalTaskConfig *config, OsalTaskHandle *outHandle)
{
    if ((config == NULL) || (config->entry == NULL))
    {
        return false;
    }

    (void)pthread_once(&s_taskKeyOnce, osal_posix_make_task_key);

    OsalPosixTask *task = (OsalPosixTask *)malloc(sizeof(OsalPosixTask));
    if (task == NULL)
    {
        return false;
    }

    task->entry = config->entry;
    task->context = config->context;

    size_t stackSize = (size_t)config->stackSizeBytes;
    if (stackSize < (size_t)kOsalPosixMinStackBytes)
    {
        stackSize = (size_t)kOsalPosixMinStackBytes;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stackSize);

    /* Best-effort priority: reflect the requested OsalTaskPriority ordering
     * via SCHED_RR when the process has permission to use a real-time
     * scheduling policy (CAP_SYS_NICE, or root). A plain host test run
     * usually has neither, so this is allowed to fail silently - task
     * creation must not fail just because priority elevation isn't
     * available; it falls back to the default SCHED_OTHER policy below. */
    struct sched_param schedParam;
    schedParam.sched_priority = osal_posix_sched_priority(config->priority);
    if ((pthread_attr_setschedpolicy(&attr, SCHED_RR) == 0) &&
        (pthread_attr_setschedparam(&attr, &schedParam) == 0))
    {
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    int createResult = pthread_create(&task->thread, &attr, osal_posix_task_trampoline, task);
    pthread_attr_destroy(&attr);

    if (createResult != 0)
    {
        /* pthread_create can reject an explicit SCHED_RR request outright
         * (EPERM) even though the attr calls above succeeded syntactically.
         * Retry once with plain default scheduling before giving up. */
        pthread_attr_t fallbackAttr;
        pthread_attr_init(&fallbackAttr);
        pthread_attr_setstacksize(&fallbackAttr, stackSize);
        createResult = pthread_create(&task->thread, &fallbackAttr, osal_posix_task_trampoline, task);
        pthread_attr_destroy(&fallbackAttr);
    }

    if (createResult != 0)
    {
        free(task);
        return false;
    }

    pthread_detach(task->thread);

    if (outHandle != NULL)
    {
        *outHandle = (OsalTaskHandle)task;
    }

    return true;
}

void osal_task_exit(void)
{
    free(pthread_getspecific(s_taskKey));
    pthread_exit(NULL);

    /* Unreachable - pthread_exit() does not return - kept only so this
     * function's own noreturn contract holds even if that were ever untrue. */
    for (;;)
    {
    }
}

/* ── Queue ────────────────────────────────────────────────────────────── */

typedef struct OsalPosixQueue
{
    pthread_mutex_t mutex;
    pthread_cond_t notEmpty;
    pthread_cond_t notFull;
    uint8_t *buffer;
    size_t itemSize;
    uint32_t capacity;
    uint32_t count;
    uint32_t head; /* next slot osal_queue_receive will read */
    uint32_t tail; /* next slot osal_queue_send will write */
} OsalPosixQueue;

static void osal_posix_deadline(struct timespec *outDeadline, uint32_t timeoutMs)
{
    clock_gettime(CLOCK_MONOTONIC, outDeadline);

    outDeadline->tv_sec += (time_t)(timeoutMs / 1000U);

    long nanos = outDeadline->tv_nsec + (long)(timeoutMs % 1000U) * 1000000L;
    if (nanos >= 1000000000L)
    {
        outDeadline->tv_sec += 1;
        nanos -= 1000000000L;
    }
    outDeadline->tv_nsec = nanos;
}

bool osal_queue_create(uint32_t itemCount, size_t itemSize, OsalQueueHandle *outHandle)
{
    if ((itemCount == 0U) || (itemSize == 0U) || (outHandle == NULL))
    {
        return false;
    }

    OsalPosixQueue *queue = (OsalPosixQueue *)malloc(sizeof(OsalPosixQueue));
    if (queue == NULL)
    {
        return false;
    }

    queue->buffer = (uint8_t *)malloc((size_t)itemCount * itemSize);
    if (queue->buffer == NULL)
    {
        free(queue);
        return false;
    }

    queue->itemSize = itemSize;
    queue->capacity = itemCount;
    queue->count = 0U;
    queue->head = 0U;
    queue->tail = 0U;

    pthread_mutex_init(&queue->mutex, NULL);

    /* CLOCK_MONOTONIC condvars so osal_queue_send/Receive timeouts are immune
     * to wall-clock jumps (NTP steps, manual clock changes). */
    pthread_condattr_t condAttr;
    pthread_condattr_init(&condAttr);
    pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
    pthread_cond_init(&queue->notEmpty, &condAttr);
    pthread_cond_init(&queue->notFull, &condAttr);
    pthread_condattr_destroy(&condAttr);

    *outHandle = (OsalQueueHandle)queue;
    return true;
}

bool osal_queue_send(OsalQueueHandle queue, const void *item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (item == NULL))
    {
        return false;
    }

    OsalPosixQueue *q = (OsalPosixQueue *)queue;

    pthread_mutex_lock(&q->mutex);

    bool haveSpace = (q->count < q->capacity);

    if (!haveSpace && (timeoutMs != 0U))
    {
        if (timeoutMs == UINT32_MAX)
        {
            while (q->count >= q->capacity)
            {
                pthread_cond_wait(&q->notFull, &q->mutex);
            }
            haveSpace = true;
        }
        else
        {
            struct timespec deadline;
            osal_posix_deadline(&deadline, timeoutMs);

            int waitResult = 0;
            while ((q->count >= q->capacity) && (waitResult == 0))
            {
                waitResult = pthread_cond_timedwait(&q->notFull, &q->mutex, &deadline);
            }
            haveSpace = (q->count < q->capacity);
        }
    }

    bool sent = false;
    if (haveSpace)
    {
        memcpy(q->buffer + ((size_t)q->tail * q->itemSize), item, q->itemSize);
        q->tail = (q->tail + 1U) % q->capacity;
        q->count += 1U;
        sent = true;
        pthread_cond_signal(&q->notEmpty);
    }

    pthread_mutex_unlock(&q->mutex);
    return sent;
}

bool osal_queue_receive(OsalQueueHandle queue, void *outItem, uint32_t timeoutMs)
{
    if ((queue == NULL) || (outItem == NULL))
    {
        return false;
    }

    OsalPosixQueue *q = (OsalPosixQueue *)queue;

    pthread_mutex_lock(&q->mutex);

    bool haveItem = (q->count > 0U);

    if (!haveItem && (timeoutMs != 0U))
    {
        if (timeoutMs == UINT32_MAX)
        {
            while (q->count == 0U)
            {
                pthread_cond_wait(&q->notEmpty, &q->mutex);
            }
            haveItem = true;
        }
        else
        {
            struct timespec deadline;
            osal_posix_deadline(&deadline, timeoutMs);

            int waitResult = 0;
            while ((q->count == 0U) && (waitResult == 0))
            {
                waitResult = pthread_cond_timedwait(&q->notEmpty, &q->mutex, &deadline);
            }
            haveItem = (q->count > 0U);
        }
    }

    bool received = false;
    if (haveItem)
    {
        memcpy(outItem, q->buffer + ((size_t)q->head * q->itemSize), q->itemSize);
        q->head = (q->head + 1U) % q->capacity;
        q->count -= 1U;
        received = true;
        pthread_cond_signal(&q->notFull);
    }

    pthread_mutex_unlock(&q->mutex);
    return received;
}

/* ── Mutex ────────────────────────────────────────────────────────────── */

bool osal_mutex_create(OsalMutexHandle *outHandle)
{
    if (outHandle == NULL)
    {
        return false;
    }

    pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (mutex == NULL)
    {
        return false;
    }

    pthread_mutex_init(mutex, NULL);
    *outHandle = (OsalMutexHandle)mutex;
    return true;
}

bool osal_mutex_lock(OsalMutexHandle mutex, uint32_t timeoutMs)
{
    pthread_mutex_t *m = (pthread_mutex_t *)mutex;

    if (timeoutMs == 0U)
    {
        return pthread_mutex_trylock(m) == 0;
    }

    if (timeoutMs == UINT32_MAX)
    {
        return pthread_mutex_lock(m) == 0;
    }

    struct timespec deadline;
    osal_posix_deadline(&deadline, timeoutMs);
    return pthread_mutex_timedlock(m, &deadline) == 0;
}

void osal_mutex_unlock(OsalMutexHandle mutex)
{
    pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

/* ── Delay / time ─────────────────────────────────────────────────────── */

void osal_delay_ms(uint32_t ms)
{
    struct timespec remaining;
    remaining.tv_sec = (time_t)(ms / 1000U);
    remaining.tv_nsec = (long)(ms % 1000U) * 1000000L;

    while (nanosleep(&remaining, &remaining) != 0)
    {
        if (errno != EINTR)
        {
            break;
        }
        /* remaining was updated in place with the time left; loop again. */
    }
}

uint32_t osal_get_time_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return (uint32_t)(((uint64_t)now.tv_sec * 1000ULL) + ((uint64_t)now.tv_nsec / 1000000ULL));
}

/* ── Heap ─────────────────────────────────────────────────────────────── */

void *osal_malloc(size_t size)
{
    return malloc(size);
}

void osal_free(void *ptr)
{
    free(ptr);
}
