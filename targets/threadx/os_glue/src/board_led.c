#include "board_led.h"

#include "gpio.h"

/* LED on PC13 is active-low: anode->3.3V, cathode->PC13, so drive the pin
 * LOW to light it and HIGH to turn it off. */
static void board_led_set(void *context, bool on)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool board_led_init(LedDevice *out_led)
{
    out_led->set = board_led_set;
    out_led->context = NULL;
    return true;
}
