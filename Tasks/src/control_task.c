#include "control_task.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#include "task_queues.h"
#include "app_cli.h"
#include "app_config.h"
#include "app_protocol.h"
#include "app_modbus.h"
#include "storage_task.h"
#include "sample_task.h"
#include "board_usart.h"
#include "board_gpio.h"
#include "board_rtc.h"
#include "cli.h"
#include "ebtn.h"

#define CONTROL_TASK_PRIORITY          3U
#define CONTROL_TASK_STACK_DEPTH       256U

/* 《01》四-4：超限指示灯 LED3 */
#define CONTROL_LED_OVER_LIMIT         3U

#define CLI_ECHO_ENABLE                1U

#define CLI_CHAR_BACKSPACE             0x08U
#define CLI_CHAR_TAB                   0x09U
#define CLI_CHAR_CR                    0x0DU
#define CLI_CHAR_LF                    0x0AU
#define CLI_CHAR_SPACE                 0x20U
#define CLI_CHAR_TILDE                 0x7EU
#define CLI_CHAR_DELETE                0x7FU

#define SAMPLE_LINE_BUFFER_SIZE        128U

static StaticTask_t s_control_task_tcb;
static StackType_t  s_control_task_stack[CONTROL_TASK_STACK_DEPTH];

static volatile uint32_t s_control_task_heartbeat;
static volatile uint32_t s_control_task_stack_high_water_mark;

/*
 * 行缓冲放静态区而不放栈上：129B 加上 cli_execute_line 内部
 * 自己的 line_copy 和 argv，栈占用会翻倍，ControlTask 只有 1KB。
 */
static char     s_cli_line_buffer[CLI_MAX_LINE_LENGTH + 1U];
static uint16_t s_cli_line_length;
static uint8_t  s_cli_line_overflow;
static uint8_t  s_cli_swallow_next_lf;

static char s_sample_line_buffer[SAMPLE_LINE_BUFFER_SIZE];

static void cli_line_terminate(void)
{
#if (CLI_ECHO_ENABLE != 0U)
    (void)app_cli_write("\r\n", 2U);
#endif

    if (s_cli_line_overflow != 0U)
    {
        (void)app_cli_print("line too long, discarded");
    }
    else if (s_cli_line_length > 0U)
    {
        s_cli_line_buffer[s_cli_line_length] = '\0';

        app_cli_execute_line(s_cli_line_buffer);
    }
    else
    {
        /* 空行不执行命令 */
    }

    s_cli_line_length = 0U;
    s_cli_line_overflow = 0U;
}

static void cli_line_process_byte(uint8_t byte)
{
    if (byte == CLI_CHAR_CR)
    {
        s_cli_swallow_next_lf = 1U;

        cli_line_terminate();

        return;
    }

    if (byte == CLI_CHAR_LF)
    {
        if (s_cli_swallow_next_lf != 0U)
        {
            /* CRLF 的后半，吞掉避免产生第二个空行 */
            s_cli_swallow_next_lf = 0U;

            return;
        }

        cli_line_terminate();

        return;
    }

    /* CRLF 序列被其他字节打断，作废 */
    s_cli_swallow_next_lf = 0U;

    if ((byte == CLI_CHAR_BACKSPACE) || (byte == CLI_CHAR_DELETE))
    {
        if (s_cli_line_length > 0U)
        {
            s_cli_line_length--;

#if (CLI_ECHO_ENABLE != 0U)
            (void)app_cli_write("\b \b", 3U);
#endif
        }

        return;
    }

    if ((byte == CLI_CHAR_TAB) ||
        ((byte >= CLI_CHAR_SPACE) && (byte <= CLI_CHAR_TILDE)))
    {
#if (CLI_ECHO_ENABLE != 0U)
        (void)app_cli_write((const char *)&byte, 1U);
#endif
        if (s_cli_line_length >= CLI_MAX_LINE_LENGTH)
        {
            /* 继续收完这一行但不再存储，终止时整行报错丢弃 */
            s_cli_line_overflow = 1U;
        }
        else
        {
            s_cli_line_buffer[s_cli_line_length] = (char)byte;
            s_cli_line_length++;
        }

        return;
    }

    /* 其余控制字节忽略 */
}

