#include <math.h>
#include <string.h>

#include "unity.h"
#include "app_config.h"

static unsigned s_critical_depth;

void test_critical_enter(void)
{
    s_critical_depth++;
}

void test_critical_exit(void)
{
    TEST_ASSERT_TRUE(s_critical_depth > 0U);
    s_critical_depth--;
}

static app_config_t test_complete_config(void)
{
    app_config_t config;

    (void)memset(&config, 0, sizeof(config));
    config.device_id = 0x0123U;
    config.sample_period_s = 10U;
    config.protocol_mode = 0U;
    config.alarm_mode = 1U;
    config.local_sample_enabled = 1U;
    config.hide_mode = 1U;
    config.rs485_baudrate = 57600U;
    config.ratio[0] = 12.5f;
    config.ratio[1] = 0.25f;
    config.limit[0] = 123.5f;
    config.limit[1] = 49.75f;

    return config;
}

static void test_config_fields_equal(const app_config_t *expected,
                                     const app_config_t *actual)
{
    TEST_ASSERT_EQUAL_UINT16(expected->device_id, actual->device_id);
    TEST_ASSERT_EQUAL_UINT8(expected->sample_period_s, actual->sample_period_s);
    TEST_ASSERT_EQUAL_UINT8(expected->protocol_mode, actual->protocol_mode);
    TEST_ASSERT_EQUAL_UINT8(expected->alarm_mode, actual->alarm_mode);
    TEST_ASSERT_EQUAL_UINT8(expected->local_sample_enabled,
                            actual->local_sample_enabled);
    TEST_ASSERT_EQUAL_UINT8(expected->hide_mode, actual->hide_mode);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->reserved, actual->reserved,
                                  sizeof(expected->reserved));
    TEST_ASSERT_EQUAL_UINT32(expected->rs485_baudrate,
                             actual->rs485_baudrate);
    TEST_ASSERT_EQUAL_FLOAT(expected->ratio[0], actual->ratio[0]);
    TEST_ASSERT_EQUAL_FLOAT(expected->ratio[1], actual->ratio[1]);
    TEST_ASSERT_EQUAL_FLOAT(expected->limit[0], actual->limit[0]);
    TEST_ASSERT_EQUAL_FLOAT(expected->limit[1], actual->limit[1]);
}

void setUp(void)
{
    s_critical_depth = 0U;
    TEST_ASSERT_EQUAL_INT(1, app_config_init());
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0U, s_critical_depth);
}

static void test_defaults_are_available_as_a_complete_config(void)
{
    app_config_t config;

    TEST_ASSERT_EQUAL_INT(1, app_config_defaults(&config));
    TEST_ASSERT_EQUAL_UINT16(1U, config.device_id);
    TEST_ASSERT_EQUAL_UINT8(5U, config.sample_period_s);
    TEST_ASSERT_EQUAL_UINT8(0U, config.protocol_mode);
    TEST_ASSERT_EQUAL_UINT8(2U, config.alarm_mode);
    TEST_ASSERT_EQUAL_UINT32(115200U, config.rs485_baudrate);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, config.ratio[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, config.ratio[1]);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, config.limit[0]);
    TEST_ASSERT_EQUAL_FLOAT(10.5f, config.limit[1]);
}

static void test_validate_accepts_a_complete_custom_config(void)
{
    app_config_t config = test_complete_config();

    TEST_ASSERT_EQUAL_INT(1, app_config_validate(&config));
}

static void test_validate_rejects_modbus_device_id_out_of_range(void)
{
    app_config_t config = test_complete_config();

    config.protocol_mode = 1U;
    config.device_id = 248U;

    TEST_ASSERT_EQUAL_INT(0, app_config_validate(&config));
}

static void test_validate_rejects_nonfinite_ratio(void)
{
    app_config_t config = test_complete_config();

    config.ratio[0] = NAN;

    TEST_ASSERT_EQUAL_INT(0, app_config_validate(&config));
}

static void test_validate_rejects_unknown_baudrate(void)
{
    app_config_t config = test_complete_config();

    config.rs485_baudrate = 12345U;

    TEST_ASSERT_EQUAL_INT(0, app_config_validate(&config));
}

