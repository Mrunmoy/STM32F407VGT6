#pragma once

#include "core/log_level.h"
#include "pal/pal_log_sink.h"
#include "pal/pal_time.h"

typedef struct Logger
{
    PalLogSink sink;
    PalTimeSource timeSource;
} Logger;

void loggerInit(Logger *self, PalLogSink sink, PalTimeSource timeSource);
void loggerLog(Logger *self, LogLevel level, const char *message);
