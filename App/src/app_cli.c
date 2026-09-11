#include <string.h>

#include "app_cli.h"
#include "app_config.h"
#include "board_usart.h"
#include "board_rtc.h"
#include "board_spi_flash.h"
#include "cli.h"
#include "storage_task.h"
#include "sample_task.h"
#include "control_task.h"
#include "task_events.h"

#include "FreeRTOS.h"
#include "task.h"

#define APP_CLI_DRAIN_TIMEOUT_TICKS    500U
#define APP_CLI_OUTPUT_BUFFER_SIZE     64U
#define APP_CLI_STORAGE_TIMEOUT_MS     2000U

/*
 * banner 与 help 共用这份文本，只此一份，不放两处。
 */
static const char s_cli_help_text[] =
    " Commands:\r\n"
    "   test              system self check\r\n"
    "   rtc config/now    set or show time\r\n"
    "   conf              import config.ini from TF\r\n"
    "   ratio ch0/ch1     set channel ratio (0-100)\r\n"
    "   limit ch0/ch1     set channel limit (0-500)\r\n"
    "   config save/read  store or show parameters\r\n"
    "   protocol          switch RS485 protocol\r\n"
    "   id                set device id\r\n"
    "   baud              set RS485 baudrate\r\n"
    "   start / stop      sampling on / off\r\n"
    "   hide / unhide     hidden hex format on / off\r\n"
    "   help              show this list\r\n"
    "   version           show firmware version\r\n"
    "==================================================\r\n";

static const char s_cli_version_text[] =
    "app version " APP_CLI_VERSION " (" __DATE__ " " __TIME__ ")\r\n";

/* ------------------------------------------------------------------ */
/* 文本格式化 helper（不用 snprintf，避免拉入浮点格式化库）             */
/* ------------------------------------------------------------------ */

uint16_t app_cli_u32_to_dec(char *dst, uint32_t value, uint8_t width)
{
    char tmp[10];
    uint16_t count = 0U;
    uint16_t pos = 0U;
    uint8_t pad;

    do
    {
        tmp[count] = (char)('0' + (value % 10U));
        count++;
        value /= 10U;
    } while (value != 0U);

    for (pad = (uint8_t)count; pad < width; pad++)
    {
        dst[pos] = '0';
        pos++;
    }

    while (count > 0U)
    {
        count--;
        dst[pos] = tmp[count];
        pos++;
    }

    return pos;
}

/* 定点两位小数：1.65 -> "1.65" */
uint16_t app_cli_append_fixed2(char *dst, uint16_t pos, float value)
{
    uint32_t cents;

    if (value < 0.0f)
    {
        value = 0.0f;
    }

    cents = (uint32_t)((value * 100.0f) + 0.5f);

    pos += app_cli_u32_to_dec(&dst[pos], cents / 100U, 0U);
    dst[pos] = '.';
    pos++;
    pos += app_cli_u32_to_dec(&dst[pos], cents % 100U, 2U);

    return pos;
}

uint16_t app_cli_append_hex16(char *dst, uint16_t pos, uint16_t value)
{
    static const char hex_chars[] = "0123456789ABCDEF";

    dst[pos] = hex_chars[(value >> 12) & 0x0FU];
    dst[pos + 1U] = hex_chars[(value >> 8) & 0x0FU];
    dst[pos + 2U] = hex_chars[(value >> 4) & 0x0FU];
    dst[pos + 3U] = hex_chars[value & 0x0FU];

    return (uint16_t)(pos + 4U);
}

uint16_t app_cli_append_time(char *dst, uint16_t pos, const board_rtc_time_t *time)
{
    pos += app_cli_u32_to_dec(&dst[pos], time->year, 4U);
    dst[pos] = '-';
    pos++;
    pos += app_cli_u32_to_dec(&dst[pos], time->month, 2U);
    dst[pos] = '-';
    pos++;
    pos += app_cli_u32_to_dec(&dst[pos], time->date, 2U);
    dst[pos] = ' ';
    pos++;
    pos += app_cli_u32_to_dec(&dst[pos], time->hour, 2U);
    dst[pos] = ':';
    pos++;
    pos += app_cli_u32_to_dec(&dst[pos], time->minute, 2U);
    dst[pos] = ':';
    pos++;
    pos += app_cli_u32_to_dec(&dst[pos], time->second, 2U);

    return pos;
}