static uint16_t control_append_voltage(char *dst, uint16_t pos,
                                       const char *name, float value)
{
    while (*name != '\0')
    {
        dst[pos] = *name;
        pos++;
        name++;
    }

    pos = app_cli_append_fixed2(dst, pos, value);

    dst[pos] = 'V';
    pos++;

    return pos;
}

/*
 * 正常格式（《01》四-1，时间戳 M4-3d 换 RTC）:
 *   t=000012  CH0=1.25V  CH1=3.30V[  [OverLimit] ch0>2.50]
 * 隐藏格式（《01》五，时间 4B + 每通道 4B，超限尾缀 *）:
 *   0000006E000C8000001A6666*
 */
static void control_print_sample_line(const app_config_t *config)
{
    sample_snapshot_t snapshot;
    board_rtc_time_t rtc_time;
    TickType_t now_ticks = xTaskGetTickCount();
    uint32_t uptime_s = (uint32_t)(now_ticks / (TickType_t)configTICK_RATE_HZ);
    uint32_t timestamp = uptime_s;
    uint16_t pos = 0U;
    uint8_t ch0_over;
    uint8_t ch1_over;
    uint8_t rtc_ok;

    if (sample_task_snapshot_get(&snapshot) == 0)
    {
        return;
    }

    rtc_ok = board_rtc_time_get(&rtc_time);

    if (rtc_ok != 0U)
    {
        timestamp = app_cli_time_to_unix(&rtc_time);
    }

    ch0_over = (snapshot.value_ch0 > config->limit[0]) ? 1U : 0U;
    ch1_over = (snapshot.value_ch1 > config->limit[1]) ? 1U : 0U;

    if (config->hide_mode == 0U)
    {
        if (rtc_ok != 0U)
        {
            pos = app_cli_append_time(s_sample_line_buffer, pos, &rtc_time);
        }
        else
        {
            pos += app_cli_u32_to_dec(&s_sample_line_buffer[pos], uptime_s, 0U);
        }

        /* 文档格式时间戳后两个空格："2026-08-09 12:30:45  CH0=1.25V" */
        s_sample_line_buffer[pos] = ' ';
        pos++;
        s_sample_line_buffer[pos] = ' ';
        pos++;

        pos = control_append_voltage(s_sample_line_buffer, pos, "CH0=", snapshot.value_ch0);
        s_sample_line_buffer[pos] = ' ';
        pos++;
        pos = control_append_voltage(s_sample_line_buffer, pos, "CH1=", snapshot.value_ch1);

        if ((ch0_over != 0U) || (ch1_over != 0U))
        {
            (void)strcpy(&s_sample_line_buffer[pos], "  [OverLimit]");
            pos += 13U;

            if (ch0_over != 0U)
            {
                (void)strcpy(&s_sample_line_buffer[pos], " ch0>");
                pos += 5U;
                pos = app_cli_append_fixed2(s_sample_line_buffer, pos, config->limit[0]);
            }

            if (ch1_over != 0U)
            {
                (void)strcpy(&s_sample_line_buffer[pos], " ch1>");
                pos += 5U;
                pos = app_cli_append_fixed2(s_sample_line_buffer, pos, config->limit[1]);
            }
        }
    }
    else
    {
        /* value*65536 的高 16 位即整数部分、低 16 位即小数（《01》五） */
        uint32_t ch0_scaled = (uint32_t)((snapshot.value_ch0 * 65536.0f) + 0.5f);
        uint32_t ch1_scaled = (uint32_t)((snapshot.value_ch1 * 65536.0f) + 0.5f);

        pos = app_cli_append_hex16(s_sample_line_buffer, pos,
                                   (uint16_t)(timestamp >> 16));
        pos = app_cli_append_hex16(s_sample_line_buffer, pos,
                                   (uint16_t)(timestamp & 0xFFFFU));
        pos = app_cli_append_hex16(s_sample_line_buffer, pos,
                                   (uint16_t)(ch0_scaled >> 16));
        pos = app_cli_append_hex16(s_sample_line_buffer, pos,
                                   (uint16_t)(ch0_scaled & 0xFFFFU));
        pos = app_cli_append_hex16(s_sample_line_buffer, pos,
                                   (uint16_t)(ch1_scaled >> 16));
        pos = app_cli_append_hex16(s_sample_line_buffer, pos,
                                   (uint16_t)(ch1_scaled & 0xFFFFU));

        if ((ch0_over != 0U) || (ch1_over != 0U))
        {
            s_sample_line_buffer[pos] = '*';
            pos++;
        }
    }

    board_led_set(CONTROL_LED_OVER_LIMIT, (uint8_t)(((ch0_over != 0U) || (ch1_over != 0U)) ? 1U : 0U));

    s_sample_line_buffer[pos] = '\r';
    s_sample_line_buffer[pos + 1U] = '\n';

    (void)app_cli_write(s_sample_line_buffer, (uint16_t)(pos + 2U));
}

