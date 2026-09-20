#include "stm32f4xx_hal.h"
#include "tx_api.h"

/* Overrides the HAL's weak busy-wait HAL_Delay(). The USB host stack (USBH_Delay(), and the
 * HAL's own port reset) calls it for ~310 ms per attach from a High-priority task; as a
 * busy-wait that starved every lower-priority task for the whole time. Sleep instead whenever
 * a thread is asking; keep the busy-wait before the kernel starts and in interrupt context. */
void HAL_Delay(uint32_t Delay)
{
    if ((tx_thread_identify() != TX_NULL) && (__get_IPSR() == 0U))
    {
        /* Round up to whole kernel ticks, +1 because a sleep of N ticks can end almost one
         * tick early. */
        const ULONG ticks = (((ULONG)Delay * TX_TIMER_TICKS_PER_SECOND) + 999UL) / 1000UL;
        (void)tx_thread_sleep(ticks + 1UL);
        return;
    }

    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) <= Delay)
    {
    }
}
