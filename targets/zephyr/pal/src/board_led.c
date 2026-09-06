#include "board_led.h"

#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec s_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void board_led_set(void *context, bool on)
{
    (void)context;
    gpio_pin_set_dt(&s_led, on ? 1 : 0);
}

bool board_led_init(LedDevice *out_led)
{
    if (!gpio_is_ready_dt(&s_led))
    {
        return false;
    }

    gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE);

    out_led->set = board_led_set;
    out_led->context = NULL;
    return true;
}