/* days_from_civil（Hinnant 算法），已用 1970-01-01 -> 0 验证 */
uint32_t app_cli_time_to_unix(const board_rtc_time_t *time)
{
    uint32_t year = time->year;
    uint32_t month = time->month;
    uint32_t era;
    uint32_t year_of_era;
    uint32_t day_of_year;
    uint32_t day_of_era;
    uint32_t days;

    if (month <= 2U)
    {
        year -= 1U;
    }

    era = year / 400U;
    year_of_era = year - (era * 400U);
    day_of_year = ((153U * (((month > 2U) ? (month - 3U) : (month + 9U)))) + 2U) / 5U
                  + (uint32_t)time->date - 1U;
    day_of_era = (year_of_era * 365U) + (year_of_era / 4U)
                 - (year_of_era / 100U) + day_of_year;
    days = (era * 146097U) + day_of_era - 719468U;

    return (days * 86400U) +
           ((uint32_t)time->hour * 3600U) +
           ((uint32_t)time->minute * 60U) +
           (uint32_t)time->second;
}

/* ------------------------------------------------------------------ */
/* 控制台输出                                                          */
/* ------------------------------------------------------------------ */

int app_cli_write(const char *data, uint16_t length)
{
    uint16_t offset;
    uint32_t waited_ticks;

    if (data == NULL)
    {
        return 0;
    }

    offset = 0U;
    waited_ticks = 0U;

    while (offset < length)
    {
        uint16_t free_space;
        uint16_t chunk;

        free_space = board_usart0_tx_free_get();

        if (free_space == 0U)
        {
            if (waited_ticks >= APP_CLI_DRAIN_TIMEOUT_TICKS)
            {
                return 0;
            }

            vTaskDelay(1U);
            waited_ticks++;

            continue;
        }

        chunk = (uint16_t)(length - offset);

        if (chunk > free_space)
        {
            chunk = free_space;
        }

        if (board_usart0_send_buffer((const uint8_t *)&data[offset], chunk) != chunk)
        {
            /* 正常到不了这里：ISR 只消费不生产，free 只会变大。
             * 防将来出现第二个发送者时静默丢字。 */
            return 0;
        }

        offset = (uint16_t)(offset + chunk);
    }

    return 1;
}

int app_cli_print(const char *text)
{
    static const char newline[2] = { '\r', '\n' };
    uint16_t length;

    if (text == NULL)
    {
        return 0;
    }

    length = (uint16_t)strlen(text);

    if ((app_cli_write(text, length) == 0) ||
        (app_cli_write(newline, 2U) == 0))
    {
        return 0;
    }

    return 1;
}

void app_cli_banner_print(void)
{
    (void)app_cli_print("==================================================");
    (void)app_cli_print(" GD32F470 Industrial Data Terminal");
    (void)app_cli_print(" Console ready. Type 'help' for commands.");
    (void)app_cli_print("==================================================");

    (void)app_cli_write(s_cli_help_text, (uint16_t)(sizeof(s_cli_help_text) - 1U));

    (void)app_cli_write("\r\n", 2U);
}

/*
 * 两段式输入状态机（《01》三-2/3/5/6/7）。
 * 先回显当前值并提示输入，下一行按数值处理而不是命令。
 * 空行取消等待；等待期间收到非数值一律报 parameter invalid 并回到
 * 命令模式（与文档流程一致，数值行中间不能插别的命令）。
 */

typedef enum
{
    APP_CLI_INPUT_NONE = 0,
    APP_CLI_INPUT_RATIO,
    APP_CLI_INPUT_LIMIT,
    APP_CLI_INPUT_PROTOCOL,
    APP_CLI_INPUT_ID,
    APP_CLI_INPUT_BAUD,
    APP_CLI_INPUT_TIME
} app_cli_input_mode_t;

static app_cli_input_mode_t s_pending_mode = APP_CLI_INPUT_NONE;
static uint8_t s_pending_channel;

static storage_task_persist_request_t s_storage_request;
static storage_task_persist_result_t s_storage_result;
static uint32_t s_storage_request_sequence;
static uint32_t s_storage_pending_request_id;
static uint8_t s_storage_pending_operation;
static TickType_t s_storage_pending_deadline;
static char s_config_line_buffer[APP_CLI_OUTPUT_BUFFER_SIZE];

/* "ch0"/"ch1" -> 0/1，其余 0xFF */
static uint8_t app_cli_channel_parse(const char *text)
{
    if ((text[0] == 'c') && (text[1] == 'h') && (text[2] == '0') && (text[3] == '\0'))
    {
        return 0U;
    }

    if ((text[0] == 'c') && (text[1] == 'h') && (text[2] == '1') && (text[3] == '\0'))
    {
        return 1U;
    }

    return 0xFFU;
}

/*
 * 十进制定点解析："5"、"5.5"、"10.50"。
 * 拒绝空串、非数字、第二个点、单独的点、负号（文档将负数判无效）、
 * 超过 5 位小数。成功返回 1。
 */
