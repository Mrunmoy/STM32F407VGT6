#include "board_led.h"

#include <stddef.h>

static void board_led_set(void *context, bool on)
{
    (void)context;
    (void)on;
}

bool board_led_init(LedDevice *out_led)
{
    out_led->set = board_led_set;
    out_led->context = NULL;
    return true;
}