static void control_execute_modbus_request(
    const protocol_request_t *request,
    protocol_result_t *result)
{
    modbus_request_t modbus_request;
    app_modbus_result_t modbus_result;
    uint16_t copy_length;

    (void)memset(&modbus_request, 0, sizeof(modbus_request));

    modbus_request.address =
        (uint8_t)request->device_address;
    modbus_request.function =
        (uint8_t)request->operation;
    modbus_request.exception =
        request->decode_exception;
    modbus_request.start_address =
        request->modbus_start_address;
    modbus_request.quantity =
        request->modbus_quantity;
    modbus_request.value =
        request->modbus_value;

    if ((request->operation == MODBUS_FUNCTION_WRITE_MULTIPLE) &&
        (request->payload_length > 0U) &&
        (request->payload_length <= 255U))
    {
        modbus_request.write_data = request->payload;
        modbus_request.write_byte_count =
            (uint8_t)request->payload_length;
    }
    else
    {
        modbus_request.write_data = NULL;
        modbus_request.write_byte_count = 0U;
    }

    app_modbus_execute(&modbus_request, &modbus_result);

    (void)memset(result, 0, sizeof(*result));

    result->request_id = request->request_id;
    result->mode_epoch = request->mode_epoch;
    result->device_address = request->device_address;
    result->operation = request->operation;
    result->protocol_sequence = request->protocol_sequence;
    result->protocol_kind = PROTOCOL_KIND_MODBUS;
    result->reply_required = modbus_result.reply_required;
    result->status = modbus_result.exception;
    result->next_device_id = modbus_result.next_device_id;
    result->next_baudrate = modbus_result.next_baudrate;
    result->apply_flags = PROTOCOL_RESULT_APPLY_NONE;

    if ((modbus_result.apply_flags & APP_MODBUS_APPLY_ID) != 0U)
    {
        result->apply_flags |= PROTOCOL_RESULT_APPLY_ID;
    }

    if ((modbus_result.apply_flags & APP_MODBUS_APPLY_BAUD) != 0U)
    {
        result->apply_flags |= PROTOCOL_RESULT_APPLY_BAUD;
    }

    if (modbus_result.data_length > sizeof(result->payload))
    {
        result->status = MODBUS_EXCEPTION_BUSY;
        result->payload_length = 0U;
        result->apply_flags = PROTOCOL_RESULT_APPLY_NONE;
        return;
    }

    copy_length = (uint16_t)modbus_result.data_length;
    result->payload_length = copy_length;

    if (copy_length > 0U)
    {
        (void)memcpy(result->payload,
                     modbus_result.data,
                     copy_length);
    }
}

static void control_execute_protocol_request(
    const protocol_request_t *request,
    protocol_result_t *result)
{
    if (request->protocol_kind == PROTOCOL_KIND_MODBUS)
    {
        control_execute_modbus_request(request, result);
    }
    else
    {
        app_protocol_execute(request, result);
    }
}