static int app_cli_parse_fixed(const char *text, float *out)
{
    static const uint32_t scale_table[6] =
        { 1U, 10U, 100U, 1000U, 10000U, 100000U };

    uint32_t integer_part = 0U;
    uint32_t frac_value = 0U;
    uint8_t frac_digits = 0U;
    uint8_t in_fraction = 0U;
    uint8_t seen_digit = 0U;
    const char *p = text;

    if (text == NULL)
    {
        return 0;
    }

    while (*p != '\0')
    {
        if ((*p >= '0') && (*p <= '9'))
        {
            seen_digit = 1U;

            if (in_fraction == 0U)
            {
                if (integer_part > 429496729U)
                {
                    return 0;
                }

                integer_part = (integer_part * 10U) + (uint32_t)(*p - '0');
            }
            else
            {
                if (frac_digits >= 5U)
                {
                    return 0;
                }

                frac_value = (frac_value * 10U) + (uint32_t)(*p - '0');
                frac_digits++;
            }
        }
        else if (*p == '.')
        {
            if (in_fraction != 0U)
            {
                return 0;
            }

            in_fraction = 1U;
        }
        else
        {
            return 0;
        }

        p++;
    }

    if (seen_digit == 0U)
    {
        return 0;
    }

    *out = (float)integer_part +
           ((float)frac_value / (float)scale_table[frac_digits]);

    return 1;
}

/* 1~4 位 HEX（大小写均可），用于 id */
static int app_cli_parse_hex4(const char *text, uint16_t *out)
{
    uint32_t value = 0U;
    uint8_t count = 0U;
    const char *p = text;

    if (text == NULL)
    {
        return 0;
    }

    while (*p != '\0')
    {
        uint8_t digit;

        if ((*p >= '0') && (*p <= '9'))
        {
            digit = (uint8_t)(*p - '0');
        }
        else if ((*p >= 'A') && (*p <= 'F'))
        {
            digit = (uint8_t)((*p - 'A') + 10U);
        }
        else if ((*p >= 'a') && (*p <= 'f'))
        {
            digit = (uint8_t)((*p - 'a') + 10U);
        }
        else
        {
            return 0;
        }

        value = (value << 4) | (uint32_t)digit;
        count++;

        if (count > 4U)
        {
            return 0;
        }

        p++;
    }

    if (count == 0U)
    {
        return 0;
    }

    *out = (uint16_t)value;

    return 1;
}

/* 纯十进制，用于 baud */
static int app_cli_parse_u32(const char *text, uint32_t *out)
{
    uint32_t value = 0U;
    const char *p = text;

    if ((text == NULL) || (*text == '\0'))
    {
        return 0;
    }

    while (*p != '\0')
    {
        if ((*p < '0') || (*p > '9'))
        {
            return 0;
        }

        if (value > 429496729U)
        {
            return 0;
        }

        value = (value * 10U) + (uint32_t)(*p - '0');
        p++;
    }

    *out = value;

    return 1;
}

/* "current ch0 ratio: 1.00" 这类行 */
static void app_cli_print_current_fixed2(const char *prefix, float value)
{
    char buf[48];
    uint16_t pos = 0U;

    while (*prefix != '\0')
    {
        buf[pos] = *prefix;
        pos++;
        prefix++;
    }

    pos = app_cli_append_fixed2(buf, pos, value);
    buf[pos] = '\0';

    (void)app_cli_print(buf);
}

static void app_cli_print_current_dec(const char *prefix, uint32_t value)
{
    char buf[48];
    uint16_t pos = 0U;

    while (*prefix != '\0')
    {
        buf[pos] = *prefix;
        pos++;
        prefix++;
    }

    pos += app_cli_u32_to_dec(&buf[pos], value, 0U);
    buf[pos] = '\0';

    (void)app_cli_print(buf);
}

static void app_cli_print_current_hex4(const char *prefix, uint16_t value)
{
    char buf[48];
    uint16_t pos = 0U;

    while (*prefix != '\0')
    {
        buf[pos] = *prefix;
        pos++;
        prefix++;
    }

    pos = app_cli_append_hex16(buf, pos, value);
    buf[pos] = '\0';

    (void)app_cli_print(buf);
}

static uint16_t app_cli_append_text(char *dst,
                                    uint16_t pos,
                                    const char *text)
{
    while (*text != '\0')
    {
        dst[pos] = *text;
        pos++;
        text++;
    }

    return pos;
}

