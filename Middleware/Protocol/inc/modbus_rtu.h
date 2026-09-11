#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    MODBUS_STATUS_OK = 0,
    MODBUS_STATUS_NULL_POINTER,
    MODBUS_STATUS_BUFFER_TOO_SMALL,
    MODBUS_STATUS_FRAME_TOO_SHORT,
    MODBUS_STATUS_INVALID_ADDRESS,
    MODBUS_STATUS_INVALID_FUNCTION,
    MODBUS_STATUS_INVALID_LENGTH,
    MODBUS_STATUS_INVALID_CRC
} modbus_status_t;

#define MODBUS_RTU_MAX_ADU_SIZE          256U
#define MODBUS_RTU_MAX_SLAVE_ADDRESS     247U

#define MODBUS_FUNCTION_READ_HOLDING     0x03U
#define MODBUS_FUNCTION_READ_INPUT       0x04U
#define MODBUS_FUNCTION_WRITE_SINGLE     0x06U
#define MODBUS_FUNCTION_WRITE_MULTIPLE   0x10U

#define MODBUS_EXCEPTION_NONE           0x00U
#define MODBUS_EXCEPTION_FUNCTION       0x01U
#define MODBUS_EXCEPTION_ADDRESS        0x02U
#define MODBUS_EXCEPTION_VALUE          0x03U
#define MODBUS_EXCEPTION_BUSY           0x06U

typedef struct
{
    uint8_t address;
    uint8_t function;
    uint8_t exception;

    uint16_t start_address;
    uint16_t quantity;

    /* 06：单个寄存器值。 */
    uint16_t value;

    /* 10：指向输入帧中的寄存器数据，不复制数据。 */
    const uint8_t *write_data;
    uint8_t write_byte_count;
} modbus_request_t;

/*
 * buffer 必须是 RTU 收帧层交付的一帧完整 ADU。
 *
 * 返回非 OK：帧无效，调用方静默丢弃。
 * 返回 OK、exception 非 0：请求需要异常响应。
 * 返回 OK、exception 为 0：交给寄存器业务层处理。
 *
 * 地址匹配、广播权限及寄存器映射由上层处理。
 */

modbus_status_t modbus_rtu_request_decode(const uint8_t *buffer, size_t length, modbus_request_t *request);
modbus_status_t modbus_rtu_exception_encode(uint8_t address, uint8_t fuction, uint8_t exception, uint8_t *buffer, size_t buffer_size, size_t *encoded_length);

modbus_status_t modbus_rtu_encode(uint8_t address,
                                  uint8_t function,
                                  const uint8_t *data,
                                  uint8_t data_length,
                                  uint8_t *buffer,
                                  size_t buffer_size,
                                  size_t *encoded_length);

modbus_status_t modbus_rtu_decode(const uint8_t *buffer,
                                  size_t buffer_size,
                                  uint8_t *address,
                                  uint8_t *function,
                                  const uint8_t **data,
                                  uint8_t *data_length,
                                  size_t *decoded_length);

#endif /* MODBUS_RTU_H */
