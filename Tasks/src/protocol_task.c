#include "protocol_task.h"
#include <string.h>

#include "gd32f4xx.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_cli.h"
#include "app_protocol.h"
#include "board_usart.h"
#include "board_rtc.h"
#include "protocol_frame.h"
#include "modbus_rtu.h"
#include "protocol_stream.h"
#include "sample_task.h"
#include "alarm_task.h"
#include "task_queues.h"
#include "task_events.h"

#define PROTOCOL_TASK_PRIORITY          5U
#define PROTOCOL_TASK_STACK_DEPTH       256U

/* ControlTask 10ms 轮询，完成应答远快于此；超时视为异常路径 */
#define PROTOCOL_COMPLETION_TIMEOUT_MS  500U

/* 应答帧数据区 = 状态字节 + 结果数据 */
#define PROTOCOL_TX_PAYLOAD_MAX         (PROTOCOL_RESULT_DATA_MAX + 1U)

#define PROTOCOL_CACHE_SIZE             128U

#define PROTOCOL_RESPONSE_OK_BYTE       0xFFU
 
#define PROTOCOL_ORIGIN_RS485           0x01U
#define PROTOCOL_ORIGIN_BROADCAST       0x02U

/* 《01》十四 K-01/K-02：CRC 错/长度错要回错误应答帧 */
#define PROTOCOL_BROADCAST_ADDRESS      0xFFFFU

/* 解析器固定头长度（帧头..数据长度字段，与 protocol_stream.c 一致） */
#define PROTOCOL_STREAM_HEADER_SIZE     12U

/* M4-4d：自动上报与心跳 */
#define PROTOCOL_EVENT_CMD_REPORT_DATA     0x0382U
#define PROTOCOL_EVENT_CMD_HEARTBEAT       0x8888U
#define PROTOCOL_HEARTBEAT_PERIOD_MS       30000U
#define PROTOCOL_HEARTBEAT_FIRST_DELAY_MS  500U

#define PROTOCOL_MODBUS_RX_BUFFER_SIZE     MODBUS_RTU_MAX_ADU_SIZE
#define PROTOCOL_ACTIVE_KIND_INVALID       0xFFU

static StaticTask_t s_protocol_task_tcb;
static StackType_t  s_protocol_task_stack[PROTOCOL_TASK_STACK_DEPTH];

static volatile uint32_t s_protocol_task_heartbeat;
static volatile uint32_t s_protocol_task_stack_high_water_mark;

static protocol_stream_t s_protocol_stream;

static protocol_stream_bad_t s_protocol_bad;
 
static uint8_t s_tx_buffer[PROTOCOL_MAX_FRAME_SIZE];

static uint8_t  s_cache_valid;
static uint16_t s_cache_address;
static uint16_t s_cache_command;
static uint16_t s_cache_sequence;
static uint8_t  s_cache_frame[PROTOCOL_CACHE_SIZE];
static uint16_t s_cache_frame_length;
 
static uint32_t s_protocol_request_id;

static TickType_t s_report_last_tick;
static TickType_t s_heartbeat_next_tick;

/* Modbus 完整 ADU 的静态接收缓冲区，不放在 ProtocolTask 栈上。 */
static uint8_t s_modbus_rx_buffer[PROTOCOL_MODBUS_RX_BUFFER_SIZE];

static uint8_t s_protocol_active_kind = PROTOCOL_ACTIVE_KIND_INVALID;
static uint32_t s_protocol_mode_epoch;
static uint32_t s_protocol_applied_baudrate;

static void protocol_send_frame(const uint8_t *data, uint16_t length)
{
    board_usart1_rs485_send_buffer(data, length);
}

static void protocol_sync_mode(void);
static void protocol_process_modbus_frames(void);
static void protocol_process_modbus_request(
    const modbus_request_t *modbus_request);
static void protocol_send_modbus_exception(uint8_t address,
                                           uint8_t function,
                                           uint8_t exception);
static void protocol_send_modbus_result(
    const protocol_result_t *result);
static void protocol_apply_modbus_result(
    const protocol_result_t *result);
