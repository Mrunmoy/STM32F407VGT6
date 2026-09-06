#pragma once

/* Builds this target's concrete adapters (UART log sink, RTC/uptime time
 * source, board LED, USB Host MSC disk) into one AppDependencies and hands
 * off to the shared app/'s appRun() - the ONE place every thread actually
 * gets started (see app.h). Call once from MX_FREERTOS_Init(), before
 * osKernelStart(). */
void board_start_app(void);
