#include "modbus_rtu.h"
#include "common_crc.h"

#define MODBUS_RTU_ADDRESS_OFFSET       0U
#define MODBUS_RTU_FUNCTION_OFFSET      1U
#define MODBUS_RTU_DATA_OFFSET           2U
#define MODBUS_RTU_RESPONSE_DATA_OFFSET  3U
#define MODBUS_RTU_CRC_SIZE              2U
#define MODBUS_RTU_MIN_FRAME_SIZE        4U

modbus_status_t modbus_rtu_encode(uint8_t address,
                                  uint8_t function,
                                  const uint8_t *data,
                                  uint8_t data_length,
                                  uint8_t *buffer,
                                  size_t buffer_size,
                                  size_t *encoded_length)
{
    size_t required_size;
    size_t crc_index;
    uint16_t crc;
    uint8_t index;

    if ((buffer == NULL) || (encoded_length == NULL))
    {
        return MODBUS_STATUS_NULL_POINTER;
    }

    *encoded_length = 0U;

    if ((data_length > 0U) && (data == NULL))
    {
        return MODBUS_STATUS_NULL_POINTER;
    }

    required_size = MODBUS_RTU_MIN_FRAME_SIZE + data_length;
    if (buffer_size < required_size)
    {
        return MODBUS_STATUS_BUFFER_TOO_SMALL;
    }

    buffer[MODBUS_RTU_ADDRESS_OFFSET] = address;
    buffer[MODBUS_RTU_FUNCTION_OFFSET] = function;

    for (index = 0U; index < data_length; index++)
    {
        buffer[MODBUS_RTU_DATA_OFFSET + index] = data[index];
    }

    crc_index = MODBUS_RTU_DATA_OFFSET + data_length;
    crc = common_crc16_calc(buffer, (uint32_t)crc_index);

    /* Modbus RTU sends the CRC low byte first. */
    buffer[crc_index] = (uint8_t)(crc & 0xFFU);
    buffer[crc_index + 1U] = (uint8_t)(crc >> 8);

    *encoded_length = required_size;
    return MODBUS_STATUS_OK;
}

modbus_status_t modbus_rtu_decode(const uint8_t *buffer,
                                  size_t buffer_size,
                                  uint8_t *address,
                                  uint8_t *function,
                                  const uint8_t **data,
                                  uint8_t *data_length,
                                  size_t *decoded_length)
{
    size_t required_size;
    size_t crc_index;
    uint8_t byte_count;
    uint16_t encoded_crc;
    uint16_t calculated_crc;

    if ((buffer == NULL) ||
        (address == NULL) ||
        (function == NULL) ||
        (data == NULL) ||
        (data_length == NULL) ||
        (decoded_length == NULL))
    {
        return MODBUS_STATUS_NULL_POINTER;
    }

    *address = 0U;
    *function = 0U;
    *data = NULL;
    *data_length = 0U;
    *decoded_length = 0U;

    if (buffer_size < MODBUS_RTU_MIN_FRAME_SIZE)
    {
        return MODBUS_STATUS_FRAME_TOO_SHORT;
    }

    *address = buffer[MODBUS_RTU_ADDRESS_OFFSET];
    *function = buffer[MODBUS_RTU_FUNCTION_OFFSET];

    if (*address == 0U)
    {
        return MODBUS_STATUS_INVALID_ADDRESS;
    }
    if (*function == 0U)
    {
        return MODBUS_STATUS_INVALID_FUNCTION;
    }

    byte_count = buffer[MODBUS_RTU_FUNCTION_OFFSET + 1U];
    required_size = MODBUS_RTU_RESPONSE_DATA_OFFSET +
                    byte_count + MODBUS_RTU_CRC_SIZE;
    if (buffer_size < required_size)
    {
        return MODBUS_STATUS_FRAME_TOO_SHORT;
    }

    crc_index = MODBUS_RTU_RESPONSE_DATA_OFFSET + byte_count;
    encoded_crc = (uint16_t)buffer[crc_index] |
                  ((uint16_t)buffer[crc_index + 1U] << 8);
    calculated_crc = common_crc16_calc(buffer, (uint32_t)crc_index);

    if (encoded_crc != calculated_crc)
    {
        return MODBUS_STATUS_INVALID_CRC;
    }

    *data = &buffer[MODBUS_RTU_RESPONSE_DATA_OFFSET];
    *data_length = byte_count;
    *decoded_length = required_size;

    return MODBUS_STATUS_OK;
}
