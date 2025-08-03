#pragma once

#include "time_source.h"

/* Concrete TimeSource for this target - elapsed time via osal.h's
 * osal_get_time_ms(), no calendar date (host/POSIX build). Named
 * time_source_impl.h (not time_source.h) - see log_sink_impl.h's doc
 * comment for why: the shared interface already owns that exact filename. */
void time_source_init(TimeSource *out_time_source);
