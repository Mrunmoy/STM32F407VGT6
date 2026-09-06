#pragma once

#include <stdbool.h>

typedef void (*LedSetFn)(void *context, bool on);

/* Abstract single LED (dependency inversion): BlinkyTask depends on this,
 * never on a concrete GPIO pin. */
typedef struct PalLed
{
    LedSetFn set;
    void *context;
} PalLed;

typedef PalLed LedDevice;
