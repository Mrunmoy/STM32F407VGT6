#include "app_task_trace.h"

#include "osal.h"

#include <string.h>

static AppTaskTrace s_traces[kAppTaskTraceCapacity];
static uint32_t s_traceCount;

/* Shared by every lookup below - a linear scan is fine at this call
 * frequency (at most once per task per loop iteration) over at most
 * kAppTaskTraceCapacity entries. Pointer equality on taskName would work in
 * practice (names are always string literals from a small set of call
 * sites), but a real string compare doesn't depend on every caller reusing
 * the exact same literal instance. */
static AppTaskTrace *findExisting(const char *taskName)
{
    for (uint32_t i = 0U; i < s_traceCount; i++)
    {
        if (strcmp(s_traces[i].taskName, taskName) == 0)
        {
            return &s_traces[i];
        }
    }

    return NULL;
}

static AppTaskTrace *findOrCreate(const char *taskName)
{
    AppTaskTrace *existing = findExisting(taskName);
    if (existing != NULL)
    {
        return existing;
    }

    if (s_traceCount >= kAppTaskTraceCapacity)
    {
        return NULL;
    }

    AppTaskTrace *trace = &s_traces[s_traceCount];
    s_traceCount++;

    trace->taskName = taskName;
    trace->registeredTimeMs = osal_get_time_ms();
    trace->lastCheckInTimeMs = trace->registeredTimeMs;
    trace->lastLoopStartMs = trace->registeredTimeMs;
    trace->lastLoopDurationMs = 0U;
    trace->lastCadenceMs = 0U;
    trace->iterationCount = 0U;
    trace->lastCheckpoint = NULL;
    trace->stopRequested = false;
    trace->stopped = false;

    return trace;
}

void appTaskTraceRegister(const char *taskName)
{
    (void)findOrCreate(taskName);
}

void appTaskTraceLoopStart(const char *taskName)
{
    AppTaskTrace *trace = findOrCreate(taskName);
    if (trace == NULL)
    {
        return;
    }

    uint32_t now = osal_get_time_ms();
    trace->lastCadenceMs = now - trace->lastLoopStartMs;
    trace->lastLoopStartMs = now;
    trace->lastCheckInTimeMs = now;
}

void appTaskTraceLoopEnd(const char *taskName)
{
    AppTaskTrace *trace = findOrCreate(taskName);
    if (trace == NULL)
    {
        return;
    }

    uint32_t now = osal_get_time_ms();
    trace->lastLoopDurationMs = now - trace->lastLoopStartMs;
    trace->lastCheckInTimeMs = now;
    trace->iterationCount++;
}

void appTaskTraceCheckpoint(const char *taskName, const char *checkpoint)
{
    AppTaskTrace *trace = findOrCreate(taskName);
    if (trace == NULL)
    {
        return;
    }

    trace->lastCheckpoint = checkpoint;
    trace->lastCheckInTimeMs = osal_get_time_ms();
}

uint32_t appTaskTraceCount(void)
{
    return s_traceCount;
}

bool appTaskTraceGetByIndex(uint32_t index, AppTaskTrace *outTrace)
{
    if ((index >= s_traceCount) || (outTrace == NULL))
    {
        return false;
    }

    *outTrace = s_traces[index];
    return true;
}

void appTaskTraceRequestStop(const char *taskName)
{
    AppTaskTrace *trace = findExisting(taskName);
    if (trace != NULL)
    {
        trace->stopRequested = true;
    }
}

bool appTaskTraceShouldStop(const char *taskName)
{
    AppTaskTrace *trace = findExisting(taskName);
    return (trace != NULL) && trace->stopRequested;
}

void appTaskTraceMarkStopped(const char *taskName)
{
    AppTaskTrace *trace = findExisting(taskName);
    if (trace != NULL)
    {
        trace->stopped = true;
    }
}
