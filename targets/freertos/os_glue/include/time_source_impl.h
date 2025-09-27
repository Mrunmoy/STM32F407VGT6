#pragma once

#include "time_source.h"

/* Always succeeds: reports elapsed time since scheduler start, no calendar
 * date. Named time_source_impl.h (not time_source.h) - see
 * log_sink_impl.h's doc comment for why: the shared interface already owns
 * that exact filename. */
void time_source_init(TimeSource *out_time_source);
