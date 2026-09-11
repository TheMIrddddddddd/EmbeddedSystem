/* PC tests for the generic Modbus RTU frame codec. */

#include "unity.h"
#include "modbus_rtu.h"
#include <string.h>

static const uint8_t read_request[8];

/* Slave-side tests: complete ADUs only; timing and register maps are upper layers. */
static size_t slave_frame(uint8_t *frame, uint8_t address, uint8_t function,
                          const uint8_t *payload, uint8_t count)
{
    size_t length = 0U;
    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
        modbus_rtu_encode(address, function, payload, count,
                          frame, 260U, &length));
    return length;
}

static void assert_empty_request(const modbus_request_t *r)
{
    TEST_ASSERT_EQUAL_UINT8(0, r->address);
    TEST_ASSERT_EQUAL_UINT8(0, r->function);
    TEST_ASSERT_EQUAL_UINT8(0, r->exception);
    TEST_ASSERT_EQUAL_UINT16(0, r->start_address);
    TEST_ASSERT_EQUAL_UINT16(0, r->quantity);
    TEST_ASSERT_EQUAL_UINT16(0, r->value);
    TEST_ASSERT_NULL(r->write_data);
    TEST_ASSERT_EQUAL_UINT8(0, r->write_byte_count);
}

void test_slave_fixed_read_vector(void)
{
    modbus_request_t r;
    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
        modbus_rtu_request_decode(read_request, sizeof(read_request), &r));
    TEST_ASSERT_EQUAL_UINT8(1, r.address);
    TEST_ASSERT_EQUAL_UINT8(3, r.function);
    TEST_ASSERT_EQUAL_UINT16(0, r.start_address);
    TEST_ASSERT_EQUAL_UINT16(10, r.quantity);
    TEST_ASSERT_EQUAL_UINT8(0, r.exception);
    TEST_ASSERT_NULL(r.write_data);
}

void test_slave_read_quantity_boundaries(void)
{
    const uint16_t quantities[] = {0, 1, 125, 126, 65535};
    uint8_t frame[260], payload[4] = {0x12, 0x34, 0, 0};
    modbus_request_t r;
    unsigned f, i;
    for (f = 3; f <= 4; ++f) {
        for (i = 0; i < sizeof(quantities)/sizeof(quantities[0]); ++i) {
            size_t n;
            payload[2] = (uint8_t)(quantities[i] >> 8);
            payload[3] = (uint8_t)quantities[i];
            n = slave_frame(frame, 1, (uint8_t)f, payload, 4);
            TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frame, n, &r));
            TEST_ASSERT_EQUAL_UINT16(0x1234, r.start_address);
            TEST_ASSERT_EQUAL_UINT16(quantities[i], r.quantity);
            TEST_ASSERT_EQUAL_UINT8((i == 1 || i == 2) ? 0 : 3, r.exception);
        }
    }
}

void test_slave_write_single_values(void)
{
    const uint16_t values[] = {0, 1, 0x1234, 65535};
    uint8_t frame[260], payload[4] = {0, 0x10, 0, 0};
    modbus_request_t r;
    unsigned i;
    for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        size_t n;
        payload[2] = (uint8_t)(values[i] >> 8);
        payload[3] = (uint8_t)values[i];
        n = slave_frame(frame, 247, 6, payload, 4);
        TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frame, n, &r));
        TEST_ASSERT_EQUAL_UINT8(247, r.address);
        TEST_ASSERT_EQUAL_UINT16(0x10, r.start_address);
        TEST_ASSERT_EQUAL_UINT16(1, r.quantity);
        TEST_ASSERT_EQUAL_UINT16(values[i], r.value);
        TEST_ASSERT_EQUAL_UINT8(0, r.exception);
    }
}