static void app_cli_print_config(const app_config_t *config)
{
    uint16_t pos;

    pos = 0U;
    pos = app_cli_append_text(s_config_line_buffer, pos, "device_id=");
    pos = app_cli_append_hex16(s_config_line_buffer, pos,
                               config->device_id);
    pos = app_cli_append_text(s_config_line_buffer, pos,
                              " sample_period=");
    pos += app_cli_u32_to_dec(&s_config_line_buffer[pos],
                              config->sample_period_s,
                              0U);
    s_config_line_buffer[pos] = '\0';
    (void)app_cli_print(s_config_line_buffer);

    pos = 0U;
    pos = app_cli_append_text(s_config_line_buffer, pos, "ch0_ratio=");
    pos = app_cli_append_fixed2(s_config_line_buffer, pos,
                                config->ratio[0]);
    pos = app_cli_append_text(s_config_line_buffer, pos, " ch0_limit=");
    pos = app_cli_append_fixed2(s_config_line_buffer, pos,
                                config->limit[0]);
    s_config_line_buffer[pos] = '\0';
    (void)app_cli_print(s_config_line_buffer);

    pos = 0U;
    pos = app_cli_append_text(s_config_line_buffer, pos, "ch1_ratio=");
    pos = app_cli_append_fixed2(s_config_line_buffer, pos,
                                config->ratio[1]);
    pos = app_cli_append_text(s_config_line_buffer, pos, " ch1_limit=");
    pos = app_cli_append_fixed2(s_config_line_buffer, pos,
                                config->limit[1]);
    s_config_line_buffer[pos] = '\0';
    (void)app_cli_print(s_config_line_buffer);

    pos = 0U;
    pos = app_cli_append_text(s_config_line_buffer, pos,
                              "protocol_mode=");
    pos += app_cli_u32_to_dec(&s_config_line_buffer[pos],
                              config->protocol_mode,
                              0U);
    pos = app_cli_append_text(s_config_line_buffer, pos, " alarm_mode=");
    pos += app_cli_u32_to_dec(&s_config_line_buffer[pos],
                              config->alarm_mode,
                              0U);
    s_config_line_buffer[pos] = '\0';
    (void)app_cli_print(s_config_line_buffer);

    pos = 0U;
    pos = app_cli_append_text(s_config_line_buffer, pos, "baudrate=");
    pos += app_cli_u32_to_dec(&s_config_line_buffer[pos],
                              config->rs485_baudrate,
                              0U);
    s_config_line_buffer[pos] = '\0';
    (void)app_cli_print(s_config_line_buffer);
}

static uint32_t app_cli_storage_next_request_id(void)
{
    s_storage_request_sequence++;

    if (s_storage_request_sequence == 0U)
    {
        s_storage_request_sequence++;
    }

    return s_storage_request_sequence;
}

static cli_status_t app_cli_storage_request_start(
    uint8_t operation,
    char *output)
{
    app_config_t config;
    uint16_t payload_length;

    if (s_storage_pending_request_id != 0U)
    {
        (void)strcpy(output, "storage busy");
        return CLI_STATUS_OK;
    }

    (void)memset(&s_storage_request, 0, sizeof(s_storage_request));
    s_storage_request.request_id = app_cli_storage_next_request_id();
    s_storage_request.deadline_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(APP_CLI_STORAGE_TIMEOUT_MS);
    s_storage_request.operation = operation;
    s_storage_request.origin = STORAGE_TASK_PERSIST_ORIGIN_CONTROL;

    if (operation == STORAGE_TASK_PERSIST_CONFIG_SAVE)
    {
        if ((app_config_get(&config) == 0) ||
            (app_config_encode(&config,
                               s_storage_request.payload,
                               sizeof(s_storage_request.payload),
                               &payload_length) == 0))
        {
            (void)strcpy(output, "config save invalid");
            return CLI_STATUS_OK;
        }

        s_storage_request.payload_length = payload_length;
        app_cli_print_config(&config);
    }

    if (storage_task_persist_request_submit(&s_storage_request) == 0)
    {
        (void)strcpy(output, "storage busy");
        return CLI_STATUS_OK;
    }

    s_storage_pending_request_id = s_storage_request.request_id;
    s_storage_pending_operation = operation;
    s_storage_pending_deadline = s_storage_request.deadline_tick;

    if (operation == STORAGE_TASK_PERSIST_CONFIG_SAVE)
    {
        (void)strcpy(output, "config save pending");
    }
    else
    {
        (void)strcpy(output, (operation == STORAGE_TASK_PERSIST_CONFIG_IMPORT) ?
                     "config import pending" : "config read pending");
    }

    return CLI_STATUS_OK;
}

static void app_cli_print_storage_error(uint8_t operation, uint8_t status)
{
    const char *operation_text;
    const char *status_text;
    uint16_t pos;

    operation_text = (operation == STORAGE_TASK_PERSIST_CONFIG_SAVE) ?
                     "config save" : ((operation == STORAGE_TASK_PERSIST_CONFIG_IMPORT) ?
                     "config import" : "config read");

    switch (status)
    {
    case STORAGE_TASK_PERSIST_STATUS_NOT_FOUND:
        status_text = "not found";
        break;

    case STORAGE_TASK_PERSIST_STATUS_NOT_READY:
        status_text = "not ready";
        break;

    case STORAGE_TASK_PERSIST_STATUS_FLASH_ERROR:
        status_text = "flash error";
        break;

    case STORAGE_TASK_PERSIST_STATUS_DATA_ERROR:
        status_text = "data error";
        break;

    default:
        status_text = "failed";
        break;
    }

    pos = 0U;
    pos = app_cli_append_text(s_config_line_buffer, pos, operation_text);
    pos = app_cli_append_text(s_config_line_buffer, pos, ": ");
    pos = app_cli_append_text(s_config_line_buffer, pos, status_text);
    s_config_line_buffer[pos] = '\0';
    (void)app_cli_print(s_config_line_buffer);
}

