#pragma once

#include "log_sink.h"

/* Concrete LogSink for this target - blocking HAL_UART_Transmit() on
 * huart1 (usart.c), mutex-serialized since this Logger is shared by the
 * storage showcase's 5 concurrent tasks. Named log_sink_impl.h (not
 * log_sink.h) because the shared interface it implements already owns that
 * exact filename (app/include/log_sink.h) - same name would be a
 * self-include collision, not just a readability problem. */
void log_sink_init(LogSink *out_sink);