static void protocol_apply_custom_result(
    const protocol_result_t *result);
static BaseType_t protocol_result_receive_for_request(
    const protocol_request_t *request,
    protocol_result_t *result);

static void protocol_send_result(const protocol_frame_t *command, const protocol_result_t *result)
{
    protocol_frame_t response;
    uint8_t tx_payload[PROTOCOL_TX_PAYLOAD_MAX];
    uint16_t tx_payload_length;
    size_t encoded_length = 0U;

    tx_payload[0] = (result->status == 0U) ?
                    PROTOCOL_RESPONSE_OK_BYTE : result->status;

    /* result 是 const，截断用局部量，不改写调用方 */
    tx_payload_length = result->payload_length;

    if (tx_payload_length > (uint16_t)(PROTOCOL_TX_PAYLOAD_MAX - 1U))
    {
        tx_payload_length = (uint16_t)(PROTOCOL_TX_PAYLOAD_MAX - 1U);
    }

    (void)memcpy(&tx_payload[1], result->payload, tx_payload_length);

    response.device_address = command->device_address;
    response.frame_type = (result->status == 0U) ?
                          PROTOCOL_TYPE_RESPONSE : PROTOCOL_TYPE_ERROR;
    response.command = command->command;
    response.sequence = command->sequence;
    response.payload_length = (uint16_t)(tx_payload_length + 1U);
    response.payload = tx_payload;
 
    if (protocol_frame_encode(&response, s_tx_buffer, sizeof(s_tx_buffer),
                              &encoded_length) != PROTOCOL_STATUS_OK)
    {
        return;
    }
 
    protocol_send_frame(s_tx_buffer, (uint16_t)encoded_length);
 
    if (encoded_length <= PROTOCOL_CACHE_SIZE)
    {
        (void)memcpy(s_cache_frame, s_tx_buffer, encoded_length);
        s_cache_frame_length = (uint16_t)encoded_length;
        s_cache_address = command->device_address;
        s_cache_command = command->command;
        s_cache_sequence = command->sequence;
        s_cache_valid = 1U;
    }
    else
    {
        s_cache_valid = 0U;
    }
}

static void protocol_send_modbus_exception(uint8_t address,
                                           uint8_t function,
                                           uint8_t exception)
{
    size_t encoded_length;

    if (address == 0U)
    {
        return;
    }

    encoded_length = 0U;

    if (modbus_rtu_exception_encode(
            address,
            function,
            exception,
            s_tx_buffer,
            sizeof(s_tx_buffer),
            &encoded_length) == MODBUS_STATUS_OK)
    {
        protocol_send_frame(s_tx_buffer, (uint16_t)encoded_length);
    }
}

static void protocol_send_modbus_result(const protocol_result_t *result)
{
    size_t encoded_length;
    modbus_status_t status;

    if ((result->reply_required == 0U) ||
        (result->device_address == 0U))
    {
        return;
    }

    encoded_length = 0U;

    if (result->status != MODBUS_EXCEPTION_NONE)
    {
        status = modbus_rtu_exception_encode(
            (uint8_t)result->device_address,
            (uint8_t)result->operation,
            result->status,
            s_tx_buffer,
            sizeof(s_tx_buffer),
            &encoded_length);
    }
    else if (result->payload_length <= 255U)
    {
        status = modbus_rtu_encode(
            (uint8_t)result->device_address,
            (uint8_t)result->operation,
            result->payload,
            (uint8_t)result->payload_length,
            s_tx_buffer,
            sizeof(s_tx_buffer),
            &encoded_length);
    }
    else
    {
        status = MODBUS_STATUS_INVALID_LENGTH;
    }

    if (status == MODBUS_STATUS_OK)
    {
        protocol_send_frame(s_tx_buffer, (uint16_t)encoded_length);
    }
}

