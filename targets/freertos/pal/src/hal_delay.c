#include "FreeRTOS.h"
#include "stm32f4xx_hal.h"
#include "task.h"

/* Overrides the HAL's weak busy-wait HAL_Delay(). The USB host stack (USBH_Delay(), and the
 * HAL's own port reset) calls it for ~310 ms per attach from a High-priority task; as a
 * busy-wait that starved every lower-priority task for the whole time. Sleep instead whenever
 * a task is asking; keep the busy-wait before the scheduler starts and in interrupt context. */
void HAL_Delay(uint32_t Delay)
{
    if ((xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && (__get_IPSR() == 0U))
    {
        /* +1 tick: vTaskDelay() counts tick edges, so N ticks can be almost one tick short. */
        vTaskDelay(pdMS_TO_TICKS(Delay) + 1U);
        return;
    }

    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) <= Delay)
    {
    }
}
