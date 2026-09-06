#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Calendar date/time as reported by a TimeSource. hasDate is false when the
 * source has no calendar knowledge (e.g. uptime-only) - year/month/day are
 * meaningless in that case and callers must not print them. */
typedef struct DateTime
{
    bool hasDate;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} DateTime;

typedef bool (*TimeSourceGetFn)(void *context, DateTime *outTime);

/* Abstract time source (dependency inversion): Logger depends on this, never
 * on a concrete RTC or tick-count implementation. */
typedef struct PalTimeSource
{
    TimeSourceGetFn get;
    void *context;
} PalTimeSource;

typedef PalTimeSource TimeSource;
