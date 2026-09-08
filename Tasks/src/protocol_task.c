#include "protocol_task.h"
#include <string.h>

#include "gd32f4xx.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_protocol.h"
#include "board_usart.h"
#include "protocol_frame.h"
#include "protocol_stream.h"
#include "task_queues.h"

#define PROTOCOL_TASK_PRIORITY          5U
#define PROTOCOL_TASK_STACK_DEPTH       256U

/* ControlTask 10ms 轮询，完成应答远快于此；超时视为异常路径 */
#define PROTOCOL_COMPLETION_TIMEOUT_MS  500U

/* 应答帧数据区 = 状态字节 + 结果数据 */
#define PROTOCOL_TX_PAYLOAD_MAX         13U

#define PROTOCOL_CACHE_SIZE             128U

#define PROTOCOL_RESPONSE_OK_BYTE       0xFFU
 
#define PROTOCOL_ORIGIN_RS485           0x01U
#define PROTOCOL_ORIGIN_BROADCAST       0x02U

/* 《01》十四 K-01/K-02：CRC 错/长度错要回错误应答帧 */
#define PROTOCOL_BROADCAST_ADDRESS      0xFFFFU

/* 解析器固定头长度（帧头..数据长度字段，与 protocol_stream.c 一致） */
#define PROTOCOL_STREAM_HEADER_SIZE     12U

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

static void protocol_send_frame(const uint8_t *data, uint16_t length)
{
    board_usart1_rs485_send_buffer(data, length);
}

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
    protocol_request_t request;
    protocol_result_t result;
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

    request.request_id = ++s_protocol_request_id;
    request.origin = (is_broadcast != 0U) ?
                     PROTOCOL_ORIGIN_BROADCAST : PROTOCOL_ORIGIN_RS485;
    request.operation = frame->command;
    request.protocol_sequence = frame->sequence;
    request.deadline_tick = xTaskGetTickCount() + pdMS_TO_TICKS(PROTOCOL_COMPLETION_TIMEOUT_MS);

    request.payload_length = frame->payload_length;

    if (request.payload_length > sizeof(request.payload))
    {
        request.payload_length = (uint8_t)sizeof(request.payload);
    }

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

    if (protocol_result_receive(&result,
                                pdMS_TO_TICKS(PROTOCOL_COMPLETION_TIMEOUT_MS)) != pdTRUE)
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

    /* 0x0101：应答已发完（TC 等待在 BSP 里），留缓冲后复位 */
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

    protocol_stream_init(&s_protocol_stream);

    for(;;)
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
