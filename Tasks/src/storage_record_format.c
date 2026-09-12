#include <string.h>

#include "storage_record_format.h"

/* 向临时文本缓冲区追加一个字符，并为末尾 NUL 保留空间。 */
static int storage_record_put_char(char *buffer, uint16_t capacity,
                                   uint16_t *position, char value)
{
    if ((*position + 1U) >= capacity)
    {
        return 0;
    }
    buffer[*position] = value;
    *position = (uint16_t)(*position + 1U);
    return 1;
}

/* 追加固定宽度十进制数字；value 超出指定宽度时失败。 */
static int storage_record_put_fixed(char *buffer, uint16_t capacity,
                                    uint16_t *position, uint32_t value,
                                    uint8_t digits)
{
    uint32_t divisor = 1U;
    uint8_t index;
    for (index = 1U; index < digits; index++) divisor *= 10U;
    if (value >= (divisor * 10U)) return 0;
    for (index = 0U; index < digits; index++)
    {
        if (storage_record_put_char(buffer, capacity, position,
                                    (char)('0' + ((value / divisor) % 10U))) == 0) return 0;
        divisor /= 10U;
    }
    return 1;
}

/* 追加不带前导零限制的无符号十进制数。 */
static int storage_record_put_u32(char *buffer, uint16_t capacity,
                                  uint16_t *position, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;
    uint8_t index;
    do { digits[count++] = (char)('0' + (value % 10U)); value /= 10U; } while (value != 0U);
    for (index = count; index > 0U; index--)
        if (storage_record_put_char(buffer, capacity, position, digits[index - 1U]) == 0) return 0;
    return 1;
}

/* 追加非负浮点数的两位小数；输入范围由调用者保证为业务量程。 */
static int storage_record_put_fixed2(char *buffer, uint16_t capacity,
                                     uint16_t *position, float value)
{
    uint32_t scaled;
    if (!(value >= 0.0f) || !(value <= 1000000.0f)) return 0;
    scaled = (uint32_t)((value * 100.0f) + 0.5f);
    if ((storage_record_put_u32(buffer, capacity, position, scaled / 100U) == 0) ||
        (storage_record_put_char(buffer, capacity, position, '.') == 0) ||
        (storage_record_put_fixed(buffer, capacity, position, scaled % 100U, 2U) == 0)) return 0;
    return 1;
}

/* 将 RTC 时间写为 YYYY-MM-DD HH:MM:SS；临时缓冲区不成功时不发布输出。 */
static int storage_record_append_time(const board_rtc_time_t *time,
                                      char *buffer, uint16_t capacity,
                                      uint16_t *position)
{
    if ((time == 0) || (time->year < 2000U) || (time->year > 2099U) ||
        (time->month < 1U) || (time->month > 12U) || (time->date < 1U) ||
        (time->date > 31U) || (time->hour > 23U) || (time->minute > 59U) ||
        (time->second > 59U)) return 0;
    return (storage_record_put_fixed(buffer, capacity, position, time->year, 4U) != 0) &&
           (storage_record_put_char(buffer, capacity, position, '-') != 0) &&
           (storage_record_put_fixed(buffer, capacity, position, time->month, 2U) != 0) &&
           (storage_record_put_char(buffer, capacity, position, '-') != 0) &&
           (storage_record_put_fixed(buffer, capacity, position, time->date, 2U) != 0) &&
           (storage_record_put_char(buffer, capacity, position, ' ') != 0) &&
           (storage_record_put_fixed(buffer, capacity, position, time->hour, 2U) != 0) &&
           (storage_record_put_char(buffer, capacity, position, ':') != 0) &&
           (storage_record_put_fixed(buffer, capacity, position, time->minute, 2U) != 0) &&
           (storage_record_put_char(buffer, capacity, position, ':') != 0) &&
           (storage_record_put_fixed(buffer, capacity, position, time->second, 2U) != 0);
}

/* 发布时间文本到调用者缓冲区；失败时保持 buffer 和 length 不变。 */
int storage_record_format_time(const board_rtc_time_t *time,
                               char *buffer, uint16_t capacity,
                               uint16_t *length)
{
    char temporary[STORAGE_RECORD_TEXT_MAX];
    uint16_t position = 0U;
    if ((time == 0) || (buffer == 0) || (length == 0) ||
        (capacity == 0U) || (capacity > STORAGE_RECORD_TEXT_MAX)) return 0;
    if (storage_record_append_time(time, temporary, capacity, &position) == 0) return 0;
    if (storage_record_put_char(temporary, capacity, &position, '\0') == 0) return 0;
    memcpy(buffer, temporary, (size_t)position + 1U);
    *length = (uint16_t)(position - 1U);
    return 1;
}

/* 格式化一条采样 CSV：时间,ch0,ch1\r\n；仅成功时写出完整行。 */
int storage_record_format_sample(const board_rtc_time_t *time,
                                 float ch0, float ch1,
                                 char *buffer, uint16_t capacity,
                                 uint16_t *length)
{
    char temporary[STORAGE_RECORD_TEXT_MAX];
    uint16_t position = 0U;
    if ((time == 0) || (buffer == 0) || (length == 0) ||
        (capacity == 0U) || (capacity > STORAGE_RECORD_TEXT_MAX)) return 0;
    if ((storage_record_append_time(time, temporary, capacity, &position) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, ',') == 0) ||
        (storage_record_put_fixed2(temporary, capacity, &position, ch0) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, ',') == 0) ||
        (storage_record_put_fixed2(temporary, capacity, &position, ch1) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, '\r') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, '\n') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, '\0') == 0)) return 0;
    memcpy(buffer, temporary, (size_t)position + 1U);
    *length = (uint16_t)(position - 1U);
    return 1;
}

/* 格式化一条告警 CSV：时间,CHx,阈值,实际值\r\n；通道仅允许 0/1。 */
int storage_record_format_alarm(const board_rtc_time_t *time,
                                uint8_t channel, float threshold, float actual,
                                char *buffer, uint16_t capacity,
                                uint16_t *length)
{
    char temporary[STORAGE_RECORD_TEXT_MAX];
    uint16_t position = 0U;
    if ((time == 0) || (channel >= 2U) || (buffer == 0) || (length == 0) ||
        (capacity == 0U) || (capacity > STORAGE_RECORD_TEXT_MAX)) return 0;
    if ((storage_record_append_time(time, temporary, capacity, &position) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, ',') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, 'C') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, 'H') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, (char)('0' + channel)) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, ',') == 0) ||
        (storage_record_put_fixed2(temporary, capacity, &position, threshold) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, ',') == 0) ||
        (storage_record_put_fixed2(temporary, capacity, &position, actual) == 0) ||
        (storage_record_put_char(temporary, capacity, &position, '\r') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, '\n') == 0) ||
        (storage_record_put_char(temporary, capacity, &position, '\0') == 0)) return 0;
    memcpy(buffer, temporary, (size_t)position + 1U);
    *length = (uint16_t)(position - 1U);
    return 1;
}
