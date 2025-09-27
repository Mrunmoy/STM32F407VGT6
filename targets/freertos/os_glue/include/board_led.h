#pragma once

#include "led_device.h"

/* Onboard LED (PC13, active-low), driven through gpio.c's MX_SET_LED().
 * Returns bool (never fails here) for the same signature every target's
 * board_led_init() uses - Zephyr's can genuinely fail a gpio-not-ready
 * check, so every target reports the same way. */
bool board_led_init(LedDevice *out_led) __attribute__((warn_unused_result));
