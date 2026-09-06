#include "blinky_task.h"

#include "app_task_trace.h"
#include "osal.h"

void blinkyTaskEntry(void *argument)
{
    static const char kTaskName[] = "Blinky";

    BlinkyTaskConfig *config = (BlinkyTaskConfig *)argument;

    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);

        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }

        config->led.set(config->led.context, true);
        loggerLog(config->logger, kLogLevelEvent, "LED ON");
        osal_delay_ms(config->onTimeMs);

        config->led.set(config->led.context, false);
        loggerLog(config->logger, kLogLevelEvent, "LED OFF");
        osal_delay_ms(config->offTimeMs);

        appTaskTraceLoopEnd(kTaskName);
    }

    /* Leave the LED off rather than mid-blink - a stopped task should leave
     * its owned hardware in a tidy, known state. */
    config->led.set(config->led.context, false);
    loggerLog(config->logger, kLogLevelEvent, "Blinky stopped");
    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}
