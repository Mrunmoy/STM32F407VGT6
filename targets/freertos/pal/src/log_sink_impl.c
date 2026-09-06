#include "log_sink_impl.h"

#include "usart.h"

#include "cmsis_os2.h"

enum
{
    kUartTransmitTimeoutMs = 100U,
};

/* This Logger is shared by the storage-service showcase's 5 concurrent
 * tasks (T_S + 4 client tasks, see storage_demo.c), all calling loggerLog()
 * on their own cadence - HAL_UART_Transmit() has no built-in protection
 * against two threads calling it on the same huart at once. Confirmed as a
 * real, not just theoretical, risk while porting this same design to the
 * sibling ThreadX target: there, two threads racing this exact call tore
 * transmissions mid-byte on real hardware. A mutex serializes callers;
 * blinky_task.c's single-task logging never hit this, which is presumably
 * why it went unnoticed here first. */
static osMutexId_t s_mutex;

static bool log_sink_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;

    if (s_mutex != NULL)
    {
        (void)osMutexAcquire(s_mutex, osWaitForever);
    }

    bool ok = HAL_UART_Transmit(&huart1, data, (uint16_t)length, kUartTransmitTimeoutMs) == HAL_OK;

    if (s_mutex != NULL)
    {
        (void)osMutexRelease(s_mutex);
    }

    return ok;
}

void log_sink_init(LogSink *out_sink)
{
    if (s_mutex == NULL)
    {
        s_mutex = osMutexNew(NULL);
    }

    out_sink->write = log_sink_write;
    out_sink->context = NULL;
}
