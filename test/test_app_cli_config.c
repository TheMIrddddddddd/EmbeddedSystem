#include <string.h>

#include "unity.h"
#include "FreeRTOS.h"
#include "app_cli.h"
#include "app_config.h"
#include "sample_task.h"
#include "storage_task.h"

static unsigned s_critical_depth;
static TickType_t s_tick;
static char s_output[4096];
static uint16_t s_output_length;
static storage_task_persist_request_t s_request;
static uint32_t s_submit_count;
static uint8_t s_submit_should_fail;
static storage_task_persist_result_t s_result;
static uint8_t s_result_available;
static uint32_t s_last_baudrate;

void test_critical_enter(void)
{
    s_critical_depth++;
}

void test_critical_exit(void)
{
    TEST_ASSERT_TRUE(s_critical_depth > 0U);
    s_critical_depth--;
}

TickType_t xTaskGetTickCount(void)
{
    return s_tick;
}

void vTaskDelay(TickType_t ticks)
{
    s_tick += ticks;
}

uint16_t board_usart0_tx_free_get(void)
{
    return 512U;
}

uint16_t board_usart0_send_buffer(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) ||
        ((uint32_t)s_output_length + length >= sizeof(s_output)))
    {
        return 0U;
    }

    (void)memcpy(&s_output[s_output_length], data, length);
    s_output_length = (uint16_t)(s_output_length + length);
    s_output[s_output_length] = '\0';
    return length;
}

void board_usart1_rs485_baudrate_set(uint32_t baudrate)
{
    s_last_baudrate = baudrate;
}

int board_spi_flash_read_jedec_id(uint8_t id[3])
{
    if (id == NULL)
    {
        return 0;
    }

    id[0] = 0xC8U;
    id[1] = 0x40U;
    id[2] = 0x13U;
    return 1;
}

int board_rtc_time_get(board_rtc_time_t *time)
{
    if (time == NULL)
    {
        return 0;
    }

    (void)memset(time, 0, sizeof(*time));
    time->year = 2026U;
    time->month = 1U;
    time->date = 1U;
    return 1;
}

int board_rtc_time_set(const board_rtc_time_t *time)
{
    return (time != NULL) ? 1 : 0;
}

int sample_task_ratio_set(uint8_t channel, float ratio)
{
    return ((channel < SAMPLE_CHANNEL_COUNT) &&
            (ratio >= 0.0f) && (ratio <= 100.0f)) ? 1 : 0;
}

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag)
{
    if (diag == NULL)
    {
        return 0;
    }

    (void)memset(diag, 0, sizeof(*diag));
    diag->state = STORAGE_TASK_SDIO_STATE_READY;
    return 1;
}

int storage_task_persist_request_submit(
    const storage_task_persist_request_t *request)
{
    if ((request == NULL) || (s_submit_should_fail != 0U))
    {
        return 0;
    }

    s_request = *request;
    s_submit_count++;
    return 1;
}

int storage_task_persist_result_get(
    storage_task_persist_result_t *result,
    uint32_t timeout_ms)
{
    (void)timeout_ms;

    if ((result == NULL) || (s_result_available == 0U))
    {
        return 0;
    }

    *result = s_result;
    s_result_available = 0U;
    return 1;
}

static void test_set_result(uint8_t operation,
                            uint8_t status,
                            const uint8_t *payload,
                            uint16_t length)
{
    (void)memset(&s_result, 0, sizeof(s_result));
    s_result.request_id = s_request.request_id;
    s_result.operation = operation;
    s_result.status = status;
    s_result.payload_length = length;
    if ((payload != NULL) && (length > 0U))
    {
        (void)memcpy(s_result.payload, payload, length);
    }
    s_result_available = 1U;
}

void setUp(void)
{
    (void)memset(&s_request, 0, sizeof(s_request));
    (void)memset(&s_result, 0, sizeof(s_result));
    s_critical_depth = 0U;
    s_tick = 0U;
    s_output_length = 0U;
    s_output[0] = '\0';
    s_submit_count = 0U;
    s_submit_should_fail = 0U;
    s_result_available = 0U;
    s_last_baudrate = 0U;
    TEST_ASSERT_EQUAL_INT(1, app_config_init());
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0U, s_critical_depth);
}

