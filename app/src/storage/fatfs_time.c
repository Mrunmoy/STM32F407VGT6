#include "fatfs_time.h"

#include "ff.h" /* DWORD, declares get_fattime() */

#include <stddef.h>

/* Fixed fallback epoch used whenever no TimeSource has been bound yet, the
 * bound source has no calendar knowledge (hasDate == false, e.g. uptime-only
 * fallback), or a read fails - 2026-01-01 00:00:00, matches this project's
 * build era. */
enum
{
    kFallbackYear = 2026U,
    kFallbackMonth = 1U,
    kFallbackDay = 1U,
};

/* Stored by value, matching this project's existing TimeSource convention
 * (see Logger in logger.h/app.c) - callers hold TimeSource instances as
 * plain locals/statics and pass them by value, never by pointer to a
 * stack-local instance that could go out of scope. */
static PalTimeSource s_timeSource;

void fatfsTimeBind(PalTimeSource source)
{
    s_timeSource = source;
}

static DWORD packFatTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    return ((DWORD)(year - 1980U) << 25)
         | ((DWORD)month << 21)
         | ((DWORD)day << 16)
         | ((DWORD)hours << 11)
         | ((DWORD)minutes << 5)
         | ((DWORD)(seconds / 2U));
}

DWORD get_fattime(void)
{
    DateTime now = {0};

    if ((s_timeSource.get != NULL) && s_timeSource.get(s_timeSource.context, &now) && now.hasDate)
    {
        return packFatTime(now.year, now.month, now.day, now.hours, now.minutes, now.seconds);
    }

    return packFatTime(kFallbackYear, kFallbackMonth, kFallbackDay, 0U, 0U, 0U);
}