void test_slave_write_multiple_limits_and_payload(void)
{
    const uint16_t quantities[] = {1, 2, 8, 123};
    uint8_t frame[260], payload[251];
    modbus_request_t r;
    unsigned i, j;
    for (i = 0; i < sizeof(quantities)/sizeof(quantities[0]); ++i) {
        size_t n;
        uint8_t bytes = (uint8_t)(2U * quantities[i]);
        payload[0] = 0x12; payload[1] = 0x34;
        payload[2] = 0; payload[3] = (uint8_t)quantities[i]; payload[4] = bytes;
        for (j = 0; j < bytes; ++j) payload[5+j] = (uint8_t)(j ^ 0xA5);
        n = slave_frame(frame, 1, 16, payload, (uint8_t)(5+bytes));
        TEST_ASSERT_EQUAL_UINT32(9U + bytes, n);
        TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frame, n, &r));
        TEST_ASSERT_EQUAL_UINT16(0x1234, r.start_address);
        TEST_ASSERT_EQUAL_UINT16(quantities[i], r.quantity);
        TEST_ASSERT_EQUAL_UINT8(0, r.exception);
        TEST_ASSERT_EQUAL_PTR(frame+7, r.write_data);
        TEST_ASSERT_EQUAL_UINT8(bytes, r.write_byte_count);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(payload+5, r.write_data, bytes);
    }
}

void test_slave_write_multiple_bad_values(void)
{
    const uint16_t quantities[] = {0, 124, 65535, 1, 2};
    uint8_t frame[260], payload[] = {0, 0, 0, 0, 0, 0xAB};
    modbus_request_t r;
    unsigned i;
    for (i = 0; i < sizeof(quantities)/sizeof(quantities[0]); ++i) {
        size_t n;
        payload[2] = (uint8_t)(quantities[i] >> 8);
        payload[3] = (uint8_t)quantities[i];
        payload[4] = (i == 4) ? 1 : 0;
        n = slave_frame(frame, 1, 16, payload, (uint8_t)(5+payload[4]));
        TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frame, n, &r));
        TEST_ASSERT_EQUAL_UINT8(3, r.exception);
        TEST_ASSERT_NULL(r.write_data);
        TEST_ASSERT_EQUAL_UINT8(0, r.write_byte_count);
    }
}

void test_slave_address_and_unknown_function(void)
{
    const uint8_t addresses[] = {0, 1, 247, 248, 255};
    uint8_t frame[260], payload[] = {0, 0, 0, 1};
    modbus_request_t r;
    unsigned i, f;
    for (i = 0; i < sizeof(addresses); ++i) {
        size_t n = slave_frame(frame, addresses[i], 3, payload, 4);
        TEST_ASSERT_EQUAL(i < 3 ? MODBUS_STATUS_OK : MODBUS_STATUS_INVALID_ADDRESS,
                          modbus_rtu_request_decode(frame, n, &r));
        if (i < 3) TEST_ASSERT_EQUAL_UINT8(addresses[i], r.address);
        else assert_empty_request(&r);
    }
    for (f = 0; f < 256; ++f) {
        size_t n;
        if (f == 3 || f == 4 || f == 6 || f == 16) continue;
        n = slave_frame(frame, 1, (uint8_t)f, NULL, 0);
        TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frame, n, &r));
        TEST_ASSERT_EQUAL_UINT8(f, r.function);
        TEST_ASSERT_EQUAL_UINT8(1, r.exception);
    }
}

void test_slave_invalid_arguments_and_bounds(void)
{
    uint8_t frame[257] = {0};
    modbus_request_t r;
    size_t n;
    memset(&r, 0xA5, sizeof(r));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_NULL_POINTER, modbus_rtu_request_decode(frame, 8, NULL));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_NULL_POINTER, modbus_rtu_request_decode(NULL, 8, &r));
    assert_empty_request(&r);
    for (n = 0; n < 4; ++n) {
        TEST_ASSERT_EQUAL(MODBUS_STATUS_FRAME_TOO_SHORT, modbus_rtu_request_decode(frame, n, &r));
        assert_empty_request(&r);
    }
    TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_LENGTH, modbus_rtu_request_decode(frame, 257, &r));
    assert_empty_request(&r);
}

void test_slave_crc_corruption_and_order(void)
{
    uint8_t frame[8];
    modbus_request_t r;
    unsigned i;
    for (i = 0; i < sizeof(frame); ++i) {
        memcpy(frame, read_request, sizeof(frame));
        frame[i] ^= 1;
        memset(&r, 0xA5, sizeof(r));
        TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_CRC, modbus_rtu_request_decode(frame, sizeof(frame), &r));
        assert_empty_request(&r);
    }
    memcpy(frame, read_request, sizeof(frame));
    frame[6] = read_request[7]; frame[7] = read_request[6];
    TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_CRC, modbus_rtu_request_decode(frame, sizeof(frame), &r));
}

