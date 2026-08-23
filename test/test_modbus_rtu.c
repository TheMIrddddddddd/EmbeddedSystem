/* PC tests for the generic Modbus RTU frame codec. */

#include "unity.h"
#include "modbus_rtu.h"
#include <string.h>

static const uint8_t read_request[] = {
    0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x0AU, 0xC5U, 0xCDU
};

static const uint8_t read_response[] = {
    0x01U, 0x03U, 0x02U, 0x00U, 0x7BU, 0xF8U, 0x67U
};

void test_modbus_encode_read_request(void)
{
    static const uint8_t payload[] = {
        0x00U, 0x00U, 0x00U, 0x0AU
    };
    uint8_t buffer[8] = {0U};
    size_t encoded_length = 0U;

    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
                      modbus_rtu_encode(0x01U, 0x03U, payload,
                                         sizeof(payload), buffer,
                                         sizeof(buffer), &encoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(read_request), encoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(read_request, buffer, sizeof(read_request));
}

void test_modbus_encode_empty_data(void)
{
    uint8_t buffer[4] = {0U};
    size_t encoded_length = 0U;

    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
                      modbus_rtu_encode(0x11U, 0x06U, NULL, 0U, buffer,
                                         sizeof(buffer), &encoded_length));
    TEST_ASSERT_EQUAL_UINT32(4U, encoded_length);
}

void test_modbus_decode_read_response(void)
{
    uint8_t address = 0U;
    uint8_t function = 0U;
    const uint8_t *data = NULL;
    uint8_t data_length = 0U;
    size_t decoded_length = 0U;

    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
                      modbus_rtu_decode(read_response, sizeof(read_response),
                                        &address, &function, &data,
                                        &data_length, &decoded_length));
    TEST_ASSERT_EQUAL_HEX8(0x01U, address);
    TEST_ASSERT_EQUAL_HEX8(0x03U, function);
    TEST_ASSERT_EQUAL_UINT8(2U, data_length);
    TEST_ASSERT_EQUAL_PTR(&read_response[3], data);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&read_response[3], data, 2U);
    TEST_ASSERT_EQUAL_UINT32(sizeof(read_response), decoded_length);
}

void test_modbus_decode_accepts_trailing_bytes(void)
{
    uint8_t buffer[sizeof(read_response) + 3U] = {0U};
    uint8_t address = 0U;
    uint8_t function = 0U;
    const uint8_t *data = NULL;
    uint8_t data_length = 0U;
    size_t decoded_length = 0U;

    memcpy(buffer, read_response, sizeof(read_response));
    memset(&buffer[sizeof(read_response)], 0x5AU, 3U);

    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
                      modbus_rtu_decode(buffer, sizeof(buffer), &address,
                                        &function, &data, &data_length,
                                        &decoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(read_response), decoded_length);
}

void test_modbus_rejects_invalid_arguments(void)
{
    uint8_t buffer[8] = {0U};
    size_t length = 0U;
    uint8_t address = 0U;
    uint8_t function = 0U;
    uint8_t data_length = 0U;
    const uint8_t *data = NULL;

    TEST_ASSERT_EQUAL(MODBUS_STATUS_NULL_POINTER,
                      modbus_rtu_encode(1U, 3U, NULL, 1U, buffer,
                                         sizeof(buffer), &length));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_NULL_POINTER,
                      modbus_rtu_decode(NULL, sizeof(read_response),
                                        &address, &function, &data,
                                        &data_length, &length));
}

void test_modbus_rejects_small_or_short_frames(void)
{
    uint8_t buffer[8] = {0U};
    size_t length = 0U;
    uint8_t address = 0U;
    uint8_t function = 0U;
    uint8_t data_length = 0U;
    const uint8_t *data = NULL;

    TEST_ASSERT_EQUAL(MODBUS_STATUS_BUFFER_TOO_SMALL,
                      modbus_rtu_encode(1U, 3U, NULL, 0U, buffer, 3U,
                                         &length));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_FRAME_TOO_SHORT,
                      modbus_rtu_decode(read_response, 2U, &address,
                                        &function, &data, &data_length,
                                        &length));
}

void test_modbus_rejects_crc_error(void)
{
    uint8_t buffer[sizeof(read_response)];
    uint8_t address = 0U;
    uint8_t function = 0U;
    uint8_t data_length = 0U;
    const uint8_t *data = NULL;
    size_t length = 0U;

    memcpy(buffer, read_response, sizeof(buffer));
    buffer[sizeof(buffer) - 1U] ^= 0x01U;

    TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_CRC,
                      modbus_rtu_decode(buffer, sizeof(buffer), &address,
                                        &function, &data, &data_length,
                                        &length));
}

void test_modbus_rejects_invalid_address_and_function(void)
{
    uint8_t buffer[sizeof(read_response)];
    uint8_t address = 0U;
    uint8_t function = 0U;
    uint8_t data_length = 0U;
    const uint8_t *data = NULL;
    size_t length = 0U;

    memcpy(buffer, read_response, sizeof(buffer));
    buffer[0] = 0x00U;
    TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_ADDRESS,
                      modbus_rtu_decode(buffer, sizeof(buffer), &address,
                                        &function, &data, &data_length,
                                        &length));

    memcpy(buffer, read_response, sizeof(buffer));
    buffer[1] = 0x00U;
    TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_FUNCTION,
                      modbus_rtu_decode(buffer, sizeof(buffer), &address,
                                        &function, &data, &data_length,
                                        &length));
}
