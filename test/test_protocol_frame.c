/* PC tests for the protocol frame encoder. */

#include "unity.h"
#include "protocol_frame.h"
#include "common_crc.h"
#include <string.h>

/* Keep the expected API visible to the test while the implementation is being
 * developed in Middleware/Protocol/src/protocol_frame.c. */
extern protocol_status_t protocol_frame_encode(const protocol_frame_t *frame,
                                                uint8_t *buffer,
                                                size_t buffer_size,
                                                size_t *encoded_length);
extern protocol_status_t protocol_frame_decode(const uint8_t *buffer,
                                                size_t buffer_size,
                                                protocol_frame_t *frame,
                                                size_t *decoded_length);

static const uint8_t empty_command_expected[] = {
    0xA5U, 0xB6U, 0x02U, 0x00U, 0x01U, 0x01U, 0x03U, 0xAAU,
    0x00U, 0x01U, 0x00U, 0x00U, 0xA8U, 0x9AU, 0xB6U, 0xA5U
};

static const uint8_t response_expected[] = {
    0xA5U, 0xB6U, 0x02U, 0x00U, 0x01U, 0x02U, 0x03U, 0xAAU,
    0x00U, 0x01U, 0x00U, 0x01U, 0xFFU, 0xAEU, 0x29U, 0xB6U, 0xA5U
};

void test_protocol_encode_empty_command(void)
{
    protocol_frame_t frame = {
        0x0001U, PROTOCOL_TYPE_COMMAND, 0x03AAU, 0x0001U, 0U, NULL
    };
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE] = {0U};
    size_t encoded_length = 0U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(empty_command_expected), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(empty_command_expected, buffer,
                                  sizeof(empty_command_expected));
}

void test_protocol_encode_response_payload(void)
{
    static const uint8_t payload[] = {0xFFU};
    protocol_frame_t frame = {
        0x0001U, PROTOCOL_TYPE_RESPONSE, 0x03AAU, 0x0001U,
        sizeof(payload), payload
    };
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE] = {0U};
    size_t encoded_length = 0U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(response_expected), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(response_expected, buffer,
                                  sizeof(response_expected));
}

void test_protocol_encode_rejects_invalid_arguments(void)
{
    protocol_frame_t frame = {
        0x0001U, PROTOCOL_TYPE_COMMAND, 0x03AAU, 0x0001U, 0U, NULL
    };
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE] = {0U};
    size_t encoded_length = 0U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_encode(NULL, buffer, sizeof(buffer),
                                             &encoded_length));
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_encode(&frame, NULL, sizeof(buffer),
                                             &encoded_length));
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             NULL));
}

void test_protocol_encode_rejects_invalid_payload_and_type(void)
{
    static const uint8_t payload[] = {0x11U};
    protocol_frame_t frame = {
        0x0001U, PROTOCOL_TYPE_COMMAND, 0x03AAU, 0x0001U,
        sizeof(payload), payload
    };
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE] = {0U};
    size_t encoded_length = 0U;

    frame.payload_length = PROTOCOL_MAX_PAYLOAD_SIZE + 1U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_PAYLOAD_TOO_LARGE,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));

    frame.payload_length = 1U;
    frame.payload = NULL;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));

    frame.payload = payload;
    frame.frame_type = 0x03U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_TYPE,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));
}

void test_protocol_encode_rejects_small_buffer(void)
{
    protocol_frame_t frame = {
        0x0001U, PROTOCOL_TYPE_COMMAND, 0x03AAU, 0x0001U, 0U, NULL
    };
    uint8_t buffer[PROTOCOL_FIXED_SIZE - 1U] = {0U};
    size_t encoded_length = 123U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_BUFFER_TOO_SMALL,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));
    TEST_ASSERT_EQUAL_UINT32(0U, encoded_length);
}

void test_protocol_encode_rejects_reserved_address(void)
{
    protocol_frame_t frame = {
        0x0000U, PROTOCOL_TYPE_COMMAND, 0x03AAU, 0x0001U, 0U, NULL
    };
    uint8_t buffer[PROTOCOL_FIXED_SIZE] = {0U};
    size_t encoded_length = 123U;
    protocol_status_t status;

    status = protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                   &encoded_length);

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_ADDRESS, status);
    TEST_ASSERT_EQUAL_UINT32(0U, encoded_length);
}

void test_protocol_encode_maximum_payload(void)
{
    static uint8_t payload[PROTOCOL_MAX_PAYLOAD_SIZE];
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE] = {0U};
    protocol_frame_t frame = {
        0x0001U, PROTOCOL_TYPE_COMMAND, 0x1234U, 0x5678U,
        PROTOCOL_MAX_PAYLOAD_SIZE, payload
    };
    size_t encoded_length = 0U;
    size_t i;
    size_t crc_index;
    uint16_t expected_crc;
    uint16_t encoded_crc;

    for (i = 0U; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)i;
    }

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_encode(&frame, buffer, sizeof(buffer),
                                             &encoded_length));
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_MAX_FRAME_SIZE, encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, &buffer[12], sizeof(payload));

    crc_index = 12U + sizeof(payload);
    expected_crc = common_crc16_calc(buffer, (uint32_t)crc_index);
    encoded_crc = ((uint16_t)buffer[crc_index] << 8) |
                  (uint16_t)buffer[crc_index + 1U];

    TEST_ASSERT_EQUAL_HEX16(expected_crc, encoded_crc);
    TEST_ASSERT_EQUAL_HEX8(0xB6U, buffer[crc_index + 2U]);
    TEST_ASSERT_EQUAL_HEX8(0xA5U, buffer[crc_index + 3U]);
}

