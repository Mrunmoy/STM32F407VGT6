#include "cfuture_selftest_irq.h"

#include "FreeRTOSConfig.h"
#include "stm32f4xx.h"

#include <stddef.h>

static CfutureSelfTestIsrHandler s_handler;
static void *s_context;

void cfuture_selftest_raise_irq(CfutureSelfTestIsrHandler handler, void *context)
{
    s_handler = handler;
    s_context = context;

    /* The handler calls a FreeRTOS FromISR API, so it must not outrank the syscall ceiling. */
    NVIC_SetPriority(CAN2_SCE_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1U);
    NVIC_EnableIRQ(CAN2_SCE_IRQn);
    NVIC_SetPendingIRQ(CAN2_SCE_IRQn);
}

void CAN2_SCE_IRQHandler(void)
{
    if (s_handler != NULL)
    {
        s_handler(s_context);
    }
}
