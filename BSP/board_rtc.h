#ifndef BOARD_RTC_H
#define BOARD_RTC_H

#include <stdint.h>

typedef struct
{
    uint16_t year;      /* 2000~2099 */
    uint8_t month;      /* 1~12 */
    uint8_t date;       /* 1~31 */
    uint8_t hour;       /* 0~23 */
    uint8_t minute;     /* 0~59 */
    uint8_t second;     /* 0~59 */
} board_rtc_time_t;

int board_rtc_init(void);
 
int board_rtc_time_get(board_rtc_time_t *time);
int board_rtc_time_set(const board_rtc_time_t *time);

#endif /* BOARD_RTC_H */