static void protocol_apply_modbus_result(const protocol_result_t *result)
{
    if ((result->status != MODBUS_EXCEPTION_NONE) ||
        (result->reply_required == 0U))
    {
        return;
    }

    if ((result->apply_flags & PROTOCOL_RESULT_APPLY_ID) != 0U)
    {
        (void)app_config_device_id_set(result->next_device_id);
    }

    if ((result->apply_flags & PROTOCOL_RESULT_APPLY_BAUD) != 0U)
    {
        if (app_config_baudrate_set(result->next_baudrate) != 0)
        {
            board_usart1_rs485_baudrate_set(result->next_baudrate);
        }
    }
}

static void protocol_apply_custom_result(const protocol_result_t *result)
{
    if ((result->status != 0U) ||
        (result->reply_required == 0U))
    {
        return;
    }

    if ((result->apply_flags & PROTOCOL_RESULT_APPLY_BAUD) != 0U)
    {
        board_usart1_rs485_baudrate_set(result->next_baudrate);
    }
}

static BaseType_t protocol_result_receive_for_request(
    const protocol_request_t *request,
    protocol_result_t *result)
{
    TickType_t now;
    TickType_t remaining;

    for (;;)
    {
        now = xTaskGetTickCount();

        if ((int32_t)(request->deadline_tick - now) <= 0)
        {
            return pdFALSE;
        }

        remaining = request->deadline_tick - now;

        if (protocol_result_receive(result, remaining) != pdTRUE)
        {
            return pdFALSE;
        }

        if ((result->request_id == request->request_id) &&
            (result->mode_epoch == request->mode_epoch) &&
            (result->protocol_kind == request->protocol_kind))
        {
            return pdTRUE;
        }
    }
}

static void protocol_process_modbus_request(
    const modbus_request_t *modbus_request)
{
    app_config_t config;
    static protocol_request_t request;
    static protocol_result_t result;

    if (app_config_get(&config) == 0)
    {
        return;
    }

    /* USART1 的通信参数只能由 ProtocolTask 应用。 */
    if (s_protocol_applied_baudrate != config.rs485_baudrate)
    {
        board_usart1_rs485_baudrate_set(config.rs485_baudrate);
        s_protocol_applied_baudrate = config.rs485_baudrate;
    }

    /* 非本机单播静默，广播地址 0 交给业务层处理。 */
    if ((modbus_request->address != 0U) &&
        (modbus_request->address != config.device_id))
    {
        return;
    }

    if (modbus_request->write_byte_count >
        sizeof(request.payload))
    {
        protocol_send_modbus_exception(
            modbus_request->address,
            modbus_request->function,
            MODBUS_EXCEPTION_VALUE);
        return;
    }

    (void)memset(&request, 0, sizeof(request));
    (void)memset(&result, 0, sizeof(result));

    request.request_id = ++s_protocol_request_id;
    request.mode_epoch = s_protocol_mode_epoch;
    request.deadline_tick = xTaskGetTickCount() +
                            pdMS_TO_TICKS(PROTOCOL_COMPLETION_TIMEOUT_MS);
    request.device_address = modbus_request->address;
    request.operation = modbus_request->function;
    request.modbus_start_address = modbus_request->start_address;
    request.modbus_quantity = modbus_request->quantity;
    request.modbus_value = modbus_request->value;
    request.protocol_sequence = 0U;
    request.protocol_kind = PROTOCOL_KIND_MODBUS;
    request.origin = (modbus_request->address == 0U) ?
                     PROTOCOL_ORIGIN_BROADCAST :
                     PROTOCOL_ORIGIN_RS485;
    request.decode_exception = modbus_request->exception;
    request.payload_length = modbus_request->write_byte_count;

    if ((modbus_request->write_data != NULL) &&
        (modbus_request->write_byte_count > 0U))
    {
        (void)memcpy(request.payload,
                     modbus_request->write_data,
                     modbus_request->write_byte_count);
    }

    if (protocol_request_send(&request) != pdTRUE)
    {
        protocol_send_modbus_exception(
            modbus_request->address,
            modbus_request->function,
            MODBUS_EXCEPTION_BUSY);
        return;
    }

    if (protocol_result_receive_for_request(&request, &result) != pdTRUE)
    {
        protocol_send_modbus_exception(
            modbus_request->address,
            modbus_request->function,
            MODBUS_EXCEPTION_BUSY);
        return;
    }

    protocol_send_modbus_result(&result);
    protocol_apply_modbus_result(&result);
}

