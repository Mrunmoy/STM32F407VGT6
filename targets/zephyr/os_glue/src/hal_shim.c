/**
 * @file hal_shim.c
 * @brief Minimal STM32 HAL runtime shim for using stm32f4xx_hal_hcd.c /
 *        stm32f4xx_ll_usb.c (Zephyr's own hal_stm32 module vendors these
 *        verbatim, same as the sibling FreeRTOS/ThreadX repos, but Zephyr's
 *        own build never compiles or calls HAL_Init() for this SoC - GPIO
 *        and clock control there go through LL/Zephyr drivers instead).
 *
 * HAL_Delay()/HAL_GetTick() are already provided by Zephyr itself
 * (zephyr/soc/st/stm32/common/stm32cube_hal.c, k_msleep/k_uptime_get_32-
 * backed, always compiled in whenever this SoC's hal_stm32 module is used -
 * which our GPIO/clock control already pulls in) - do NOT redefine them
 * here, that's a duplicate-symbol link error. The only thing genuinely
 * missing is Error_Handler(), which the vendored HCD/USB Host code calls on
 * unrecoverable init failure. HAL_Init()/HAL_MspInit() are deliberately
 * never called - that sequence would try to reconfigure SysTick, fighting
 * Zephyr's own timer driver, which already owns time-keeping on this build.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

void Error_Handler(void)
{
    printk("[ERROR] Error_Handler() called - halting\r\n");
    __disable_irq();
    while (1)
    {
    }
}
