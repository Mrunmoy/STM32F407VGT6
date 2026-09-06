#pragma once

#include <stdbool.h>

#include "time_source.h"

/* Brings up the LSE-clocked RTC and reports real calendar time. Returns false
 * (without touching hardware state further) if the LSE crystal doesn't start
 * or the RTC peripheral fails to init - callers should fall back to another
 * TimeSource, e.g. time_source_init (time_source_impl.h), rather than treat
 * this as fatal. */
bool rtc_time_source_init(TimeSource *out_time_source) __attribute__((warn_unused_result));
