#ifndef TASK_QUEUES_H
#define TASK_QUEUES_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "ebtn.h"

typedef struct 
{
    uint16_t key_id;
    uint8_t event;
    uint8_t reserved;
    uint32_t timestamp_ms;
} key_event_t;

#define PROTOCOL_KIND_CUSTOM               0U
#define PROTOCOL_KIND_MODBUS               1U
#define PROTOCOL_CUSTOM_REQUEST_DATA_MAX   12U
/* RTU ADU 256B - 地址/功能码/CRC 4B，完整复制数据区。 */
#define PROTOCOL_REQUEST_DATA_MAX          252U
#define PROTOCOL_RESULT_DATA_MAX           32U
#define PROTOCOL_RESULT_APPLY_NONE         0x00U
#define PROTOCOL_RESULT_APPLY_ID           0x01U
#define PROTOCOL_RESULT_APPLY_BAUD         0x02U

typedef struct
{
    uint32_t request_id;
    uint32_t mode_epoch; /* 模式切换代次，由后续模式管理流程维护。 */
    TickType_t deadline_tick;
    uint16_t device_address;
    uint16_t operation; /* 自定义命令码或 Modbus 功能码。 */
    uint16_t modbus_start_address;
    uint16_t modbus_quantity;
    uint16_t modbus_value;
    uint16_t protocol_sequence;
    uint16_t payload_length;
    uint8_t protocol_kind;
    uint8_t origin;
    uint8_t decode_exception;
    uint8_t reserved;
    uint8_t payload[PROTOCOL_REQUEST_DATA_MAX];
} protocol_request_t;

typedef struct
{
    uint32_t request_id;
    uint32_t mode_epoch;
    uint32_t next_baudrate;
    uint16_t device_address;
    uint16_t operation;
    uint16_t protocol_sequence;
    uint16_t payload_length;
    uint16_t next_device_id;
    uint8_t protocol_kind;
    uint8_t status; /* 0=成功，其他值按 protocol_kind 解释。 */
    uint8_t reply_required;
    uint8_t apply_flags; /* 应答发送完成后应用通信参数。 */
    uint8_t payload[PROTOCOL_RESULT_DATA_MAX];
} protocol_result_t;

int task_queues_init(void);

BaseType_t protocol_request_send(const protocol_request_t *request);
BaseType_t protocol_request_receive(protocol_request_t *request, TickType_t wait_ticks);
void protocol_queues_reset(void);
BaseType_t key_event_send(const key_event_t *event);
BaseType_t key_event_receive(key_event_t *event, TickType_t wait_ticks);
BaseType_t protocol_result_send(const protocol_result_t *result);
BaseType_t protocol_result_receive(protocol_result_t *result, TickType_t wait_ticks);
#endif /* TASK_QUEUES_H */
