#include "time_source_impl.h"

#include "osal.h"

static bool time_source_get(void *context, DateTime *out_time)
{
    (void)context;

    uint32_t total_seconds = osal_get_time_ms() / 1000U;

    out_time->hasDate = false;
    out_time->year = 0U;
    out_time->month = 0U;
    out_time->day = 0U;
    out_time->hours = (uint8_t)((total_seconds / 3600U) % 24U);
    out_time->minutes = (uint8_t)((total_seconds / 60U) % 60U);
    out_time->seconds = (uint8_t)(total_seconds % 60U);

    return true;
}

void time_source_init(TimeSource *out_time_source)
{
    out_time_source->get = time_source_get;
    out_time_source->context = NULL;
}
