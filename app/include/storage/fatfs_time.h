#pragma once

#include "pal/pal_time.h"

/* Call once, after the composition root has resolved its TimeSource (RTC, or
 * uptime fallback), so FatFS's get_fattime() has something to read. Safe to
 * call again if the source changes at runtime (e.g. RTC becomes available
 * later). */
void fatfsTimeBind(PalTimeSource source);
