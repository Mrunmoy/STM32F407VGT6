#pragma once

#include "log_sink.h"

/* Concrete LogSink over Zephyr's own console/printk plumbing (already bound
 * to USART1 via zephyr,console in the board overlay) - unlike the
 * FreeRTOS/ThreadX targets' log_sink_impl.c, this does NOT call
 * HAL_UART_Transmit() directly: Zephyr's own uart_stm32 driver already owns
 * USART1 (it's a real devicetree-modeled peripheral there, unlike OTG_FS),
 * so driving it via raw HAL underneath would race Zephyr's own driver state.
 * printk() is used instead - serialized the same way (a mutex around the
 * call), since this Logger is shared by the storage showcase's 5 concurrent
 * tasks exactly as on the other two ports. Named log_sink_impl.h (not
 * log_sink.h) because the shared interface it implements already owns that
 * exact filename (app/include/log_sink.h) - same name would be a
 * self-include collision, not just a readability problem. */
void log_sink_init(LogSink *out_sink);
