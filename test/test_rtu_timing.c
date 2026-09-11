#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "board_rtu_timing.h"

int main(void)
{
    board_rtu_timing_t s;
    uint32_t base;
    unsigned i;
    board_rtu_timing_init(&s, 750, 1750, 96, 0);
    assert(s.dropping); /* Startup must synchronize to silence. */
    assert(!board_rtu_timing_poll(&s, 1749));
    assert(board_rtu_timing_poll(&s, 1846));
    assert(!s.dropping);
    assert(!board_rtu_timing_byte(&s, 2000));
    assert(!board_rtu_timing_byte(&s, 2096));
    assert(!s.dropping && s.gap_errors == 0);
    assert(!board_rtu_timing_byte(&s, 2942)); /* 750us silence */
    assert(!s.dropping);
    assert(!board_rtu_timing_byte(&s, 3789)); /* 751us silence */
    assert(s.dropping && s.gap_errors == 1);
    assert(!board_rtu_timing_byte(&s, 3885)); /* Bad-frame suffix */
    assert(s.dropping && s.gap_errors == 1);
    assert(board_rtu_timing_byte(&s, 5731)); /* Exactly t3.5 + char */
    assert(!s.dropping);
    assert(!board_rtu_timing_poll(&s, 7481)); /* A byte may be in flight. */
    assert(board_rtu_timing_poll(&s, 7577));
    assert(!board_rtu_timing_poll(&s, 8000)); /* No duplicate boundary */

    /* Wraparound, delayed timer, long burst and startup traffic. */
    base = UINT32_MAX - 1000U;
    board_rtu_timing_init(&s, 750, 1750, 96, base);
    assert(!board_rtu_timing_byte(&s, base + 100));
    assert(s.dropping);
    assert(board_rtu_timing_poll(&s, base + 1946));
    for (i = 0; i < 512; i++)
    {
        assert(!board_rtu_timing_byte(&s, base + 2100 + i * 96));
        assert(!s.dropping);
    }
    assert(board_rtu_timing_byte(&s, base + 2100 + 511 * 96 + 2000));
    assert(!s.dropping);
    puts("RTU timing: boundary, recovery, startup, wraparound, delayed timer PASS");
    return 0;
}