void test_slave_structure_lengths(void)
{
    uint8_t frame[260], payload[252] = {0};
    modbus_request_t r;
    unsigned f, count;
    for (f = 3; f <= 6; ++f) {
        if (f == 5) continue;
        for (count = 0; count <= 6; ++count) {
            size_t n;
            if (count == 4) continue;
            n = slave_frame(frame, 1, (uint8_t)f, payload, (uint8_t)count);
            TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_LENGTH, modbus_rtu_request_decode(frame, n, &r));
        }
    }
    for (count = 0; count <= 6; ++count) {
        size_t n;
        payload[4] = 4;
        n = slave_frame(frame, 1, 16, payload, (uint8_t)count);
        TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_LENGTH, modbus_rtu_request_decode(frame, n, &r));
    }
    /* Exactly 256 bytes: accepted as an ADU, rejected as an invalid quantity. */
    payload[2] = 0; payload[3] = 124; payload[4] = 247;
    count = (unsigned)slave_frame(frame, 1, 16, payload, 252);
    TEST_ASSERT_EQUAL_UINT32(256, count);
    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frame, count, &r));
    TEST_ASSERT_EQUAL_UINT8(3, r.exception);
}

void test_slave_rejects_concatenated_frames(void)
{
    uint8_t frames[16];
    modbus_request_t r;
    memcpy(frames, read_request, 8);
    memcpy(frames+8, read_request, 8);
    TEST_ASSERT_NOT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frames, sizeof(frames), &r));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK, modbus_rtu_request_decode(frames, 8, &r));
}

void test_slave_exception_fixed_vector(void)
{
    const uint8_t expected[] = {1, 0x83, 2, 0xC0, 0xF1};
    uint8_t frame[7];
    size_t n;
    memset(frame, 0xA5, sizeof(frame));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
        modbus_rtu_exception_encode(1, 3, 2, frame+1, 5, &n));
    TEST_ASSERT_EQUAL_UINT32(5, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame+1, 5);
    TEST_ASSERT_EQUAL_HEX8(0xA5, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, frame[6]);
}

void test_slave_exception_codes_and_arguments(void)
{
    const uint8_t codes[] = {1, 2, 3, 6};
    uint8_t frame[260], expected[260];
    size_t n, expected_n;
    unsigned i;
    for (i = 0; i < sizeof(codes); ++i) {
        expected_n = slave_frame(expected, 247, 0x90, &codes[i], 1);
        TEST_ASSERT_EQUAL(MODBUS_STATUS_OK,
            modbus_rtu_exception_encode(247, 16, codes[i], frame, 5, &n));
        TEST_ASSERT_EQUAL_UINT32(expected_n, n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, n);
    }
    TEST_ASSERT_EQUAL(MODBUS_STATUS_NULL_POINTER, modbus_rtu_exception_encode(1,3,2,frame,5,NULL));
    n = 99;
    TEST_ASSERT_EQUAL(MODBUS_STATUS_NULL_POINTER, modbus_rtu_exception_encode(1,3,2,NULL,5,&n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
    for (i = 0; i < 256; ++i) {
        if (i == 1 || i == 2 || i == 3 || i == 6) continue;
        n = 99;
        TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_FUNCTION,
            modbus_rtu_exception_encode(1,3,(uint8_t)i,frame,5,&n));
        TEST_ASSERT_EQUAL_UINT32(0, n);
    }
    for (i = 0; i < 256; ++i) {
        if (i >= 1 && i <= 247) continue;
        TEST_ASSERT_EQUAL(MODBUS_STATUS_INVALID_ADDRESS,
            modbus_rtu_exception_encode((uint8_t)i,3,2,frame,5,&n));
        TEST_ASSERT_EQUAL_UINT32(0, n);
    }
    memset(frame, 0xA5, sizeof(frame));
    TEST_ASSERT_EQUAL(MODBUS_STATUS_BUFFER_TOO_SMALL, modbus_rtu_exception_encode(1,3,2,frame,4,&n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
    TEST_ASSERT_EQUAL_HEX8(0xA5, frame[0]);
}


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
