#ifndef STORAGE_RECORD_FORMAT_H
#define STORAGE_RECORD_FORMAT_H

#include <stdint.h>
#include "board_rtc.h"

#define STORAGE_RECORD_TEXT_MAX 96U

uint32_t storage_record_time_to_unix(const board_rtc_time_t *time);

int storage_record_format_time(const board_rtc_time_t *time,
                               char *buffer, uint16_t capacity,
                               uint16_t *length);
int storage_record_format_sample(const board_rtc_time_t *time,
                                 float ch0, float ch1,
                                 char *buffer, uint16_t capacity,
                                 uint16_t *length);
int storage_record_format_alarm(const board_rtc_time_t *time,
                                uint8_t channel, float threshold, float actual,
                                char *buffer, uint16_t capacity,
                                uint16_t *length);

#endif
