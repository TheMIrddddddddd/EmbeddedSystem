#ifndef BOARD_TIMEBASE_H
#define BOARD_TIMEBASE_H

#include <stdint.h>

int board_timebase_init(void);
uint32_t board_timebase_now_us(void);

typedef void (*board_timebase_alarm_callback_t)(void);

int board_timebase_alarm_start(uint32_t delay_us, board_timebase_alarm_callback_t callback);

void board_timebase_alarm_cancel(void);

void board_timebase_irq_handler(void);

#endif /* BOARD_TIMEBASE_H */