static void test_config_save_submits_the_encoded_runtime_config(void)
{
    app_config_t config;

    TEST_ASSERT_EQUAL_INT(1, app_config_ratio_set(0U, 5.5f));
    TEST_ASSERT_EQUAL_INT(1, app_config_get(&config));

    app_cli_execute_line("config save");

    TEST_ASSERT_EQUAL_UINT32(1U, s_submit_count);
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_CONFIG_SAVE,
                            s_request.operation);
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_ORIGIN_CONTROL,
                            s_request.origin);
    TEST_ASSERT_EQUAL_UINT16(APP_CONFIG_SERIALIZED_SIZE,
                             s_request.payload_length);
    TEST_ASSERT_NOT_NULL(strstr(s_output, "config save pending"));

    test_set_result(STORAGE_TASK_PERSIST_CONFIG_SAVE,
                    STORAGE_TASK_PERSIST_STATUS_OK,
                    NULL,
                    0U);
    app_cli_storage_result_poll();
}

static void test_config_save_prints_success_after_matching_result(void)
{
    app_cli_execute_line("config save");
    test_set_result(STORAGE_TASK_PERSIST_CONFIG_SAVE,
                    STORAGE_TASK_PERSIST_STATUS_OK,
                    NULL,
                    0U);

    app_cli_storage_result_poll();

    TEST_ASSERT_NOT_NULL(strstr(s_output, "save to flash [OK]"));
}

static void test_config_read_decodes_and_prints_persisted_config(void)
{
    app_config_t expected;
    uint8_t payload[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t payload_length = 0U;

    (void)app_config_defaults(&expected);
    expected.device_id = 0x0123U;
    expected.sample_period_s = 15U;
    expected.rs485_baudrate = 57600U;
    expected.ratio[0] = 5.5f;
    TEST_ASSERT_EQUAL_INT(1, app_config_encode(&expected,
                                               payload,
                                               sizeof(payload),
                                               &payload_length));

    app_cli_execute_line("config read");
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_CONFIG_READ,
                            s_request.operation);
    test_set_result(STORAGE_TASK_PERSIST_CONFIG_READ,
                    STORAGE_TASK_PERSIST_STATUS_OK,
                    payload,
                    payload_length);

    app_cli_storage_result_poll();

    TEST_ASSERT_NOT_NULL(strstr(s_output, "device_id=0123"));
    TEST_ASSERT_NOT_NULL(strstr(s_output, "sample_period=15"));
    TEST_ASSERT_NOT_NULL(strstr(s_output, "ch0_ratio=5.50"));
    TEST_ASSERT_NOT_NULL(strstr(s_output, "baudrate=57600"));
}

static void test_second_config_request_is_rejected_while_pending(void)
{
    app_cli_execute_line("config save");
    app_cli_execute_line("config read");

    TEST_ASSERT_EQUAL_UINT32(1U, s_submit_count);
    TEST_ASSERT_NOT_NULL(strstr(s_output, "storage busy"));

    test_set_result(STORAGE_TASK_PERSIST_CONFIG_SAVE,
                    STORAGE_TASK_PERSIST_STATUS_OK,
                    NULL,
                    0U);
    app_cli_storage_result_poll();
}

static void test_storage_submit_failure_is_reported_without_pending_state(void)
{
    s_submit_should_fail = 1U;

    app_cli_execute_line("config save");

    TEST_ASSERT_EQUAL_UINT32(0U, s_submit_count);
    TEST_ASSERT_NOT_NULL(strstr(s_output, "storage busy"));
}

static void test_matching_result_is_required_before_clearing_pending_state(void)
{
    app_cli_execute_line("config save");
    s_result.request_id = s_request.request_id + 1U;
    s_result.operation = STORAGE_TASK_PERSIST_CONFIG_SAVE;
    s_result.status = STORAGE_TASK_PERSIST_STATUS_OK;
    s_result_available = 1U;

    app_cli_storage_result_poll();
    TEST_ASSERT_NOT_NULL(strstr(s_output, "config save pending"));

    test_set_result(STORAGE_TASK_PERSIST_CONFIG_SAVE,
                    STORAGE_TASK_PERSIST_STATUS_OK,
                    NULL,
                    0U);
    app_cli_storage_result_poll();
    TEST_ASSERT_NOT_NULL(strstr(s_output, "save to flash [OK]"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_save_submits_the_encoded_runtime_config);
    RUN_TEST(test_config_save_prints_success_after_matching_result);
    RUN_TEST(test_config_read_decodes_and_prints_persisted_config);
    RUN_TEST(test_second_config_request_is_rejected_while_pending);
    RUN_TEST(test_storage_submit_failure_is_reported_without_pending_state);
    RUN_TEST(test_matching_result_is_required_before_clearing_pending_state);
    return UNITY_END();
}
