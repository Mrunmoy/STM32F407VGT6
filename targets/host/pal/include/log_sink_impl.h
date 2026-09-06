#pragma once

#include "log_sink.h"

/* Concrete LogSink for this target - stdout, for the host/POSIX build.
 * Named log_sink_impl.h (not log_sink.h) because the shared interface it
 * implements already owns that exact filename (app/include/log_sink.h) -
 * same name would be a self-include collision, not just a readability
 * problem. Every target's own concrete LogSink lives at this same path,
 * pal/include/log_sink_impl.h - only the shared interface header
 * (log_sink.h) is genuinely one file for every target. */
void log_sink_init(LogSink *out_sink);
