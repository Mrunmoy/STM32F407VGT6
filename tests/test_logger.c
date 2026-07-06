/* Unit test for app/src/logger.c - the one piece of app/ that's pure
 * formatting logic sitting entirely behind DI interfaces (LogSink,
 * TimeSource), so it's testable in complete isolation with no OS, no
 * hardware, and no mocking framework: a fake LogSink and a fake TimeSource,
 * both plain structs of one function pointer, are all it takes.
 *
 * Plain assert()-based checks, not a test framework - this project has none
 * vendored, and one test file doesn't need one. Run via `ctest` (root
 * CMakeLists.txt's enable_testing()/add_test()) or directly. */

#include "logger.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ── Fake LogSink: captures the last formatted line verbatim ────────────── */

static char s_capturedLine[256];

static bool fakeSinkWrite(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    size_t copyLength = (length < sizeof(s_capturedLine) - 1U) ? length : (sizeof(s_capturedLine) - 1U);
    memcpy(s_capturedLine, data, copyLength);
    s_capturedLine[copyLength] = '\0';
    return true;
}

/* ── Fake TimeSource: returns a fixed, known DateTime ────────────────────── */

static bool fakeTimeGetWithDate(void *context, DateTime *outTime)
{
    (void)context;
    outTime->hasDate = true;
    outTime->year = 2026;
    outTime->month = 3;
    outTime->day = 14;
    outTime->hours = 9;
    outTime->minutes = 5;
    outTime->seconds = 7;
    return true;
}

static bool fakeTimeGetUptimeOnly(void *context, DateTime *outTime)
{
    (void)context;
    outTime->hasDate = false;
    outTime->hours = 1;
    outTime->minutes = 2;
    outTime->seconds = 3;
    return true;
}

static void testLogsWithCalendarDate(void)
{
    LogSink sink = {.write = fakeSinkWrite, .context = NULL};
    TimeSource timeSource = {.get = fakeTimeGetWithDate, .context = NULL};
    Logger logger;
    loggerInit(&logger, sink, timeSource);

    s_capturedLine[0] = '\0';
    loggerLog(&logger, kLogLevelEvent, "hello");

    assert(strcmp(s_capturedLine, "[2026-03-14 09:05:07] [EVENT] hello\r\n") == 0);
}

static void testLogsWithUptimeOnly(void)
{
    LogSink sink = {.write = fakeSinkWrite, .context = NULL};
    TimeSource timeSource = {.get = fakeTimeGetUptimeOnly, .context = NULL};
    Logger logger;
    loggerInit(&logger, sink, timeSource);

    s_capturedLine[0] = '\0';
    loggerLog(&logger, kLogLevelError, "trouble");

    assert(strcmp(s_capturedLine, "[up 01:02:03] [ERROR] trouble\r\n") == 0);
}

static void testErrorLevelLabel(void)
{
    LogSink sink = {.write = fakeSinkWrite, .context = NULL};
    TimeSource timeSource = {.get = fakeTimeGetWithDate, .context = NULL};
    Logger logger;
    loggerInit(&logger, sink, timeSource);

    s_capturedLine[0] = '\0';
    loggerLog(&logger, kLogLevelError, "oops");

    assert(strstr(s_capturedLine, "[ERROR] oops") != NULL);
}

int main(void)
{
    testLogsWithCalendarDate();
    testLogsWithUptimeOnly();
    testErrorLevelLabel();

    printf("test_logger: all tests passed\n");
    return 0;
}