static void test_encode_and_decode_round_trip_preserves_all_fields(void)
{
    app_config_t expected = test_complete_config();
    app_config_t actual;
    uint8_t encoded[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t encoded_length = 0U;

    TEST_ASSERT_EQUAL_INT(1, app_config_encode(&expected,
                                               encoded,
                                               sizeof(encoded),
                                               &encoded_length));
    TEST_ASSERT_EQUAL_UINT16(APP_CONFIG_SERIALIZED_SIZE, encoded_length);
    TEST_ASSERT_EQUAL_UINT8(APP_CONFIG_PERSISTENCE_VERSION, encoded[0]);

    (void)memset(&actual, 0xA5, sizeof(actual));
    TEST_ASSERT_EQUAL_INT(1, app_config_decode(encoded,
                                               encoded_length,
                                               &actual));
    test_config_fields_equal(&expected, &actual);
}

static void test_encode_rejects_an_output_buffer_that_is_too_small(void)
{
    app_config_t config = test_complete_config();
    uint8_t encoded[APP_CONFIG_SERIALIZED_SIZE - 1U];
    uint16_t encoded_length = 99U;

    TEST_ASSERT_EQUAL_INT(0, app_config_encode(&config,
                                               encoded,
                                               sizeof(encoded),
                                               &encoded_length));
    TEST_ASSERT_EQUAL_UINT16(0U, encoded_length);
}

static void test_decode_rejects_bad_version_without_overwriting_output(void)
{
    app_config_t expected = test_complete_config();
    app_config_t actual = test_complete_config();
    uint8_t encoded[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t encoded_length = 0U;

    TEST_ASSERT_EQUAL_INT(1, app_config_encode(&expected,
                                               encoded,
                                               sizeof(encoded),
                                               &encoded_length));
    encoded[0] = (uint8_t)(APP_CONFIG_PERSISTENCE_VERSION + 1U);

    actual.device_id = 0x0555U;
    TEST_ASSERT_EQUAL_INT(0, app_config_decode(encoded,
                                               encoded_length,
                                               &actual));
    TEST_ASSERT_EQUAL_UINT16(0x0555U, actual.device_id);
}

static void test_decode_rejects_invalid_values_without_overwriting_output(void)
{
    app_config_t expected = test_complete_config();
    app_config_t actual = test_complete_config();
    uint8_t encoded[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t encoded_length = 0U;

    TEST_ASSERT_EQUAL_INT(1, app_config_encode(&expected,
                                               encoded,
                                               sizeof(encoded),
                                               &encoded_length));
    encoded[1] = 0U;
    encoded[2] = 0U;

    actual.device_id = 0x0666U;
    TEST_ASSERT_EQUAL_INT(0, app_config_decode(encoded,
                                               encoded_length,
                                               &actual));
    TEST_ASSERT_EQUAL_UINT16(0x0666U, actual.device_id);
}

static void test_apply_replaces_the_complete_runtime_configuration(void)
{
    app_config_t expected = test_complete_config();
    app_config_t actual;

    TEST_ASSERT_EQUAL_INT(1, app_config_apply(&expected));
    TEST_ASSERT_EQUAL_INT(1, app_config_get(&actual));
    test_config_fields_equal(&expected, &actual);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_available_as_a_complete_config);
    RUN_TEST(test_validate_accepts_a_complete_custom_config);
    RUN_TEST(test_validate_rejects_modbus_device_id_out_of_range);
    RUN_TEST(test_validate_rejects_nonfinite_ratio);
    RUN_TEST(test_validate_rejects_unknown_baudrate);
    RUN_TEST(test_encode_and_decode_round_trip_preserves_all_fields);
    RUN_TEST(test_encode_rejects_an_output_buffer_that_is_too_small);
    RUN_TEST(test_decode_rejects_bad_version_without_overwriting_output);
    RUN_TEST(test_decode_rejects_invalid_values_without_overwriting_output);
    RUN_TEST(test_apply_replaces_the_complete_runtime_configuration);
    return UNITY_END();
}