void test_protocol_decode_empty_command(void)
{
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_decode(empty_command_expected,
                                            sizeof(empty_command_expected),
                                            &frame, &decoded_length));
    TEST_ASSERT_EQUAL_HEX16(0x0001U, frame.device_address);
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_TYPE_COMMAND, frame.frame_type);
    TEST_ASSERT_EQUAL_HEX16(0x03AAU, frame.command);
    TEST_ASSERT_EQUAL_HEX16(0x0001U, frame.sequence);
    TEST_ASSERT_EQUAL_UINT16(0U, frame.payload_length);
    TEST_ASSERT_NULL(frame.payload);
    TEST_ASSERT_EQUAL_UINT32(sizeof(empty_command_expected), decoded_length);
}

void test_protocol_decode_response_payload(void)
{
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_decode(response_expected,
                                            sizeof(response_expected),
                                            &frame, &decoded_length));
    TEST_ASSERT_EQUAL_HEX16(0x0001U, frame.device_address);
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_TYPE_RESPONSE, frame.frame_type);
    TEST_ASSERT_EQUAL_HEX16(0x03AAU, frame.command);
    TEST_ASSERT_EQUAL_HEX16(0x0001U, frame.sequence);
    TEST_ASSERT_EQUAL_UINT16(1U, frame.payload_length);
    TEST_ASSERT_EQUAL_PTR(&response_expected[12], frame.payload);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, frame.payload[0]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(response_expected), decoded_length);
}

void test_protocol_decode_rejects_invalid_arguments(void)
{
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_decode(NULL,
                                            sizeof(empty_command_expected),
                                            &frame, &decoded_length));
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_decode(empty_command_expected,
                                            sizeof(empty_command_expected),
                                            NULL, &decoded_length));
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_NULL_POINTER,
                      protocol_frame_decode(empty_command_expected,
                                            sizeof(empty_command_expected),
                                            &frame, NULL));
}

void test_protocol_decode_rejects_short_frame(void)
{
    protocol_frame_t frame = {0U};
    size_t decoded_length = 123U;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_FRAME_TOO_SHORT,
                      protocol_frame_decode(empty_command_expected,
                                            PROTOCOL_FIXED_SIZE - 1U,
                                            &frame, &decoded_length));
    TEST_ASSERT_EQUAL_UINT32(0U, decoded_length);
}

void test_protocol_decode_rejects_fixed_field_errors(void)
{
    uint8_t buffer[sizeof(empty_command_expected)];
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    memcpy(buffer, empty_command_expected, sizeof(buffer));
    buffer[0] = 0x00U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_HEADER,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));

    memcpy(buffer, empty_command_expected, sizeof(buffer));
    buffer[2] = 0x01U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_VERSION,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));

    memcpy(buffer, empty_command_expected, sizeof(buffer));
    buffer[3] = 0x00U;
    buffer[4] = 0x00U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_ADDRESS,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));

    memcpy(buffer, empty_command_expected, sizeof(buffer));
    buffer[5] = 0x03U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_TYPE,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));
}

void test_protocol_decode_rejects_length_errors(void)
{
    uint8_t buffer[sizeof(response_expected)];
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    memcpy(buffer, response_expected, sizeof(buffer));
    buffer[10] = 0x04U;
    buffer[11] = 0x01U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_LENGTH,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));

    memcpy(buffer, response_expected, sizeof(buffer));
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_FRAME_TOO_SHORT,
                      protocol_frame_decode(buffer, PROTOCOL_FIXED_SIZE,
                                            &frame, &decoded_length));
}

void test_protocol_decode_rejects_crc_and_tail_errors(void)
{
    uint8_t buffer[sizeof(empty_command_expected)];
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    memcpy(buffer, empty_command_expected, sizeof(buffer));
    buffer[12] ^= 0x01U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_CRC,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));

    memcpy(buffer, empty_command_expected, sizeof(buffer));
    buffer[14] = 0x00U;
    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_INVALID_TAIL,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));
}

void test_protocol_decode_consumes_one_frame(void)
{
    uint8_t buffer[sizeof(empty_command_expected) + 4U] = {0U};
    protocol_frame_t frame = {0U};
    size_t decoded_length = 0U;

    memcpy(buffer, empty_command_expected, sizeof(empty_command_expected));
    memset(&buffer[sizeof(empty_command_expected)], 0x5AU, 4U);

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_decode(buffer, sizeof(buffer), &frame,
                                            &decoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(empty_command_expected), decoded_length);
}