static void protocol_process_modbus_frames(void)
{
    modbus_request_t modbus_request;
    uint16_t frame_length;
    uint8_t receive_status;
    modbus_status_t decode_status;

    for (;;)
    {
        frame_length = 0U;

        receive_status = board_usart1_rs485_rtu_frame_receive(
            s_modbus_rx_buffer,
            sizeof(s_modbus_rx_buffer),
            &frame_length);

        if (receive_status == BOARD_USART1_RS485_RX_FRAME_NONE)
        {
            return;
        }

        if (receive_status == BOARD_USART1_RS485_RX_FRAME_BUFFER_SMALL)
        {
            /* 当前缓冲区等于最大 RTU ADU，此状态只保留作防御。 */
            return;
        }

        if (receive_status != BOARD_USART1_RS485_RX_FRAME_READY)
        {
            /* GAP_ERROR / OVERFLOW：按 RTU 规则静默丢弃。 */
            continue;
        }

        decode_status = modbus_rtu_request_decode(
            s_modbus_rx_buffer,
            frame_length,
            &modbus_request);

        if (decode_status != MODBUS_STATUS_OK)
        {
            /* CRC、长度或非法地址错误不回异常应答。 */
            continue;
        }

        protocol_process_modbus_request(&modbus_request);
    }
}

static void protocol_sync_mode(void)
{
    app_config_t config;
    uint8_t desired_kind;
    TickType_t now;

    if (app_config_get(&config) == 0)
    {
        return;
    }

    desired_kind = (config.protocol_mode != 0U) ?
                   PROTOCOL_KIND_MODBUS :
                   PROTOCOL_KIND_CUSTOM;

    if ((desired_kind == PROTOCOL_KIND_MODBUS) &&
        ((config.device_id == 0U) ||
         (config.device_id > MODBUS_RTU_MAX_SLAVE_ADDRESS)))
    {
        /* Modbus 地址必须为 1~247，非法配置回到自定义模式。 */
        (void)app_config_protocol_mode_set(0U);
        desired_kind = PROTOCOL_KIND_CUSTOM;
    }

    if (desired_kind == s_protocol_active_kind)
    {
        return;
    }

    board_usart1_rs485_rtu_receive_enable(
        (desired_kind == PROTOCOL_KIND_MODBUS) ? 1U : 0U);

    /* 模式切换丢弃旧协议尚未消费的任务间事务。 */
    protocol_queues_reset();

    protocol_stream_init(&s_protocol_stream);
    s_cache_valid = 0U;
    app_protocol_auto_report_set(0U);

    s_protocol_mode_epoch++;
    s_protocol_active_kind = desired_kind;

    now = xTaskGetTickCount();
    s_report_last_tick = now;
    s_heartbeat_next_tick = now +
                            pdMS_TO_TICKS(PROTOCOL_HEARTBEAT_PERIOD_MS);
}

static void protocol_resend_cached(const protocol_frame_t *command)
{
    (void)command;
    protocol_send_frame(s_cache_frame, s_cache_frame_length);
}

static void protocol_send_error(uint16_t address, uint16_t command,
                                uint16_t sequence, uint8_t error_code)
{
    protocol_frame_t response;
    uint8_t tx_payload[1];
    size_t encoded_length = 0U;

    tx_payload[0] = error_code;

    response.device_address = address;
    response.frame_type = PROTOCOL_TYPE_ERROR;
    response.command = command;
    response.sequence = sequence;
    response.payload_length = 1U;
    response.payload = tx_payload;

    if (protocol_frame_encode(&response, s_tx_buffer, sizeof(s_tx_buffer),
                              &encoded_length) == PROTOCOL_STATUS_OK)
    {
        protocol_send_frame(s_tx_buffer, (uint16_t)encoded_length);
    }
}

/*
 * 事件帧（帧类型 05）：设备主动发起，无应答、不进重复帧缓存。
 * 地址填本机 ID（多机总线上标识来源）；序列号填 0（无请求方，文档留白）。
 */
