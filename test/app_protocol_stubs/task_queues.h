#ifndef TEST_APP_PROTOCOL_TASK_QUEUES_H
#define TEST_APP_PROTOCOL_TASK_QUEUES_H

#include <stdint.h>

#define PROTOCOL_KIND_CUSTOM       0U
#define PROTOCOL_RESULT_APPLY_NONE 0x00U
#define PROTOCOL_RESULT_APPLY_ID   0x01U
#define PROTOCOL_RESULT_APPLY_BAUD 0x02U

typedef struct
{
    uint32_t request_id;
    uint32_t mode_epoch;
    uint32_t deadline_tick;
    uint16_t device_address;
    uint16_t operation;
    uint16_t modbus_start_address;
    uint16_t modbus_quantity;
    uint16_t modbus_value;
    uint16_t protocol_sequence;
    uint16_t payload_length;
    uint8_t protocol_kind;
    uint8_t origin;
    uint8_t decode_exception;
    uint8_t reserved;
    uint8_t payload[252];
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
    uint8_t status;
    uint8_t reply_required;
    uint8_t apply_flags;
    uint8_t payload[32];
} protocol_result_t;

#endif
