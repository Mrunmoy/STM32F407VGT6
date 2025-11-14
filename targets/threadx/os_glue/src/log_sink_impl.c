#include "log_sink_impl.h"

#include "usart.h"

#include "tx_api.h"

enum
{
    kUartTransmitTimeoutMs = 100U,
};

/* Diverges from the sibling FreeRTOS target's copy of this file: this
 * Logger is shared by the storage-service showcase's 5 concurrent tasks
 * (T_S + 4 client tasks), all calling loggerLog() on their own cadence -
 * HAL_UART_Transmit() has no built-in protection against two threads
 * calling it on the same huart at once, and on real hardware that raced and
 * tore transmissions mid-byte (confirmed via JLink: readable but scrambled
 * UART output). A TX_MUTEX serializes callers instead. */
static TX_MUTEX s_mutex;
static bool s_mutex_ready;

static bool log_sink_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;

    if (s_mutex_ready)
    {
        (void)tx_mutex_get(&s_mutex, TX_WAIT_FOREVER);
    }

    bool ok = HAL_UART_Transmit(&huart1, data, (uint16_t)length, kUartTransmitTimeoutMs) == HAL_OK;

    if (s_mutex_ready)
    {
        (void)tx_mutex_put(&s_mutex);
    }

    return ok;
}

void log_sink_init(LogSink *out_sink)
{
    if (!s_mutex_ready)
    {
        s_mutex_ready = (tx_mutex_create(&s_mutex, "LogSinkMutex", TX_NO_INHERIT) == TX_SUCCESS);
    }

    out_sink->write = log_sink_write;
    out_sink->context = NULL;
}