/* "ch0 ratio set to 5.50 [OK]" */
static void app_cli_print_channel_ok(uint8_t channel, const char *what, float value)
{
    char buf[48];
    uint16_t pos = 0U;

    buf[pos] = 'c';
    buf[pos + 1U] = 'h';
    buf[pos + 2U] = (char)('0' + channel);
    buf[pos + 3U] = ' ';
    pos += 4U;

    while (*what != '\0')
    {
        buf[pos] = *what;
        pos++;
        what++;
    }

    buf[pos] = ' ';
    pos++;

    pos = app_cli_append_fixed2(buf, pos, value);

    (void)strcpy(&buf[pos], " [OK]");

    (void)app_cli_print(buf);
}

static void app_cli_apply_ratio(const char *line)
{
    float value;

    if ((app_cli_parse_fixed(line, &value) == 0) ||
        (app_config_ratio_set(s_pending_channel, value) == 0))
    {
        (void)app_cli_print("parameter invalid, ratio unchanged");
        return;
    }

    (void)sample_task_ratio_set(s_pending_channel, value);

    app_cli_print_channel_ok(s_pending_channel, "ratio set to", value);
}

static void app_cli_apply_limit(const char *line)
{
    float value;

    if ((app_cli_parse_fixed(line, &value) == 0) ||
        (app_config_limit_set(s_pending_channel, value) == 0))
    {
        (void)app_cli_print("parameter invalid, limit unchanged");
        return;
    }

    app_cli_print_channel_ok(s_pending_channel, "limit set to", value);
}

static void app_cli_apply_protocol(const char *line)
{
    uint8_t mode;

    if (((line[0] != '0') && (line[0] != '1')) || (line[1] != '\0'))
    {
        (void)app_cli_print("parameter invalid, protocol unchanged");
        return;
    }

    mode = (uint8_t)(line[0] - '0');

    if (app_config_protocol_mode_set(mode) == 0)
    {
        (void)app_cli_print("parameter invalid, protocol unchanged");
        return;
    }

    if (mode == 0U)
    {
        (void)app_cli_print("protocol set to 0 (custom)");
    }
    else
    {
        (void)app_cli_print("protocol set to 1 (modbus)");
    }
}

static void app_cli_apply_id(const char *line)
{
    uint16_t value;

    if ((app_cli_parse_hex4(line, &value) == 0) ||
        (app_config_device_id_set(value) == 0))
    {
        (void)app_cli_print("parameter invalid, id unchanged");
        return;
    }

    /* 文档原文带 ", saved [OK]"，持久化在 M5，M4 阶段不打印 saved */
    app_cli_print_current_hex4("device id set to ", value);
}

static void app_cli_apply_baud(const char *line)
{
    uint32_t value;

    if ((app_cli_parse_u32(line, &value) == 0) ||
        (app_config_baudrate_set(value) == 0))
    {
        (void)app_cli_print("parameter invalid, baudrate unchanged");
        return;
    }

    board_usart1_rs485_baudrate_set(value);

    app_cli_print_current_dec("baudrate set to ", value);
}

/*
 * 时间解析（《01》二-2）：分隔符无关，抓满 14 个数字
 * YYYY MM DD HH MM SS；范围校验在 board_rtc_time_set 里做。
 */
static int app_cli_parse_time(const char *line, board_rtc_time_t *out)
{
    uint8_t digits[14];
    uint8_t count = 0U;
    const char *p = line;

    if (line == NULL)
    {
        return 0;
    }

    while (*p != '\0')
    {
        if ((*p >= '0') && (*p <= '9'))
        {
            if (count >= 14U)
            {
                return 0;
            }

            digits[count] = (uint8_t)(*p - '0');
            count++;
        }

        p++;
    }

    if (count != 14U)
    {
        return 0;
    }

    out->year = (uint16_t)((digits[0] * 1000U) + (digits[1] * 100U) +
                           (digits[2] * 10U) + digits[3]);
    out->month = (uint8_t)((digits[4] * 10U) + digits[5]);
    out->date = (uint8_t)((digits[6] * 10U) + digits[7]);
    out->hour = (uint8_t)((digits[8] * 10U) + digits[9]);
    out->minute = (uint8_t)((digits[10] * 10U) + digits[11]);
    out->second = (uint8_t)((digits[12] * 10U) + digits[13]);

    return 1;
}

