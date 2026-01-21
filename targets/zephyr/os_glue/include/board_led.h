#pragma once

#include "led_device.h"

/* Onboard LED (PC13, active-low - devicetree's led0 alias, see
 * boards/black_f407zg_pro.overlay), driven via Zephyr's GPIO API. Returns
 * false if the GPIO device isn't ready - callers should treat that as a
 * boot-time fault, not something to retry. */
bool board_led_init(LedDevice *out_led) __attribute__((warn_unused_result));