static void protocol_send_event(uint16_t command,
                                const uint8_t *payload, uint16_t payload_length)
{
    app_config_t config;
    protocol_frame_t event_frame;
    size_t encoded_length = 0U;

    (void)app_config_get(&config);

    event_frame.device_address = config.device_id;
    event_frame.frame_type = PROTOCOL_TYPE_EVENT;
    event_frame.command = command;
    event_frame.sequence = 0U;
    event_frame.payload_length = payload_length;
    event_frame.payload = payload;

    if (protocol_frame_encode(&event_frame, s_tx_buffer, sizeof(s_tx_buffer),
                              &encoded_length) == PROTOCOL_STATUS_OK)
    {
        protocol_send_frame(s_tx_buffer, (uint16_t)encoded_length);
    }
}

/* 0x0382：时间戳 4B + CH0/CH1 各 4B（大端，已乘变比），取共享区最新快照 */
static void protocol_send_report_data(void)
{
    sample_snapshot_t snapshot;
    board_rtc_time_t rtc_time;
    uint8_t payload[12];
    uint32_t timestamp;

    if (sample_task_snapshot_get(&snapshot) == 0)
    {
        return;   /* 采集引擎常驻，正常到不了这里 */
    }

    if (board_rtc_time_get(&rtc_time) != 0)
    {
        timestamp = app_cli_time_to_unix(&rtc_time);
    }
    else
    {
        timestamp = (uint32_t)(xTaskGetTickCount() /
                    (TickType_t)configTICK_RATE_HZ);
    }

    app_protocol_store_u32_be(&payload[0], timestamp);
    app_protocol_store_float_be(&payload[4], snapshot.value_ch0);
    app_protocol_store_float_be(&payload[8], snapshot.value_ch1);

    protocol_send_event(PROTOCOL_EVENT_CMD_REPORT_DATA, payload, 12U);
}

/* 0x0681：告警进入 ACTIVE 时的主动事件，字段布局与 0x0602 查询一致。 */
static void protocol_send_alarm_event(const alarm_task_event_t *event)
{
    uint8_t payload[13];

    if ((event == NULL) || (event->channel >= 2U))
    {
        return;
    }

    app_protocol_store_u32_be(&payload[0], event->timestamp);
    payload[4] = event->channel;
    app_protocol_store_float_be(&payload[5], event->threshold);
    app_protocol_store_float_be(&payload[9], event->actual);

    protocol_send_event(APP_PROTOCOL_CMD_ALARM_EVENT, payload,
                        (uint16_t)sizeof(payload));
}

/* 消费 AlarmTask 的事件；只有自定义协议 + 主动告警模式才占用 RS485。 */
static void protocol_process_alarm_events(void)
{
    alarm_task_event_t event;

    while (alarm_task_event_receive(&event, 0U) != 0)
    {
        if ((s_protocol_active_kind == PROTOCOL_KIND_CUSTOM) &&
            (app_protocol_alarm_report_enabled() != 0U))
        {
            protocol_send_alarm_event(&event);
        }
    }
}

/* 0x8888：载荷 2B 设备 ID（A-04"ID 一致"的判定来源；文档未定义载荷，此为补白） */
static void protocol_send_heartbeat(void)
{
    app_config_t config;
    uint8_t payload[2];

    (void)app_config_get(&config);
    app_protocol_store_u16_be(payload, config.device_id);

    protocol_send_event(PROTOCOL_EVENT_CMD_HEARTBEAT, payload, 2U);
}

/*
 * K-01/K-02：坏帧错误应答。
 * 只有"帧形完整"的坏帧才回（头尾版本正常且地址是本机）；
 * 长度字段非法的残帧只看头 12 字节；广播地址坏帧一律静默。
 */
