#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*LogSinkWriteFn)(void *context, const uint8_t *data, size_t length);

/* Abstract log destination (dependency inversion): Logger depends on this,
 * never on a concrete UART/USB/etc. transport. */
typedef struct LogSink
{
    LogSinkWriteFn write;
    void *context;
} LogSink;
