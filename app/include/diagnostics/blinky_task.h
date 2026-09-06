#pragma once

#include <stdint.h>

#include "core/logger.h"
#include "pal/pal_led.h"

typedef struct BlinkyTaskConfig
{
    PalLed led;
    Logger *logger;
    uint32_t onTimeMs;
    uint32_t offTimeMs;
} BlinkyTaskConfig;

/* OSAL task entry (osal.h's OsalTaskEntryFn shape), identical on every
 * target - argument must be a BlinkyTaskConfig*. */
void blinkyTaskEntry(void *argument);
