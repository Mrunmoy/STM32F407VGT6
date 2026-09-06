#include "rtc_time_source.h"

#ifdef HAL_RTC_MODULE_ENABLED

#include "rtc.h"

#include "cmsis_os2.h"

enum
{
    kRtcInitializedMagicValue = 0x1234U,
};

/* Blinky, StorageSvc, and the 4 client tasks all call loggerLog() on their
 * own cadence, which calls this TimeSource.get() with no serialization of
 * its own. HAL_RTC_GetTime() latches the RTC's calendar shadow registers and
 * HAL_RTC_GetDate() must be read from that same latch before another read
 * re-latches it - without a lock, one thread's date read can be latched by
 * a different thread's concurrent time read, producing a self-inconsistent
 * timestamp. Same class of bug already found and fixed for the shared UART
 * write (log_sink_impl.c's mutex, after real hardware showed torn
 * transmissions) - same fix here. */
static osMutexId_t s_mutex;

static void set_default_date_time(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    time.Hours = 18U;
    time.Minutes = 0U;
    time.Seconds = 0U;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    (void)HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);

    date.WeekDay = RTC_WEEKDAY_SATURDAY;
    date.Month = RTC_MONTH_JULY;
    date.Date = 19U;
    date.Year = 26U;
    (void)HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, kRtcInitializedMagicValue);
}

static bool rtc_time_source_get(void *context, DateTime *out_time)
{
    RTC_HandleTypeDef *handle = (RTC_HandleTypeDef *)context;
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    if (s_mutex != NULL)
    {
        (void)osMutexAcquire(s_mutex, osWaitForever);
    }

    (void)HAL_RTC_GetTime(handle, &time, RTC_FORMAT_BIN);
    /* Date shadow register only latches after a time read - order matters. */
    (void)HAL_RTC_GetDate(handle, &date, RTC_FORMAT_BIN);

    if (s_mutex != NULL)
    {
        (void)osMutexRelease(s_mutex);
    }

    out_time->hasDate = true;
    out_time->year = (uint16_t)(2000U + date.Year);
    out_time->month = date.Month;
    out_time->day = date.Date;
    out_time->hours = time.Hours;
    out_time->minutes = time.Minutes;
    out_time->seconds = time.Seconds;

    return true;
}

bool rtc_time_source_init(TimeSource *out_time_source)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    RCC_OscInitTypeDef oscInit = {0};
    oscInit.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    oscInit.LSEState = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&oscInit) != HAL_OK)
    {
        return false;
    }

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127U;
    hrtc.Init.SynchPrediv = 255U;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK)
    {
        return false;
    }

    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != kRtcInitializedMagicValue)
    {
        set_default_date_time();
    }

    if (s_mutex == NULL)
    {
        s_mutex = osMutexNew(NULL);
    }

    out_time_source->get = rtc_time_source_get;
    out_time_source->context = &hrtc;
    return true;
}

#else

bool rtc_time_source_init(TimeSource *out_time_source)
{
    (void)out_time_source;
    return false;
}

#endif /* HAL_RTC_MODULE_ENABLED */
