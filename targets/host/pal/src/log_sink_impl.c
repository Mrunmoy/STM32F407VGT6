#include "log_sink_impl.h"

#include <stdio.h>

static bool log_sink_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    return fwrite(data, 1U, length, stdout) == length;
}

void log_sink_init(LogSink *out_sink)
{
    out_sink->write = log_sink_write;
    out_sink->context = NULL;
}
