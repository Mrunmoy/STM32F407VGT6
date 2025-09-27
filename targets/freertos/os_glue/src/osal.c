#include "osal.h"

#include "cmsis_os.h"

/* FreeRTOS/CMSIS-OS2 implementation of osal.h. This is the only osal.h
 * implementation linked into this target. Matches this project's existing
 * CMSIS-OS2 usage elsewhere in this target exactly (osThreadNew, osDelay,
 * osKernelGetTickCount). */

/* ── Priority mapping ─────────────────────────────────────────────────── */

static osPriority_t osal_priority_to_os_priority(OsalTaskPriority priority)
{
    osPriority_t result = osPriorityNormal;

    switch (priority)
    {
        case kOsalPriorityLow:
            result = osPriorityLow;
            break;

        case kOsalPriorityHigh:
            result = osPriorityHigh;
            break;

        case kOsalPriorityNormal:
        default:
            result = osPriorityNormal;
            break;
    }

    return result;
}

/* ── Timeout mapping ──────────────────────────────────────────────────── */

/* osal.h's timeoutMs and CMSIS-OS2's tick-based timeouts are numerically
 * interchangeable here because FreeRTOSConfig.h fixes configTICK_RATE_HZ at
 * 1000 (1 tick == 1 ms); only the UINT32_MAX "block forever" sentinel needs
 * translating to CMSIS-OS2's own osWaitForever sentinel. */
static uint32_t osal_timeout_to_os_timeout(uint32_t timeoutMs)
{
    return (timeoutMs == UINT32_MAX) ? osWaitForever : timeoutMs;
}

/* ── Task creation ────────────────────────────────────────────────────── */

bool osal_task_create(const OsalTaskConfig *config, OsalTaskHandle *out_handle)
{
    if ((config == NULL) || (config->entry == NULL))
    {
        return false;
    }

    const osThreadAttr_t attributes = {
        .name = config->name,
        .stack_size = config->stackSizeBytes,
        .priority = osal_priority_to_os_priority(config->priority),
    };

    /* OsalTaskEntryFn (void (*)(void *context)) matches osThreadFunc_t
     * (void (*)(void *argument)) exactly, per osal.h's own doc comment - the
     * cast below is a formality to satisfy the nominal type difference, not
     * a behavior change. */
    osThreadId_t handle = osThreadNew((osThreadFunc_t)config->entry, config->context, &attributes);
    if (handle == NULL)
    {
        return false;
    }

    if (out_handle != NULL)
    {
        *out_handle = (OsalTaskHandle)handle;
    }

    return true;
}

void osal_task_exit(void)
{
    /* CMSIS-OS2, declared __NO_RETURN in cmsis_os2.h - terminates execution
     * of the calling thread. */
    osThreadExit();

    /* Unreachable - osThreadExit() does not return - kept only so this
     * function's own noreturn contract holds even if that were ever untrue. */
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

    osMessageQueueId_t queue = osMessageQueueNew(itemCount, (uint32_t)itemSize, NULL);
    if (queue == NULL)
    {
        return false;
    }

    *out_handle = (OsalQueueHandle)queue;
    return true;
}

bool osal_queue_send(OsalQueueHandle queue, const void *item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (item == NULL))
    {
        return false;
    }

    osStatus_t status = osMessageQueuePut((osMessageQueueId_t)queue, item, 0U, osal_timeout_to_os_timeout(timeoutMs));
    return (status == osOK);
}

bool osal_queue_receive(OsalQueueHandle queue, void *out_item, uint32_t timeoutMs)
{
    if ((queue == NULL) || (out_item == NULL))
    {
        return false;
    }

    osStatus_t status = osMessageQueueGet((osMessageQueueId_t)queue, out_item, NULL, osal_timeout_to_os_timeout(timeoutMs));
    return (status == osOK);
}

/* ── Mutex ────────────────────────────────────────────────────────────── */

bool osal_mutex_create(OsalMutexHandle *out_handle)
{
    if (out_handle == NULL)
    {
        return false;
    }

    osMutexId_t mutex = osMutexNew(NULL);
    if (mutex == NULL)
    {
        return false;
    }

    *out_handle = (OsalMutexHandle)mutex;
    return true;
}

bool osal_mutex_lock(OsalMutexHandle mutex, uint32_t timeoutMs)
{
    return osMutexAcquire((osMutexId_t)mutex, osal_timeout_to_os_timeout(timeoutMs)) == osOK;
}

void osal_mutex_unlock(OsalMutexHandle mutex)
{
    (void)osMutexRelease((osMutexId_t)mutex);
}

/* ── Delay / time ─────────────────────────────────────────────────────── */

void osal_delay_ms(uint32_t ms)
{
    (void)osDelay(ms);
}

uint32_t osal_get_time_ms(void)
{
    /* 1 tick == 1 ms, see osal_timeout_to_os_timeout() above. */
    return osKernelGetTickCount();
}

/* ── Heap ─────────────────────────────────────────────────────────────── */

void *osal_malloc(size_t size)
{
    return pvPortMalloc(size);
}

void osal_free(void *ptr)
{
    vPortFree(ptr);
}
