#ifndef BOARD_RTU_TIMING_H
#define BOARD_RTU_TIMING_H

#include <stdint.h>

/* Private, hardware-independent timing core. Caller serializes all access.
 * Timestamps are byte-completion times, not task-poll or DMA-block times.
 * Unsigned subtraction permits timer wrap; each armed interval is < 2^31 us.
 */
typedef struct
{
    uint32_t t1_5_us;
    uint32_t t3_5_us;
    uint32_t char_us;
    uint32_t last_us;
    uint32_t gap_errors;
    uint8_t active;
    uint8_t dropping;
} board_rtu_timing_t;

static inline void board_rtu_timing_init(board_rtu_timing_t *s,
    uint32_t t1, uint32_t t3, uint32_t character, uint32_t now)
{
    s->t1_5_us = t1;
    s->t3_5_us = t3;
    s->char_us = character;
    s->last_us = now;
    s->gap_errors = 0U;
    s->active = 1U;
    s->dropping = 1U; /* Synchronize after enabling/changing baud. */
}

/* Returns 1 if the PREVIOUS sequence ended before this new byte.
 * Caller publishes/discards that sequence before appending this byte.
 */
static inline uint8_t board_rtu_timing_byte(board_rtu_timing_t *s, uint32_t now)
{
    uint8_t boundary = 0U;
    uint32_t delta = now - s->last_us;
    if (s->active != 0U)
    {
        if (delta >= s->t3_5_us + s->char_us)
        {
            boundary = 1U;
            s->dropping = 0U;
        }
        else if ((delta > s->t1_5_us + s->char_us) &&
                 (s->dropping == 0U))
        {
            s->dropping = 1U;
            s->gap_errors++;
        }
    }
    s->last_us = now;
    s->active = 1U;
    return boundary;
}

/* One extra character protects a byte that started just before t3.5
 * but has not completed yet. Actual frame separation is still t3.5.
 */
static inline uint8_t board_rtu_timing_poll(board_rtu_timing_t *s,
                                           uint32_t now)
{
    if ((s->active == 0U) ||
        ((uint32_t)(now - s->last_us) < s->t3_5_us + s->char_us))
    {
        return 0U;
    }
    s->active = 0U;
    s->dropping = 0U;
    return 1U;
}

#endif
