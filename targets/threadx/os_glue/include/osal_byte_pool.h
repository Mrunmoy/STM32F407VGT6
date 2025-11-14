#pragma once

#include "tx_api.h"

/* Must be called once, from App_ThreadX_Init() (via board_start_app.c), before
 * any osal_task_create()/osal_queue_create() call - osal.c allocates task
 * stacks and queue storage from this same byte pool (tx_byte_allocate()). */
void osal_byte_pool_init(TX_BYTE_POOL *pool);
