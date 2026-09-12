#include <string.h>

#include "unity.h"
#include "app_protocol.h"
#include "app_config.h"
#include "sample_task.h"
#include "board_rtc.h"
#include "storage_task.h"

static app_config_t s_config;
static uint8_t s_alarm_query_payload[132];
static uint16_t s_alarm_query_length;
static uint8_t s_alarm_clear_called;

int app_config_get(app_config_t *out)
{
    *out = s_config;
    return 1;
}

int app_config_device_id_set(uint16_t device_id)
{
    s_config.device_id = device_id;
    return 1;
}

int app_config_baudrate_set(uint32_t baudrate)
{
    s_config.rs485_baudrate = baudrate;
    return 1;
}

int app_config_limit_set(uint8_t channel, float limit)
{
    s_config.limit[channel] = limit;
    return 1;
}

int app_config_ratio_set(uint8_t channel, float ratio)
{
    s_config.ratio[channel] = ratio;
    return 1;
}

int app_config_alarm_mode_set(uint8_t mode)
{
    if ((mode != 1U) && (mode != 2U))
    {
        return 0;
    }
    s_config.alarm_mode = mode;
    return 1;
}

int sample_task_snapshot_get(sample_snapshot_t *snapshot)
{
    snapshot->sequence = 1U;
    snapshot->value_ch0 = 1.0f;
    snapshot->value_ch1 = 2.0f;
    return 1;
}

int sample_task_ratio_set(uint8_t channel, float ratio)
{
    s_config.ratio[channel] = ratio;
    return 1;
}

int board_dac_output_set(uint16_t value)
{
    (void)value;
    return 1;
}

int board_rtc_time_get(board_rtc_time_t *time)
{
    memset(time, 0, sizeof(*time));
    time->year = 2026U;
    time->month = 1U;
    time->date = 1U;
    return 1;
}

int board_spi_flash_read_jedec_id(uint8_t jedec_id[3])
{
    jedec_id[0] = 0xC8U;
    jedec_id[1] = 0x40U;
    jedec_id[2] = 0x13U;
    return 1;
}

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag)
{
    diag->state = STORAGE_TASK_SDIO_STATE_READY;
    return 1;
}

uint8_t storage_task_fatfs_mounted_get(void)
{
    return 1U;
}

int storage_task_audit_event_submit(uint8_t event, uint8_t channel,
                                    float value0, float value1,
                                    uint32_t argument)
{
    (void)event; (void)channel; (void)value0; (void)value1; (void)argument;
    return 1;
}

int storage_task_alarm_records_get(uint8_t *payload, uint16_t capacity,
                                   uint16_t *length)
{
    if ((payload == NULL) || (length == NULL) ||
        (capacity < s_alarm_query_length))
    {
        return 0;
    }
    (void)memcpy(payload, s_alarm_query_payload, s_alarm_query_length);
    *length = s_alarm_query_length;
    return 1;
}

int storage_task_alarm_records_clear(void)
{
    s_alarm_clear_called = 1U;
    return 1;
}

void setUp(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_config.device_id = 1U;
    s_config.rs485_baudrate = 115200U;
    s_config.ratio[0] = 1.0f;
    s_config.ratio[1] = 1.0f;
    s_config.limit[0] = 2.5f;
    s_config.limit[1] = 10.5f;
    s_config.alarm_mode = 2U;
    (void)memset(s_alarm_query_payload, 0, sizeof(s_alarm_query_payload));
    s_alarm_query_length = 1U;
    s_alarm_clear_called = 0U;
}

void tearDown(void)
{
}

static void test_custom_set_baud_defers_hardware_apply(void)
{
    protocol_request_t request = {0};
    protocol_result_t result;

    request.request_id = 1U;
    request.device_address = 1U;
    request.protocol_kind = PROTOCOL_KIND_CUSTOM;
    request.operation = 0x0106U;
    request.payload_length = 4U;
    request.payload[0] = 0x00U;
    request.payload[1] = 0x00U;
    request.payload[2] = 0xE1U;
    request.payload[3] = 0x00U;

    app_protocol_execute(&request, &result);

    TEST_ASSERT_EQUAL_UINT8(0U, result.status);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_RESULT_APPLY_BAUD, result.apply_flags);
    TEST_ASSERT_EQUAL_UINT32(57600U, result.next_baudrate);
}

static void test_custom_alarm_mode_selects_active_or_passive_reporting(void)
{
    protocol_request_t request = {0};
    protocol_result_t result;

    request.device_address = 1U;
    request.protocol_kind = PROTOCOL_KIND_CUSTOM;
    request.operation = 0x0601U;
    request.payload_length = 1U;
    request.payload[0] = 1U;

    app_protocol_execute(&request, &result);

    TEST_ASSERT_EQUAL_UINT8(0U, result.status);
    TEST_ASSERT_EQUAL_UINT8(1U, s_config.alarm_mode);
    TEST_ASSERT_EQUAL_UINT8(1U, app_protocol_alarm_report_enabled());

    request.payload[0] = 3U;
    app_protocol_execute(&request, &result);

    TEST_ASSERT_EQUAL_UINT8(0x04U, result.status);
    TEST_ASSERT_EQUAL_UINT8(1U, s_config.alarm_mode);

    request.payload[0] = 2U;
    app_protocol_execute(&request, &result);
    TEST_ASSERT_EQUAL_UINT8(0U, result.status);
    TEST_ASSERT_EQUAL_UINT8(0U, app_protocol_alarm_report_enabled());
}

static void test_custom_alarm_query_and_clear_use_storage_service(void)
{
    protocol_request_t request = {0};
    protocol_result_t result;

    s_alarm_query_payload[0] = 1U;
    s_alarm_query_payload[1] = 0x00U;
    s_alarm_query_payload[2] = 0x00U;
    s_alarm_query_payload[3] = 0x00U;
    s_alarm_query_payload[4] = 0x2AU;
    s_alarm_query_length = 14U;

    request.device_address = 1U;
    request.protocol_kind = PROTOCOL_KIND_CUSTOM;
    request.operation = 0x0602U;
    app_protocol_execute(&request, &result);

    TEST_ASSERT_EQUAL_UINT8(0U, result.status);
    TEST_ASSERT_EQUAL_UINT16(s_alarm_query_length, result.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(s_alarm_query_payload, result.payload,
                                  s_alarm_query_length);

    request.operation = 0x0603U;
    app_protocol_execute(&request, &result);

    TEST_ASSERT_EQUAL_UINT8(0U, result.status);
    TEST_ASSERT_EQUAL_UINT8(1U, s_alarm_clear_called);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_custom_set_baud_defers_hardware_apply);
    RUN_TEST(test_custom_alarm_mode_selects_active_or_passive_reporting);
    RUN_TEST(test_custom_alarm_query_and_clear_use_storage_service);
    return UNITY_END();
}
