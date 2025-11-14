#include "cfuture_sync_ops.h"

#include "tx_api.h"

#include <stdint.h>

static TX_EVENT_FLAGS_GROUP s_event_group;
static uint32_t s_next_bit;

static ULONG ms_to_ticks(uint32_t timeoutMs)
{
    ULONG ticks = TX_WAIT_FOREVER;

    if (timeoutMs == 0U)
    {
        ticks = TX_NO_WAIT;
    }
    else if (timeoutMs != UINT32_MAX)
    {
        ticks = (ULONG)((timeoutMs + (1000U / TX_TIMER_TICKS_PER_SECOND) - 1U) / (1000U / TX_TIMER_TICKS_PER_SECOND));
        if (ticks == 0U)
        {
            ticks = 1U;
        }
    }

    return ticks;
}

static void *event_create(void)
{
    if (s_next_bit >= 32U)
    {
        return NULL;
    }

    ULONG bitMask = 1UL << s_next_bit;
    s_next_bit++;

    return (void *)bitMask;
}

static void event_destroy(void *eventHandle)
{
    /* No-op: see the header comment - slots in this app are never freed. */
    (void)eventHandle;
}

static void event_set(void *eventHandle)
{
    ULONG bitMask = (ULONG)eventHandle;
    (void)tx_event_flags_set(&s_event_group, bitMask, TX_OR);
}

static bool event_wait(void *eventHandle, uint32_t timeoutMs)
{
    ULONG bitMask = (ULONG)eventHandle;
    ULONG actualFlags = 0U;

    return tx_event_flags_get(&s_event_group, bitMask, TX_OR_CLEAR, &actualFlags, ms_to_ticks(timeoutMs)) == TX_SUCCESS;
}

static void event_reset(void *eventHandle)
{
    ULONG bitMask = (ULONG)eventHandle;
    (void)tx_event_flags_set(&s_event_group, ~bitMask, TX_AND);
}

static const cfuture_sync_ops_t s_ops = {
    .event_create = event_create,
    .event_destroy = event_destroy,
    .event_set = event_set,
    .event_wait = event_wait,
    .event_reset = event_reset,
    /* tx_event_flags_set() is documented ISR-safe as-is (it handles nested-
     * interrupt reentrancy internally) - no separate ISR variant needed. */
    .event_set_from_isr = event_set,
};

const cfuture_sync_ops_t *cfuture_sync_ops_get(void)
{
    static bool s_initialized;

    /* Only latch s_initialized on success - if tx_event_flags_create() ever
     * fails, every later event_set()/event_wait() would otherwise silently
     * operate on an uninitialized TX_EVENT_FLAGS_GROUP with no diagnostic.
     * Leaving s_initialized false makes the next caller retry instead. */
    if (!s_initialized)
    {
        s_initialized = tx_event_flags_create(&s_event_group, "CfutureEventFlags") == TX_SUCCESS;
    }

    return &s_ops;
}
