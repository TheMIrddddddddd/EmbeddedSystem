#include <string.h>

#include "protocol_frame.h"
#include "common_crc.h"

#define PROTOCOL_PAYLOAD_OFFSET 12U
#define PROTOCOL_CRC_SIZE       2U

static void protocol_write_u16_be(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value >> 8);
    buffer[1] = (uint8_t)(value & 0xFFU);
}

static uint16_t protocol_read_u16_be(const uint8_t *buffer)
{
    return (uint16_t)(((uint16_t)buffer[0] << 8) |
                      (uint16_t)buffer[1]);
}

static uint8_t protocol_frame_type_is_valid(uint8_t frame_type)
{
    switch (frame_type)
    {
        case PROTOCOL_TYPE_COMMAND:
        case PROTOCOL_TYPE_RESPONSE:
        case PROTOCOL_TYPE_EVENT:
        case PROTOCOL_TYPE_ERROR:
            return 1U;

        default:
            return 0U;
    }
}

protocol_status_t protocol_frame_encode(const protocol_frame_t *frame,
                                        uint8_t *buffer,
                                        size_t buffer_size,
                                        size_t *encode_length)
{
    size_t required_size;
    size_t crc_index;
    size_t tail_index;
    uint16_t crc;

    if ((frame == NULL) || (buffer == NULL) || (encode_length == NULL))
    {
        return PROTOCOL_STATUS_NULL_POINTER;
    }

    *encode_length = 0U;

    if (frame->device_address == 0U)
    {
        return PROTOCOL_STATUS_INVALID_ADDRESS;
    }
    if (frame->payload_length > PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return PROTOCOL_STATUS_PAYLOAD_TOO_LARGE;
    }
    if ((frame->payload_length > 0U) && (frame->payload == NULL))
    {
        return PROTOCOL_STATUS_NULL_POINTER;
    }
    if (protocol_frame_type_is_valid(frame->frame_type) == 0U)
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    required_size = PROTOCOL_FIXED_SIZE + frame->payload_length;
    if (buffer_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    protocol_write_u16_be(&buffer[0], PROTOCOL_FRAME_HEADER);
    buffer[2] = PROTOCOL_VERSION;
    protocol_write_u16_be(&buffer[3], frame->device_address);
    buffer[5] = frame->frame_type;
    protocol_write_u16_be(&buffer[6], frame->command);
    protocol_write_u16_be(&buffer[8], frame->sequence);
    protocol_write_u16_be(&buffer[10], frame->payload_length);

    if (frame->payload_length > 0U)
    {
        memcpy(&buffer[PROTOCOL_PAYLOAD_OFFSET], frame->payload,
               frame->payload_length);
    }

    crc_index = PROTOCOL_PAYLOAD_OFFSET + frame->payload_length;
    crc = common_crc16_calc(buffer, (uint32_t)crc_index);
    protocol_write_u16_be(&buffer[crc_index], crc);

    tail_index = crc_index + PROTOCOL_CRC_SIZE;
    protocol_write_u16_be(&buffer[tail_index], PROTOCOL_FRAME_TAIL);

    *encode_length = required_size;
    return PROTOCOL_STATUS_OK;
}

protocol_status_t protocol_frame_decode(const uint8_t *buffer,
                                        size_t buffer_size,
                                        protocol_frame_t *frame,
                                        size_t *decode_length)
{
    uint16_t device_address;
    uint8_t frame_type;
    uint16_t payload_length;
    size_t required_size;
    size_t crc_index;
    size_t tail_index;
    uint16_t encoded_crc;
    uint16_t calculated_crc;

    if ((buffer == NULL) || (frame == NULL) || (decode_length == NULL))
    {
        return PROTOCOL_STATUS_NULL_POINTER;
    }

    *decode_length = 0U;

    if (buffer_size < PROTOCOL_FIXED_SIZE)
    {
        return PROTOCOL_STATUS_FRAME_TOO_SHORT;
    }
    if (protocol_read_u16_be(&buffer[0]) != PROTOCOL_FRAME_HEADER)
    {
        return PROTOCOL_STATUS_INVALID_HEADER;
    }
    if (buffer[2] != PROTOCOL_VERSION)
    {
        return PROTOCOL_STATUS_INVALID_VERSION;
    }

    device_address = protocol_read_u16_be(&buffer[3]);
    if (device_address == 0U)
    {
        return PROTOCOL_STATUS_INVALID_ADDRESS;
    }

    frame_type = buffer[5];
    if (protocol_frame_type_is_valid(frame_type) == 0U)
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    payload_length = protocol_read_u16_be(&buffer[10]);
    if (payload_length > PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    required_size = PROTOCOL_FIXED_SIZE + payload_length;
    if (buffer_size < required_size)
    {
        return PROTOCOL_STATUS_FRAME_TOO_SHORT;
    }

    crc_index = PROTOCOL_PAYLOAD_OFFSET + payload_length;
    tail_index = crc_index + PROTOCOL_CRC_SIZE;
    if (protocol_read_u16_be(&buffer[tail_index]) != PROTOCOL_FRAME_TAIL)
    {
        return PROTOCOL_STATUS_INVALID_TAIL;
    }

    encoded_crc = protocol_read_u16_be(&buffer[crc_index]);
    calculated_crc = common_crc16_calc(buffer, (uint32_t)crc_index);
    if (encoded_crc != calculated_crc)
    {
        return PROTOCOL_STATUS_INVALID_CRC;
    }

    frame->device_address = device_address;
    frame->frame_type = frame_type;
    frame->command = protocol_read_u16_be(&buffer[6]);
    frame->sequence = protocol_read_u16_be(&buffer[8]);
    frame->payload_length = payload_length;
    frame->payload = (payload_length > 0U) ?
                     &buffer[PROTOCOL_PAYLOAD_OFFSET] : NULL;

    *decode_length = required_size;
    return PROTOCOL_STATUS_OK;
}
