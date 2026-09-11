#ifndef TEST_APP_PROTOCOL_BOARD_RTC_H
#define TEST_APP_PROTOCOL_BOARD_RTC_H

#include <stdint.h>

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} board_rtc_time_t;

int board_rtc_time_get(board_rtc_time_t *time);

#endif
