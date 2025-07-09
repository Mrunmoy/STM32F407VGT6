#pragma once

#include <stdint.h>

#include "led_device.h"
#include "logger.h"

typedef struct BlinkyTaskConfig
{
    LedDevice led;
    Logger *logger;
    uint32_t onTimeMs;
    uint32_t offTimeMs;
} BlinkyTaskConfig;

/* OSAL task entry (osal.h's OsalTaskEntryFn shape), identical on every
 * target - argument must be a BlinkyTaskConfig*. */
void blinkyTaskEntry(void *argument);