static void app_cli_apply_time(const char *line)
{
    char buf[40];
    board_rtc_time_t time;
    uint16_t pos = 0U;

    if ((app_cli_parse_time(line, &time) == 0) ||
        (board_rtc_time_set(&time) == 0))
    {
        (void)app_cli_print("RTC set failed: invalid time format");
        return;
    }

    (void)strcpy(buf, "RTC set success: ");
    pos = (uint16_t)strlen(buf);
    pos = app_cli_append_time(buf, pos, &time);
    buf[pos] = '\0';

    (void)app_cli_print(buf);
}

static void app_cli_print_rtc_time(const board_rtc_time_t *time)
{
    char buf[24];
    uint16_t pos;

    pos = app_cli_append_time(buf, 0U, time);
    buf[pos] = '\0';

    (void)app_cli_print(buf);
}

static void app_cli_pending_value_process(const char *line)
{
    if (line[0] == '\0')
    {
        (void)app_cli_print("input cancelled");
        return;
    }

    switch (s_pending_mode)
    {
    case APP_CLI_INPUT_RATIO:
        app_cli_apply_ratio(line);
        break;

    case APP_CLI_INPUT_LIMIT:
        app_cli_apply_limit(line);
        break;

    case APP_CLI_INPUT_PROTOCOL:
        app_cli_apply_protocol(line);
        break;

    case APP_CLI_INPUT_ID:
        app_cli_apply_id(line);
        break;

    case APP_CLI_INPUT_BAUD:
        app_cli_apply_baud(line);
        break;

    case APP_CLI_INPUT_TIME:
        app_cli_apply_time(line);
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 命令 handler                                                        */
/* ------------------------------------------------------------------ */

/*
 * 约定：短回复（一两行）写进 output 缓冲由这里统一打印；
 * help 这类长文本直接 app_cli_write 流式输出，output 保持空。
 */
static cli_status_t app_cli_help(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argc;
    (void)argv;
    (void)output;
    (void)output_size;

    (void)app_cli_write(s_cli_help_text, (uint16_t)(sizeof(s_cli_help_text) - 1U));

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_version(int argc, const char *argv[],
                                    char *output, size_t output_size)
{
    (void)argc;
    (void)argv;

    if (strlen(s_cli_version_text) >= output_size)
    {
        return CLI_STATUS_OUTPUT_TOO_SMALL;
    }

    (void)strcpy(output, s_cli_version_text);

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_start(int argc, const char *argv[],
                                  char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_sample_enable_set(1U);

    (void)strcpy(output, "sampling started");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_stop(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_sample_enable_set(0U);

    (void)strcpy(output, "sampling stopped");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_hide(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_hide_mode_set(1U);

    (void)strcpy(output, "hide mode on");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_unhide(int argc, const char *argv[],
                                   char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_hide_mode_set(0U);

    (void)strcpy(output, "hide mode off");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_ratio(int argc, const char *argv[],
                                  char *output, size_t output_size)
{
    app_config_t config;
    uint8_t channel;

    (void)output;
    (void)output_size;

    if (argc != 2)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    channel = app_cli_channel_parse(argv[1]);

    if (channel > 1U)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_get(&config);

    app_cli_print_current_fixed2(
        (channel == 0U) ? "current ch0 ratio: " : "current ch1 ratio: ",
        config.ratio[channel]);

    (void)app_cli_print("please input new ratio (0-100):");

    s_pending_mode = APP_CLI_INPUT_RATIO;
    s_pending_channel = channel;

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_limit(int argc, const char *argv[],
                                  char *output, size_t output_size)
{
    app_config_t config;
    uint8_t channel;

    (void)output;
    (void)output_size;

    if (argc != 2)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    channel = app_cli_channel_parse(argv[1]);

    if (channel > 1U)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_get(&config);

    app_cli_print_current_fixed2(
        (channel == 0U) ? "current ch0 limit: " : "current ch1 limit: ",
        config.limit[channel]);

    (void)app_cli_print("please input new limit (0-500):");

    s_pending_mode = APP_CLI_INPUT_LIMIT;
    s_pending_channel = channel;

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_protocol(int argc, const char *argv[],
                                     char *output, size_t output_size)
{
    app_config_t config;

    (void)argv;
    (void)output;
    (void)output_size;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_get(&config);

    if (config.protocol_mode == 0U)
    {
        (void)app_cli_print("current protocol: 0 (custom)");
    }
    else
    {
        (void)app_cli_print("current protocol: 1 (modbus)");
    }

    (void)app_cli_print("please input protocol mode (0=custom, 1=modbus):");

    s_pending_mode = APP_CLI_INPUT_PROTOCOL;

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_id(int argc, const char *argv[],
                               char *output, size_t output_size)
{
    app_config_t config;

    (void)argv;
    (void)output;
    (void)output_size;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_get(&config);

    app_cli_print_current_hex4("current device id: ", config.device_id);

    (void)app_cli_print("please input new id (0001-FFFE):");

    s_pending_mode = APP_CLI_INPUT_ID;

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_baud(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    app_config_t config;

    (void)argv;
    (void)output;
    (void)output_size;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_get(&config);

    app_cli_print_current_dec("current baudrate: ", config.rs485_baudrate);

    (void)app_cli_print("please input baudrate (9600/19200/38400/57600/115200):");

    s_pending_mode = APP_CLI_INPUT_BAUD;

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_config(int argc, const char *argv[],
                                   char *output, size_t output_size)
{
    (void)output_size;

    if (argc != 2)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    if (strcmp(argv[1], "save") == 0)
    {
        return app_cli_storage_request_start(
            STORAGE_TASK_PERSIST_CONFIG_SAVE,
            output);
    }

    if (strcmp(argv[1], "read") == 0)
    {
        return app_cli_storage_request_start(
            STORAGE_TASK_PERSIST_CONFIG_READ,
            output);
    }

    if (strcmp(argv[1], "import") == 0)
    {
        return app_cli_storage_request_start(
            STORAGE_TASK_PERSIST_CONFIG_IMPORT,
            output);
    }

    return CLI_STATUS_INVALID_ARGUMENTS;
}

/* conf 直接触发 TF 卡 config.ini 导入；结果仍由 ControlTask 轮询应用。 */
static cli_status_t app_cli_conf(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argv;
    (void)output_size;
    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }
    return app_cli_storage_request_start(
        STORAGE_TASK_PERSIST_CONFIG_IMPORT, output);
}

static cli_status_t app_cli_rtc(int argc, const char *argv[],
                                char *output, size_t output_size)
{
    board_rtc_time_t time;

    (void)output;
    (void)output_size;

    if (argc != 2)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    if (strcmp(argv[1], "now") == 0)
    {
        if (board_rtc_time_get(&time) == 0)
        {
            (void)app_cli_print("rtc not ready");
        }
        else
        {
            app_cli_print_rtc_time(&time);
        }

        return CLI_STATUS_OK;
    }

    if (strcmp(argv[1], "config") == 0)
    {
        (void)app_cli_print("Please input time (YYYY-MM-DD HH:MM:SS):");
        s_pending_mode = APP_CLI_INPUT_TIME;

        return CLI_STATUS_OK;
    }

    return CLI_STATUS_INVALID_ARGUMENTS;
}

/*
 * 《01》二-1 四项自检。TF 只读 StorageTask 的诊断状态（文件系统
 * 所有权在 StorageTask）；OLED 启动即校验过，能走到这里说明已通过。
 */
static cli_status_t app_cli_test(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    storage_task_sdio_diag_t diag;
    board_rtc_time_t time;
    uint8_t jedec_id[3];
    char buf[40];
    uint16_t pos;
    uint8_t flash_pass = 0U;
    uint8_t tf_pass = 0U;
    uint8_t rtc_pass = 0U;
    uint8_t all_pass;

    (void)argv;
    (void)output;
    (void)output_size;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_cli_print("=== System Test ===");

    /* Flash: GD25Q40E JEDEC 预期 0xC84013 */
    if (board_spi_flash_read_jedec_id(jedec_id) != 0)
    {
        flash_pass = ((jedec_id[0] == 0xC8U) &&
                      (jedec_id[1] == 0x40U) &&
                      (jedec_id[2] == 0x13U)) ? 1U : 0U;
    }

    (void)app_cli_print((flash_pass != 0U) ?
        "Flash ID   : 0xC84013  [PASS]" : "Flash ID   : --------  [FAIL]");

    if (storage_task_sdio_diag_get(&diag) != 0)
    {
        tf_pass = (diag.state == STORAGE_TASK_SDIO_STATE_READY) ? 1U : 0U;
    }

    (void)app_cli_print((tf_pass != 0U) ?
        "TF Card    : Found     [PASS]" : "TF Card    : Not Found [FAIL]");

    (void)app_cli_print("OLED       : OK        [PASS]");

    if (board_rtc_time_get(&time) == 0)
    {
        (void)app_cli_print("RTC        : Not Ready  [FAIL]");
    }
    else
    {
        /* 判定标准：非 2000-01-01 00:00:00 默认值（《01》二-1） */
        rtc_pass = ((time.year != 2000U) || (time.month != 1U) ||
                    (time.date != 1U) || (time.hour != 0U) ||
                    (time.minute != 0U) || (time.second != 0U)) ? 1U : 0U;

        (void)strcpy(buf, "RTC        : ");
        pos = (uint16_t)strlen(buf);
        pos = app_cli_append_time(buf, pos, &time);
        (void)strcpy(&buf[pos], (rtc_pass != 0U) ? " [PASS]" : " [FAIL]");
        (void)app_cli_print(buf);
    }

    all_pass = (uint8_t)(flash_pass & tf_pass & rtc_pass);

    (void)app_cli_print((all_pass != 0U) ?
        "=== Test Result: PASS ===" : "=== Test Result: FAIL ===");

    return CLI_STATUS_OK;
}

void app_cli_storage_result_poll(void)
{
    app_config_t config;

    if (s_storage_pending_request_id == 0U)
    {
        return;
    }

    while (storage_task_persist_result_get(&s_storage_result, 0U) != 0)
    {
        if ((s_storage_result.request_id != s_storage_pending_request_id) ||
            (s_storage_result.operation != s_storage_pending_operation))
        {
            /* 丢弃迟到或不属于当前 CLI 请求的完成消息。 */
            continue;
        }

        s_storage_pending_request_id = 0U;

        if (s_storage_result.status != STORAGE_TASK_PERSIST_STATUS_OK)
        {
            app_cli_print_storage_error(s_storage_pending_operation,
                                         s_storage_result.status);
            return;
        }

        if (s_storage_pending_operation == STORAGE_TASK_PERSIST_CONFIG_SAVE)
        {
            (void)app_cli_print("save to flash [OK]");
            return;
        }

        if (s_storage_pending_operation == STORAGE_TASK_PERSIST_CONFIG_IMPORT)
        {
            if (control_apply_persisted_config(&s_storage_result) == 0U)
            {
                (void)app_cli_print("config import: apply error");
                return;
            }
            (void)xEventGroupSetBits(task_events_get(), TASK_EVENT_CONFIG_READY);
            (void)app_cli_print("config.ini loaded and saved [OK]");
            return;
        }

        if ((s_storage_result.payload_length != APP_CONFIG_SERIALIZED_SIZE) ||
            (app_config_decode(s_storage_result.payload,
                               s_storage_result.payload_length,
                               &config) == 0))
        {
            (void)app_cli_print("config read: data error");
            return;
        }

        app_cli_print_config(&config);
        return;
    }

    if ((int32_t)(xTaskGetTickCount() - s_storage_pending_deadline) >= 0)
    {
        s_storage_pending_request_id = 0U;
        (void)app_cli_print("storage operation timeout");
    }
}

static const cli_command_t s_cli_commands[] =
{
    { "help",    app_cli_help },
    { "version", app_cli_version },
    { "start",   app_cli_start },
    { "stop",    app_cli_stop },
    { "hide",    app_cli_hide },
    { "unhide",  app_cli_unhide },
    { "ratio",   app_cli_ratio },
    { "limit",   app_cli_limit },
    { "protocol", app_cli_protocol },
    { "id",      app_cli_id },
    { "baud",    app_cli_baud },
    { "config",  app_cli_config },
    { "conf",    app_cli_conf },
    { "rtc",     app_cli_rtc },
    { "test",    app_cli_test }
};

#define APP_CLI_COMMAND_COUNT \
    (sizeof(s_cli_commands) / sizeof(s_cli_commands[0]))

void app_cli_execute_line(const char *line)
{
    char output[APP_CLI_OUTPUT_BUFFER_SIZE];
    cli_status_t status;

    if (s_pending_mode != APP_CLI_INPUT_NONE)
    {
        app_cli_pending_value_process(line);
        s_pending_mode = APP_CLI_INPUT_NONE;
        return;
    }

    output[0] = '\0';    /* handler 可能不写 output（如 help 直接流式输出），先置空串 */

    status = cli_execute_line(s_cli_commands, APP_CLI_COMMAND_COUNT,
                              line, output, sizeof(output));

    switch (status)
    {
    case CLI_STATUS_OK:
        if (output[0] != '\0')
        {
            (void)app_cli_print(output);
        }
        break;

    case CLI_STATUS_UNKNOWN_COMMAND:
        (void)app_cli_write("unknown command: ", 17U);
        (void)app_cli_print(line);
        break;

    case CLI_STATUS_LINE_TOO_LONG:
        (void)app_cli_print("line too long");
        break;

    case CLI_STATUS_INVALID_ARGUMENTS:
        (void)app_cli_print("invalid arguments");
        break;

    case CLI_STATUS_OUTPUT_TOO_SMALL:
        (void)app_cli_print("output buffer too small");
        break;

    case CLI_STATUS_NULL_POINTER:
    case CLI_STATUS_EMPTY_LINE:
    default:
        /* EMPTY_LINE 到不了：空行在 ControlTask 已过滤 */
        (void)app_cli_print("cli error");
        break;
    }
}
