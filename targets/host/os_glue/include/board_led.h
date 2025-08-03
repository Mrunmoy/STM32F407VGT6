#pragma once

#include "led_device.h"

/* No-op LedDevice - this target has no real LED. Blinky still runs (same
 * thread list as every other target - see app.c's appRun()), it just
 * doesn't drive any hardware; its LED ON/OFF log lines are the only
 * visible effect. Returns bool (never fails here) for the same signature
 * every target's board_led_init() uses - Zephyr's can genuinely fail a
 * gpio-not-ready check, so every target reports the same way. */
bool board_led_init(LedDevice *out_led) __attribute__((warn_unused_result));
