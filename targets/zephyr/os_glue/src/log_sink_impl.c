#include "log_sink_impl.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* Shared by the storage showcase's 5 concurrent tasks (see storage_demo.c) -
 * printk() itself is documented thread-safe for interleaving individual
 * calls, but Logger formats one full "[timestamp] [LEVEL] message\r\n" line
 * via a single write() call with the whole buffer, so a mutex still avoids
 * two tasks' lines interleaving mid-line - same reasoning the FreeRTOS/
 * ThreadX targets' log_sink_impl.c documents. */
static struct k_mutex s_mutex;
static bool s_mutex_ready;

static bool log_sink_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;

    if (s_mutex_ready)
    {
        k_mutex_lock(&s_mutex, K_FOREVER);
    }

    for (size_t i = 0; i < length; i++)
    {
        printk("%c", (char)data[i]);
    }

    if (s_mutex_ready)
    {
        k_mutex_unlock(&s_mutex);
    }

    return true;
}

void log_sink_init(LogSink *out_sink)
{
    if (!s_mutex_ready)
    {
        k_mutex_init(&s_mutex);
        s_mutex_ready = true;
    }

    out_sink->write = log_sink_write;
    out_sink->context = NULL;
}
