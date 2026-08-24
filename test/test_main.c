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
void test_cli_dispatches_single_command(void);
void test_cli_splits_arguments_and_ignores_extra_spaces(void);
void test_cli_rejects_empty_and_unknown_commands(void);
void test_cli_reports_handler_argument_error(void);
void test_cli_reports_output_buffer_error(void);
void test_cli_rejects_invalid_input_pointers(void);
void test_cli_rejects_line_too_long(void);
void test_flash_kv_initializes_empty_storage(void);
void test_flash_kv_sets_and_gets_value(void);
void test_flash_kv_latest_value_replaces_previous_value(void);
void test_flash_kv_keeps_different_keys_independent(void);
void test_flash_kv_rejects_invalid_arguments(void);
void test_flash_kv_rejects_oversized_key_and_value(void);
void test_flash_kv_reports_small_output_buffer(void);
void test_flash_kv_reports_no_space(void);
void test_flash_kv_recovers_records_after_reinitialization(void);

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

    RUN_TEST(test_cli_dispatches_single_command);
    RUN_TEST(test_cli_splits_arguments_and_ignores_extra_spaces);
    RUN_TEST(test_cli_rejects_empty_and_unknown_commands);
    RUN_TEST(test_cli_reports_handler_argument_error);
    RUN_TEST(test_cli_reports_output_buffer_error);
    RUN_TEST(test_cli_rejects_invalid_input_pointers);
    RUN_TEST(test_cli_rejects_line_too_long);

    RUN_TEST(test_flash_kv_initializes_empty_storage);
    RUN_TEST(test_flash_kv_sets_and_gets_value);
    RUN_TEST(test_flash_kv_latest_value_replaces_previous_value);
    RUN_TEST(test_flash_kv_keeps_different_keys_independent);
    RUN_TEST(test_flash_kv_rejects_invalid_arguments);
    RUN_TEST(test_flash_kv_rejects_oversized_key_and_value);
    RUN_TEST(test_flash_kv_reports_small_output_buffer);
    RUN_TEST(test_flash_kv_reports_no_space);
    RUN_TEST(test_flash_kv_recovers_records_after_reinitialization);

    return UNITY_END();
}