static void protocol_process_bad_frame(const protocol_stream_bad_t *bad)
{
    app_config_t config;
    uint16_t address;
    uint16_t command;
    uint16_t sequence;

    if (bad->length < PROTOCOL_STREAM_HEADER_SIZE)
    {
        return;
    }

    if ((bad->data[0] != 0xA5U) || (bad->data[1] != 0xB6U) ||
        (bad->data[2] != PROTOCOL_VERSION))
    {
        return;
    }

    address = (uint16_t)((bad->data[3] << 8) | bad->data[4]);
    command = (uint16_t)((bad->data[6] << 8) | bad->data[7]);
    sequence = (uint16_t)((bad->data[8] << 8) | bad->data[9]);

    (void)app_config_get(&config);

    if ((address == PROTOCOL_BROADCAST_ADDRESS) ||
        (address != config.device_id))
    {
        return;
    }

    if (bad->length < PROTOCOL_FIXED_SIZE)
    {
        /* 头 12 字节就因长度字段非法被丢（K-02） */
        protocol_send_error(address, command, sequence, 0x02U);
        return;
    }

    /* CRC/帧尾等整帧级错误（K-01），要求帧尾完好才回 */
    if ((bad->data[bad->length - 2U] == 0xB6U) &&
        (bad->data[bad->length - 1U] == 0xA5U))
    {
        protocol_send_error(address, command, sequence, 0x01U);
    }
}

static void protocol_process_frame(const protocol_frame_t *frame)
{
    app_config_t config;
    /* 仅由 ProtocolTask 调用，不可重入；扩容后避免占用任务栈。 */
    static protocol_request_t request;
    static protocol_result_t result;
    uint8_t is_broadcast;

    /* 从机只应答命令帧（《01》七-3） */
    if (frame->frame_type != PROTOCOL_TYPE_COMMAND)
    {
        return;
    }

    (void)app_config_get(&config);

    /* 广播：允许的写命令静默执行（执行后不回帧不缓存），其余静默丢弃 */
    if (frame->device_address == PROTOCOL_BROADCAST_ADDRESS)
    {
        if (app_protocol_broadcast_allowed(frame->command) == 0U)
        {
            return;
        }

        is_broadcast = 1U;
    }
    else if (frame->device_address == config.device_id)
    {
        is_broadcast = 0U;
    }
    else
    {
        return;
    }

    /* 业务长度限制独立于队列容量，超长帧不得截断后执行。 */
    if (frame->payload_length > PROTOCOL_CUSTOM_REQUEST_DATA_MAX)
    {
        if (is_broadcast == 0U)
        {
            protocol_send_error(frame->device_address, frame->command,
                                frame->sequence, 0x02U);
        }
        return;
    }

    /* 重复帧缓存只对本机单帧生效（广播写幂等，重发无害） */
    if ((is_broadcast == 0U) &&
        (s_cache_valid != 0U) &&
        (s_cache_address == frame->device_address) &&
        (s_cache_command == frame->command) &&
        (s_cache_sequence == frame->sequence))
    {
        protocol_resend_cached(frame);
        return;
    }

    (void)memset(&request, 0, sizeof(request));
    (void)memset(&result, 0, sizeof(result));
    request.request_id = ++s_protocol_request_id;
    request.mode_epoch = s_protocol_mode_epoch;
    request.protocol_kind = PROTOCOL_KIND_CUSTOM;
    request.device_address = frame->device_address;
    request.origin = (is_broadcast != 0U) ?
                     PROTOCOL_ORIGIN_BROADCAST : PROTOCOL_ORIGIN_RS485;
    request.operation = frame->command;
    request.protocol_sequence = frame->sequence;
    request.deadline_tick = xTaskGetTickCount() + pdMS_TO_TICKS(PROTOCOL_COMPLETION_TIMEOUT_MS);

    request.payload_length = frame->payload_length;

    if (request.payload_length > 0U)
    {
        (void)memcpy(request.payload, frame->payload, request.payload_length);
    }

    if (protocol_request_send(&request) != pdTRUE)
    {
        if (is_broadcast == 0U)
        {
            result.status = 0x05U;   /* 请求队列满，设备忙 */
            result.payload_length = 0U;
            result.request_id = 0U;
            result.protocol_sequence = frame->sequence;
            protocol_send_result(frame, &result);
        }
        return;
    }

    if (protocol_result_receive_for_request(&request, &result) != pdTRUE)
    {
        if (is_broadcast == 0U)
        {
            result.status = 0x05U;
            result.payload_length = 0U;
            result.request_id = 0U;
            result.protocol_sequence = frame->sequence;
            protocol_send_result(frame, &result);
        }
        return;
    }

    if (is_broadcast != 0U)
    {
        /* 广播执行完成即静默（《01》七-6：任何设备均不得对广播帧应答） */
        return;
    }

    protocol_send_result(frame, &result);
    protocol_apply_custom_result(&result);

    /* 0x0101/0x0500：应答已发完（TC 等待在 BSP 里），留缓冲后复位 */
    if (app_protocol_reboot_pending() != 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(100U));

        NVIC_SystemReset();
    }
}

