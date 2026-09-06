#include "cfuture_sync_ops.h"

#include <zephyr/kernel.h>

#include <stdint.h>

static struct k_event s_event_group;
static uint32_t s_next_bit;

static void *event_create(void)
{
    if (s_next_bit >= 32U)
    {
        return NULL;
    }

    uint32_t bit_mask = 1UL << s_next_bit;
    s_next_bit++;

    return (void *)(uintptr_t)bit_mask;
}

static void event_destroy(void *event_handle)
{
    /* No-op: see the header comment - slots in this app are never freed. */
    (void)event_handle;
}

static void event_set(void *event_handle)
{
    uint32_t bit_mask = (uint32_t)(uintptr_t)event_handle;
    (void)k_event_post(&s_event_group, bit_mask);
}

static bool event_wait(void *event_handle, uint32_t timeout_ms)
{
    uint32_t bit_mask = (uint32_t)(uintptr_t)event_handle;
    k_timeout_t timeout;

    if (timeout_ms == 0U)
    {
        timeout = K_NO_WAIT;
    }
    else if (timeout_ms == UINT32_MAX)
    {
        timeout = K_FOREVER;
    }
    else
    {
        timeout = K_MSEC(timeout_ms);
    }

    /* reset=true auto-clears the bit on a successful wait, matching the
     * ThreadX target's TX_OR_CLEAR behavior. */
    return k_event_wait(&s_event_group, bit_mask, true, timeout) != 0U;
}

static void event_reset(void *event_handle)
{
    uint32_t bit_mask = (uint32_t)(uintptr_t)event_handle;
    (void)k_event_clear(&s_event_group, bit_mask);
}

static const cfuture_sync_ops_t s_ops = {
    .event_create = event_create,
    .event_destroy = event_destroy,
    .event_set = event_set,
    .event_wait = event_wait,
    .event_reset = event_reset,
    /* k_event_post() is documented safe to call from ISR context - no
     * separate ISR variant needed. */
    .event_set_from_isr = event_set,
};

const cfuture_sync_ops_t *cfuture_sync_ops_get(void)
{
    static bool s_initialized;

    if (!s_initialized)
    {
        k_event_init(&s_event_group);
        s_initialized = true;
    }

    return &s_ops;
}
