#include "logger.h"

#include <stdint.h>
#include <stdio.h>

enum
{
    kLoggerMaxLineLength = 96U,
};

static const char *logLevelName(LogLevel level)
{
    switch (level)
    {
        case kLogLevelError:
            return "ERROR";
        case kLogLevelEvent:
        default:
            return "EVENT";
    }
}

void loggerInit(Logger *self, PalLogSink sink, PalTimeSource timeSource)
{
    self->sink = sink;
    self->timeSource = timeSource;
}

void loggerLog(Logger *self, LogLevel level, const char *message)
{
    DateTime time = {0};
    (void)self->timeSource.get(self->timeSource.context, &time);

    char line[kLoggerMaxLineLength];
    int written;

    if (time.hasDate)
    {
        written = snprintf(line, sizeof(line), "[%04u-%02u-%02u %02u:%02u:%02u] [%s] %s\r\n",
                            (unsigned int)time.year, (unsigned int)time.month, (unsigned int)time.day,
                            (unsigned int)time.hours, (unsigned int)time.minutes, (unsigned int)time.seconds,
                            logLevelName(level), message);
    }
    else
    {
        written = snprintf(line, sizeof(line), "[up %02u:%02u:%02u] [%s] %s\r\n",
                            (unsigned int)time.hours, (unsigned int)time.minutes, (unsigned int)time.seconds,
                            logLevelName(level), message);
    }

    if (written <= 0)
    {
        return;
    }

    size_t length = ((size_t)written < sizeof(line)) ? (size_t)written : (sizeof(line) - 1U);
    (void)self->sink.write(self->sink.context, (const uint8_t *)line, length);
}
