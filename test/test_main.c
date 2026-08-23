/*
 * test_main.c - Unity test entry point (M2 PC unit tests)
 */

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* test cases from sibling files */
void test_crc16_standard_vector(void);
void test_crc16_empty(void);
void test_crc16_modbus_frame_vector(void);
void test_crc32_standard_vector(void);
void test_crc32_empty(void);
void test_crc32_single_char(void);
void test_ringbuffer_basic_empty_full(void);
void test_ringbuffer_put_get_roundtrip(void);
void test_ringbuffer_wraparound(void);
void test_ringbuffer_put_force_overwrite(void);
void test_ringbuffer_peek_consuming(void);
void test_ringbuffer_capacity_2048(void);
void test_protocol_encode_empty_command(void);
void test_protocol_encode_response_payload(void);
void test_protocol_encode_rejects_invalid_arguments(void);
void test_protocol_encode_rejects_invalid_payload_and_type(void);
void test_protocol_encode_rejects_small_buffer(void);
void test_protocol_encode_rejects_reserved_address(void);
void test_protocol_encode_maximum_payload(void);
void test_protocol_decode_empty_command(void);
void test_protocol_decode_response_payload(void);
void test_protocol_decode_rejects_invalid_arguments(void);
void test_protocol_decode_rejects_short_frame(void);
void test_protocol_decode_rejects_fixed_field_errors(void);
void test_protocol_decode_rejects_length_errors(void);
void test_protocol_decode_rejects_crc_and_tail_errors(void);
void test_protocol_decode_consumes_one_frame(void);
void test_modbus_encode_read_request(void);
void test_modbus_encode_empty_data(void);
void test_modbus_decode_read_response(void);
void test_modbus_decode_accepts_trailing_bytes(void);
void test_modbus_rejects_invalid_arguments(void);
void test_modbus_rejects_small_or_short_frames(void);
void test_modbus_rejects_crc_error(void);
void test_modbus_rejects_invalid_address_and_function(void);

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_standard_vector);
    RUN_TEST(test_crc16_empty);
    RUN_TEST(test_crc16_modbus_frame_vector);
    RUN_TEST(test_crc32_standard_vector);
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_single_char);

    RUN_TEST(test_ringbuffer_basic_empty_full);
    RUN_TEST(test_ringbuffer_put_get_roundtrip);
    RUN_TEST(test_ringbuffer_wraparound);
    RUN_TEST(test_ringbuffer_put_force_overwrite);
    RUN_TEST(test_ringbuffer_peek_consuming);
    RUN_TEST(test_ringbuffer_capacity_2048);

    RUN_TEST(test_protocol_encode_empty_command);
    RUN_TEST(test_protocol_encode_response_payload);
    RUN_TEST(test_protocol_encode_rejects_invalid_arguments);
    RUN_TEST(test_protocol_encode_rejects_invalid_payload_and_type);
    RUN_TEST(test_protocol_encode_rejects_small_buffer);
    RUN_TEST(test_protocol_encode_rejects_reserved_address);
    RUN_TEST(test_protocol_encode_maximum_payload);

    RUN_TEST(test_protocol_decode_empty_command);
    RUN_TEST(test_protocol_decode_response_payload);
    RUN_TEST(test_protocol_decode_rejects_invalid_arguments);
    RUN_TEST(test_protocol_decode_rejects_short_frame);
    RUN_TEST(test_protocol_decode_rejects_fixed_field_errors);
    RUN_TEST(test_protocol_decode_rejects_length_errors);
    RUN_TEST(test_protocol_decode_rejects_crc_and_tail_errors);
    RUN_TEST(test_protocol_decode_consumes_one_frame);

    RUN_TEST(test_modbus_encode_read_request);
    RUN_TEST(test_modbus_encode_empty_data);
    RUN_TEST(test_modbus_decode_read_response);
    RUN_TEST(test_modbus_decode_accepts_trailing_bytes);
    RUN_TEST(test_modbus_rejects_invalid_arguments);
    RUN_TEST(test_modbus_rejects_small_or_short_frames);
    RUN_TEST(test_modbus_rejects_crc_error);
    RUN_TEST(test_modbus_rejects_invalid_address_and_function);

    return UNITY_END();
}
