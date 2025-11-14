#pragma once

#include "tx_api.h"

/* Builds this target's concrete adapters (UART log sink, uptime time
 * source, board LED, USB Host MSC disk) into one AppDependencies and hands
 * off to the shared app/'s appRun() - the ONE place every thread actually
 * gets started (see app.h). Call once from App_ThreadX_Init(), passing the
 * same byte pool it received - osal_byte_pool_init() (osal_byte_pool.h) allocates
 * every task stack from it. */
void board_start_app(TX_BYTE_POOL *bytePool);
