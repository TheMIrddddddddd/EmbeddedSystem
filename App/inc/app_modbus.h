#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>
#include "modbus_rtu.h"

/*
 * 最大正常响应：
 * 03 读取 8 个保持寄存器：
 * byte_count 1B + 寄存器数据 16B = 17B。
 */
#define APP_MODBUS_RESPONSE_DATA_MAX        17U

#define APP_MODBUS_APPLY_NONE               0X00U
#define APP_MODBUS_APPLY_ID                 0X01U
#define APP_MODBUS_APPLY_BAUD               0X02U

typedef struct
{
    uint8_t reply_required;
    uint8_t address;
    uint8_t function;
    uint8_t exception;
    uint8_t data_length;
    uint8_t data[APP_MODBUS_RESPONSE_DATA_MAX];
    uint8_t apply_flags;
    uint16_t next_device_id;
    uint32_t next_baudrate;
} app_modbus_result_t;

void app_modbus_execute(const modbus_request_t *request, app_modbus_result_t *result);

#endif /* APP_MODBUS_H */
