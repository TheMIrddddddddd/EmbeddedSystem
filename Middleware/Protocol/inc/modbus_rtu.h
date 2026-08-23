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