static void protocol_task(void *argument)
{
    protocol_frame_t frame;
    uint8_t byte;

    (void)argument;

    while ((xEventGroupGetBits(task_events_get()) &
            TASK_EVENT_CONFIG_READY) == 0U)
    {
        s_protocol_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_protocol_task_heartbeat++;
        vTaskDelay(pdMS_TO_TICKS(10U));
    }

    protocol_stream_init(&s_protocol_stream);

    protocol_sync_mode();

    s_report_last_tick = xTaskGetTickCount();
    s_heartbeat_next_tick = xTaskGetTickCount() +
                            pdMS_TO_TICKS(PROTOCOL_HEARTBEAT_FIRST_DELAY_MS);

    for(;;)
    {
        protocol_sync_mode();

        if (s_protocol_active_kind == PROTOCOL_KIND_MODBUS)
        {
            protocol_process_modbus_frames();
        }
        else
        {
            while (board_usart1_rs485_try_receive_byte(&byte) != 0U)
            {
                protocol_stream_event_t event =
                    protocol_stream_feed(&s_protocol_stream, byte, &frame,
                                         &s_protocol_bad);

                if (event == PROTOCOL_STREAM_EVENT_FRAME_READY)
                {
                    protocol_process_frame(&frame);
                }
                else if (event == PROTOCOL_STREAM_EVENT_FRAME_DROPPED)
                {
                    /* K-01/K-02：帧形完整的坏帧回错误应答，其余静默 */
                    protocol_process_bad_frame(&s_protocol_bad);
                }
            }
        }

        protocol_process_alarm_events();

        /* 自动上报和心跳只在自定义协议模式运行。 */
        if ((s_protocol_active_kind == PROTOCOL_KIND_CUSTOM) &&
            (app_protocol_auto_report_enabled() != 0U))
        {
            TickType_t now = xTaskGetTickCount();
            TickType_t interval_ticks =
                (TickType_t)app_protocol_report_interval_get() *
                (TickType_t)configTICK_RATE_HZ;

            if ((now - s_report_last_tick) >= interval_ticks)
            {
                protocol_send_report_data();
                s_report_last_tick = now;
            }
        }

        /* 心跳：上电后先发一次，之后每 30s（七-7） */
        if (s_protocol_active_kind == PROTOCOL_KIND_CUSTOM)
        {
            TickType_t now = xTaskGetTickCount();

            if (now >= s_heartbeat_next_tick)
            {
                protocol_send_heartbeat();
                s_heartbeat_next_tick = now +
                    pdMS_TO_TICKS(PROTOCOL_HEARTBEAT_PERIOD_MS);
            }
        }

        s_protocol_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_protocol_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int protocol_task_create(void)
{
    TaskHandle_t protocol_task_handle;

    protocol_task_handle = xTaskCreateStatic(
        protocol_task,
        "Protocol",
        PROTOCOL_TASK_STACK_DEPTH,
        NULL,
        PROTOCOL_TASK_PRIORITY,
        s_protocol_task_stack,
        &s_protocol_task_tcb
    );

    if (protocol_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}

uint32_t protocol_task_get_heartbeat(void)
{
    return s_protocol_task_heartbeat;
}

uint32_t protocol_task_get_stack_high_water_mark(void)
{
    return s_protocol_task_stack_high_water_mark;
}