static void control_task(void *argument)
{
    key_event_t key_event;
    app_config_t config;
    /* ControlTask 独占，静态存放扩容后的队列对象。 */
    static protocol_request_t protocol_request;
    static protocol_result_t protocol_result;
    uint8_t sample_was_enabled = 0U;
    TickType_t last_print_tick = 0U;
    uint8_t byte;

    (void)argument;

    app_cli_banner_print();

    for(;;)
    {
        while (board_usart0_try_receive_byte(&byte) != 0U)
        {
            cli_line_process_byte(byte);
        }

        (void)app_config_get(&config);

        if (config.local_sample_enabled != 0U)
        {
            TickType_t now = xTaskGetTickCount();
            TickType_t period_ticks =
                (TickType_t)config.sample_period_s * (TickType_t)configTICK_RATE_HZ;

            if ((sample_was_enabled == 0U) ||
                ((now - last_print_tick) >= period_ticks))
            {
                control_print_sample_line(&config);
                last_print_tick = now;
            }
        }
        else if (sample_was_enabled != 0U)
        {
            /* 停止采样时熄灭超限灯 */
            board_led_set(CONTROL_LED_OVER_LIMIT, 0U);
        }

        sample_was_enabled = config.local_sample_enabled;

        if (key_event_receive(&key_event, 0U) == pdTRUE)
        {
            /* 《01》四-2：KEY1 按下翻转采样 */
            if ((key_event.key_id == 1U) && (key_event.event == (uint8_t)EBTN_EVT_ONPRESS))
            {
                (void)app_config_sample_enable_set(
                    (config.local_sample_enabled != 0U) ? 0U : 1U);
            }
            else if ((key_event.event == (uint8_t)EBTN_EVT_ONPRESS) &&
                     (key_event.key_id >= 2U) &&
                     (key_event.key_id <= 4U))
            {
                if (app_config_sample_period_key_set(key_event.key_id) != 0)
                {
                    if (key_event.key_id == 2U)
                    {
                        (void)app_cli_print("sample period set to 5s");
                    }
                    else if (key_event.key_id == 3U)
                    {
                        (void)app_cli_print("sample period set to 10s");
                    }
                    else
                    {
                        (void)app_cli_print("sample period set to 15s");
                    }
                }
            }
        }

        if (protocol_request_receive(&protocol_request, 0U) == pdTRUE)
        {
            control_execute_protocol_request(
                &protocol_request,
                &protocol_result);

            (void)protocol_result_send(&protocol_result);
        }

        /* LED2 采集工作灯：采样或自动上报任一进行中即亮（十一-1） */
        board_led_set(2U,
            (uint8_t)(((config.local_sample_enabled != 0U) ||
                       (app_protocol_auto_report_enabled() != 0U)) ? 1U : 0U));

        /* LED1 系统灯 1s 闪烁；LED5 TF 挂载常亮——每秒刷一次省总线 */
        {
            static uint32_t s_led_last_second = 0xFFFFFFFFU;
            uint32_t seconds = (uint32_t)(xTaskGetTickCount() /
                               (TickType_t)configTICK_RATE_HZ);

            if (seconds != s_led_last_second)
            {
                storage_task_sdio_diag_t diag;
                uint8_t tf_ok = 0U;

                s_led_last_second = seconds;

                board_led_set(1U, (uint8_t)((seconds % 2U) != 0U));

                if (storage_task_sdio_diag_get(&diag) != 0)
                {
                    tf_ok = (diag.state == STORAGE_TASK_SDIO_STATE_READY) ? 1U : 0U;
                }

                board_led_set(5U, tf_ok);
            }
        }

        s_control_task_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_control_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int control_task_create(void)
{
    TaskHandle_t control_task_handle;

    control_task_handle = xTaskCreateStatic(
        control_task,
        "Control",
        CONTROL_TASK_STACK_DEPTH,
        NULL,
        CONTROL_TASK_PRIORITY,
        s_control_task_stack,
        &s_control_task_tcb
    );

    if (control_task_handle == NULL)
    {
        return 0;
    }

    return 1;
}

uint32_t control_task_get_heartbeat(void)
{
    return s_control_task_heartbeat;
}

uint32_t control_task_get_stack_high_water_mark(void)
{
    return s_control_task_stack_high_water_mark;
}
