#pragma once

#include "log_level.h"
#include "log_sink.h"
#include "time_source.h"

typedef struct Logger
{
    LogSink sink;
    TimeSource timeSource;
} Logger;

void loggerInit(Logger *self, LogSink sink, TimeSource timeSource);
void loggerLog(Logger *self, LogLevel level, const char *message);
