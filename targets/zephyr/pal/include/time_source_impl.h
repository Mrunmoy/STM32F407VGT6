#pragma once

#include "time_source.h"

/* Concrete TimeSource using k_uptime_get() - no RTC configured on this
 * board target (hasDate=false, elapsed HH:MM:SS since boot only). Named
 * time_source_impl.h (not time_source.h) - see log_sink_impl.h's doc
 * comment for why: the shared interface already owns that exact filename. */
void time_source_init(TimeSource *out_time_source);
