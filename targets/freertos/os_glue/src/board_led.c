#include "board_led.h"

#include "gpio.h"

static void board_led_set(void *context, bool on)
{
    (void)context;
    MX_SET_LED(on ? 1U : 0U);
}

bool board_led_init(LedDevice *out_led)
{
    out_led->set = board_led_set;
    out_led->context = NULL;
    return true;
}
