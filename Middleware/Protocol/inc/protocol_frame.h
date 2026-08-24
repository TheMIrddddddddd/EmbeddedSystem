#ifndef PROTOCOL_FRAME_H
#define PROTOCOL_FRAME_H

#include <stdint.h>
#include <stddef.h>

#define PROTOCOL_FRAME_HEADER       0XA5B6
#define PROTOCOL_FRAME_TAIL         0XB6A5
#define PROTOCOL_VERSION            0X02
#define PROTOCOL_MAX_PAYLOAD_SIZE   1024UL
#define PROTOCOL_FIXED_SIZE         16UL
#define PROTOCOL_MAX_FRAME_SIZE     1040UL

#define PROTOCOL_TYPE_COMMAND       0X01
#define PROTOCOL_TYPE_RESPONSE      0X02
#define PROTOCOL_TYPE_EVENT         0X05
#define PROTOCOL_TYPE_ERROR         0XFF

typedef enum
{
    PROTOCOL_STATUS_OK = 0,
    PROTOCOL_STATUS_NULL_POINTER,
    PROTOCOL_STATUS_BUFFER_TOO_SMALL,
    PROTOCOL_STATUS_PAYLOAD_TOO_LARGE,
    PROTOCOL_STATUS_FRAME_TOO_SHORT,
    PROTOCOL_STATUS_INVALID_HEADER,
    PROTOCOL_STATUS_INVALID_VERSION,
    PROTOCOL_STATUS_INVALID_TYPE,
    PROTOCOL_STATUS_INVALID_LENGTH,
    PROTOCOL_STATUS_INVALID_CRC,
    PROTOCOL_STATUS_INVALID_TAIL,
    PROTOCOL_STATUS_INVALID_ADDRESS
} protocol_status_t;

typedef struct
{
    uint16_t device_address;
    uint8_t frame_type;
    uint16_t command;
    uint16_t sequence;
    uint16_t payload_length;
    const uint8_t *payload;
} protocol_frame_t;

protocol_status_t protocol_frame_encode(const protocol_frame_t *frame,
                                        uint8_t *buffer,
                                        size_t buffer_size,
                                        size_t *encode_length);

protocol_status_t protocol_frame_decode(const uint8_t *buffer,
                                        size_t buffer_size,
                                        protocol_frame_t *frame,
                                        size_t *decode_length);

#endif /* PROTOCOL_FRAME_H */
