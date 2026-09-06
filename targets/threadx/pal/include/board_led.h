#pragma once

#include "led_device.h"

/* Onboard LED (PC13, active-low), driven directly via HAL_GPIO_WritePin -
 * this target's gpio.c (CubeMX-generated, never hand-edited) has no
 * MX_SET_LED() helper the way the FreeRTOS target's does, so this talks to
 * the pin directly instead. Returns bool (never fails here) for the same
 * signature every target's board_led_init() uses - Zephyr's can genuinely
 * fail a gpio-not-ready check, so every target reports the same way. */
bool board_led_init(LedDevice *out_led) __attribute__((warn_unused_result));
