#include <string.h>

#include "unity.h"
#include "app_config.h"
#include "storage_persistence.h"

#define TEST_FLASH_SIZE       0x00080000UL
#define TEST_SECTOR_SIZE      0x00001000UL
#define TEST_SECTOR_A         0x00000000UL
#define TEST_SECTOR_B         0x00001000UL
#define TEST_RECORD_OFFSET    12U
#define TEST_RECORD_CRC_OFFSET 5U
#define TEST_RECORD_SIZE      47U /* 10B header + 6B key + 31B config */
#define TEST_ALARM_RECORD_SIZE 13U
#define TEST_ALARM_RECORD_MAX  10U
#define TEST_ALARM_QUERY_SIZE  (1U + (TEST_ALARM_RECORD_SIZE * TEST_ALARM_RECORD_MAX))

static uint8_t s_flash[TEST_FLASH_SIZE];
static int s_program_fail_after;
static int s_program_calls;
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

int board_spi_flash_read(uint32_t address, uint8_t *data, uint32_t length)
{
    if ((data == NULL) ||
        (length == 0U) ||
        ((uint64_t)address + length > TEST_FLASH_SIZE))
    {
        return 0;
    }

    (void)memcpy(data, &s_flash[address], length);
    return 1;
}

int board_spi_flash_page_program(uint32_t address,
                                 const uint8_t *data,
                                 uint32_t length)
{
    uint32_t index;

    if ((data == NULL) ||
        (length == 0U) ||
        (length > 256U) ||
        (((address & 0xFFU) + length) > 256U) ||
        ((uint64_t)address + length > TEST_FLASH_SIZE))
    {
        return 0;
    }

    if ((s_program_fail_after >= 0) &&
        (s_program_calls >= s_program_fail_after))
    {
        return 0;
    }

    s_program_calls++;

    /* NOR Flash 编程只能将 bit 从 1 写为 0。 */
    for (index = 0U; index < length; index++)
    {
        s_flash[address + index] &= data[index];
    }

    return 1;
}

int board_spi_flash_sector_erase(uint32_t address)
{
    if ((address % TEST_SECTOR_SIZE) != 0U ||
        ((uint64_t)address + TEST_SECTOR_SIZE > TEST_FLASH_SIZE))
    {
        return 0;
    }

    (void)memset(&s_flash[address], 0xFF, TEST_SECTOR_SIZE);
    return 1;
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

static app_config_t test_config(uint16_t device_id, uint32_t baudrate)
{
    app_config_t config;

    (void)app_config_defaults(&config);
    config.device_id = device_id;
    config.rs485_baudrate = baudrate;
    config.sample_period_s = 10U;
    config.ratio[0] = 12.5f;
    config.ratio[1] = 0.25f;
    config.limit[0] = 123.5f;
    config.limit[1] = 49.75f;
    return config;
}

static int test_config_encode(const app_config_t *config,
                              uint8_t payload[APP_CONFIG_SERIALIZED_SIZE])
{
    uint16_t length = 0U;

    return app_config_encode(config,
                             payload,
                             APP_CONFIG_SERIALIZED_SIZE,
                             &length) &&
           (length == APP_CONFIG_SERIALIZED_SIZE);
}

static void test_config_payload_equal(const uint8_t *expected,
                                      const uint8_t *actual,
                                      uint16_t length)
{
    TEST_ASSERT_EQUAL_UINT16(APP_CONFIG_SERIALIZED_SIZE, length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, length);
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

static void test_initialization_on_blank_flash_creates_empty_kv(void)
{
    uint8_t payload[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t length = 0U;

    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_NOT_FOUND,
                          storage_persistence_config_read(payload,
                                                          sizeof(payload),
                                                          &length));
    TEST_ASSERT_EQUAL_UINT16(0U, length);
    TEST_ASSERT_EQUAL_UINT8(0x54U, s_flash[TEST_SECTOR_A + 0U]);
    TEST_ASSERT_EQUAL_UINT8(0x3CU, s_flash[TEST_SECTOR_A + 8U]);
}

static void test_config_save_and_read_round_trip(void)
{
    app_config_t config = test_config(0x0123U, 57600U);
    uint8_t expected[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t actual[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t actual_length = 0U;

    TEST_ASSERT_TRUE(test_config_encode(&config, expected));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_save(
                              expected,
                              sizeof(expected)));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_read(
                              actual,
                              sizeof(actual),
                              &actual_length));
    test_config_payload_equal(expected, actual, actual_length);
}

static void test_reopen_keeps_the_latest_config(void)
{
    app_config_t config = test_config(0x0042U, 38400U);
    uint8_t expected[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t actual[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t actual_length = 0U;

    TEST_ASSERT_TRUE(test_config_encode(&config, expected));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_save(
                              expected,
                              sizeof(expected)));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_read(
                              actual,
                              sizeof(actual),
                              &actual_length));
    test_config_payload_equal(expected, actual, actual_length);
}

static void test_failed_rotation_keeps_previous_config_after_reopen(void)
{
    app_config_t previous = test_config(0x0042U, 38400U);
    app_config_t next = test_config(0x0055U, 19200U);
    uint8_t previous_payload[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t next_payload[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t actual[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t actual_length = 0U;

    TEST_ASSERT_TRUE(test_config_encode(&previous, previous_payload));
    TEST_ASSERT_TRUE(test_config_encode(&next, next_payload));

    s_program_calls = 0;
    s_program_fail_after = 4;
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_FLASH_ERROR,
                          storage_persistence_config_save(
                              next_payload,
                              sizeof(next_payload)));

    s_program_fail_after = -1;
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_read(
                              actual,
                              sizeof(actual),
                              &actual_length));
    test_config_payload_equal(previous_payload, actual, actual_length);
}

static void test_flash_diag_returns_expected_jedec_id(void)
{
    uint8_t id[3] = {0U};

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_flash_diag(id));
    TEST_ASSERT_EQUAL_UINT8(0xC8U, id[0]);
    TEST_ASSERT_EQUAL_UINT8(0x40U, id[1]);
    TEST_ASSERT_EQUAL_UINT8(0x13U, id[2]);
}

/* 验证上电计数首次从 1 开始，之后经 FlashKV 读回继续递增。 */
static void test_boot_count_persists_and_increments(void)
{
    uint32_t count = 0U;
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK, storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_boot_count_next(&count));
    TEST_ASSERT_EQUAL_UINT32(1U, count);
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_boot_count_next(&count));
    TEST_ASSERT_EQUAL_UINT32(2U, count);
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK, storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_boot_count_next(&count));
    TEST_ASSERT_EQUAL_UINT32(3U, count);
}

/* 验证上电计数参数和 Flash 写失败不会发布伪造计数。 */
static void test_boot_count_errors(void)
{
    uint32_t count = 99U;
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK, storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT,
                          storage_persistence_boot_count_next(NULL));
    s_program_fail_after = 0;
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_FLASH_ERROR,
                          storage_persistence_boot_count_next(&count));
    TEST_ASSERT_EQUAL_UINT32(99U, count);
    s_program_fail_after = -1;
}

/* 验证长期运行下 boot_count 与 config 的重复写入不会写满记录层。
 * 每次上电都会追加一条 boot_count，若提交时不压缩历史记录，
 * 记录数超过 FLASH_KV_MAX_RECORDS 后所有写入都会失败。 */
static void test_repeated_writes_do_not_exhaust_records(void)
{
    uint32_t count = 0U;
    app_config_t config = test_config(0x0123U, 57600U);
    uint8_t expected[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t actual[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t actual_length = 0U;
    uint8_t index;

    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());

    for (index = 0U; index < 20U; index++)
    {
        TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                              storage_persistence_boot_count_next(&count));
    }
    TEST_ASSERT_EQUAL_UINT32(20U, count);

    TEST_ASSERT_TRUE(test_config_encode(&config, expected));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_save(
                              expected,
                              sizeof(expected)));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_read(
                              actual,
                              sizeof(actual),
                              &actual_length));
    test_config_payload_equal(expected, actual, actual_length);
}

static void test_crc_corruption_is_reported_as_data_error(void)
{
    app_config_t config = test_config(0x0077U, 115200U);
    uint8_t payload[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t actual[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t actual_length = 0U;
    int corrupted = 0;
    uint32_t sector_base;

    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());

    TEST_ASSERT_TRUE(test_config_encode(&config, payload));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_save(
                              payload,
                              sizeof(payload)));

    /* 在两个扇区中查找 config 记录并破坏 CRC，保证最新扇区读取失败。 */
    for (sector_base = 0U;
         (sector_base == 0U) || (sector_base == TEST_SECTOR_SIZE);
         sector_base += TEST_SECTOR_SIZE)
    {
        uint32_t offset = sector_base + TEST_RECORD_OFFSET;

        while ((offset + 10U) <= (sector_base + TEST_SECTOR_SIZE))
        {
            uint8_t *record = &s_flash[offset];
            size_t key_length = record[2];
            size_t value_length = (size_t)record[3] |
                                  ((size_t)record[4] << 8U);
            size_t record_size = 10U + key_length + value_length;

            if ((record[0] != 0x5AU) || (record[1] != 0xA5U) ||
                (key_length == 0U) ||
                (record_size > (TEST_SECTOR_SIZE - (offset - sector_base))) ||
                (record[9] != 0x5AU))
            {
                break;
            }

            if ((key_length == 6U) &&
                (memcmp(&record[10], "config", 6U) == 0))
            {
                record[TEST_RECORD_CRC_OFFSET] ^= 0x01U;
                corrupted = 1;
                break;
            }

            offset += record_size;
        }
    }

    TEST_ASSERT_TRUE(corrupted);

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_DATA_ERROR,
                          storage_persistence_config_read(
                              actual,
                              sizeof(actual),
                              &actual_length));
    TEST_ASSERT_EQUAL_UINT16(0U, actual_length);
}

static void test_request_handler_saves_and_reads_config(void)
{
    app_config_t config = test_config(0x0088U, 9600U);
    storage_task_persist_request_t request;
    storage_task_persist_result_t result;
    uint8_t payload[APP_CONFIG_SERIALIZED_SIZE];

    TEST_ASSERT_TRUE(test_config_encode(&config, payload));

    (void)memset(&request, 0, sizeof(request));
    request.request_id = 77U;
    request.operation = STORAGE_TASK_PERSIST_CONFIG_SAVE;
    request.origin = STORAGE_TASK_PERSIST_ORIGIN_CONTROL;
    request.payload_length = sizeof(payload);
    (void)memcpy(request.payload, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_INT(1,
                          storage_persistence_request_handle(&request,
                                                             &result));
    TEST_ASSERT_EQUAL_UINT32(request.request_id, result.request_id);
    TEST_ASSERT_EQUAL_UINT8(request.operation, result.operation);
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_STATUS_OK, result.status);
    TEST_ASSERT_EQUAL_UINT16(0U, result.payload_length);

    request.request_id++;
    request.operation = STORAGE_TASK_PERSIST_CONFIG_READ;
    request.payload_length = 0U;
    (void)memset(request.payload, 0xA5, sizeof(request.payload));

    TEST_ASSERT_EQUAL_INT(1,
                          storage_persistence_request_handle(&request,
                                                             &result));
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_STATUS_OK, result.status);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), result.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload,
                                  result.payload,
                                  sizeof(payload));
}

static void test_request_handler_maps_invalid_config_to_data_error(void)
{
    storage_task_persist_request_t request;
    storage_task_persist_result_t result;

    (void)memset(&request, 0, sizeof(request));
    request.request_id = 78U;
    request.operation = STORAGE_TASK_PERSIST_CONFIG_SAVE;
    request.payload_length = APP_CONFIG_SERIALIZED_SIZE;
    (void)memset(request.payload, 0xFF, request.payload_length);

    TEST_ASSERT_EQUAL_INT(1,
                          storage_persistence_request_handle(&request,
                                                             &result));
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_STATUS_DATA_ERROR,
                            result.status);
}

static void test_request_handler_returns_flash_diagnostic_result(void)
{
    storage_task_persist_request_t request;
    storage_task_persist_result_t result;

    (void)memset(&request, 0, sizeof(request));
    request.request_id = 79U;
    request.operation = STORAGE_TASK_PERSIST_FLASH_DIAG;

    TEST_ASSERT_EQUAL_INT(1,
                          storage_persistence_request_handle(&request,
                                                             &result));
    TEST_ASSERT_EQUAL_UINT8(STORAGE_TASK_PERSIST_STATUS_OK, result.status);
    TEST_ASSERT_EQUAL_UINT16(3U, result.payload_length);
    TEST_ASSERT_EQUAL_UINT8(0xC8U, result.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x40U, result.payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x13U, result.payload[2]);
}

static uint32_t test_load_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static float test_load_float_be(const uint8_t *data)
{
    uint8_t little[4];
    float value;

    little[0] = data[3];
    little[1] = data[2];
    little[2] = data[1];
    little[3] = data[0];
    (void)memcpy(&value, little, sizeof(value));
    return value;
}

static void test_alarm_records_keep_latest_ten_in_reverse_order(void)
{
    app_config_t config = test_config(0x0011U, 115200U);
    uint8_t config_payload[APP_CONFIG_SERIALIZED_SIZE];
    uint8_t payload[TEST_ALARM_QUERY_SIZE] = {0U};
    uint16_t length = 0U;
    uint32_t boot_count = 0U;
    uint8_t index;

    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    TEST_ASSERT_TRUE(test_config_encode(&config, config_payload));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_config_save(
                              config_payload, sizeof(config_payload)));
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_boot_count_next(&boot_count));
    TEST_ASSERT_EQUAL_UINT32(1U, boot_count);

    for (index = 0U; index < 12U; index++)
    {
        TEST_ASSERT_EQUAL_INT(
            STORAGE_PERSISTENCE_STATUS_OK,
            storage_persistence_alarm_record_append(
                100U + index, (uint8_t)(index & 1U),
                2.5f + (float)index, 3.5f + (float)index));
    }

    TEST_ASSERT_EQUAL_INT(
        STORAGE_PERSISTENCE_STATUS_OK,
        storage_persistence_alarm_records_read(payload, sizeof(payload), &length));
    TEST_ASSERT_EQUAL_UINT16(TEST_ALARM_QUERY_SIZE, length);
    TEST_ASSERT_EQUAL_UINT8(TEST_ALARM_RECORD_MAX, payload[0]);

    for (index = 0U; index < TEST_ALARM_RECORD_MAX; index++)
    {
        const uint8_t *record = &payload[1U +
                                         (index * TEST_ALARM_RECORD_SIZE)];
        uint8_t source_index = (uint8_t)(11U - index);

        TEST_ASSERT_EQUAL_UINT32(100U + source_index,
                                 test_load_u32_be(record));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(source_index & 1U), record[4]);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f + (float)source_index,
                                 test_load_float_be(&record[5]));
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.5f + (float)source_index,
                                 test_load_float_be(&record[9]));
    }

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    (void)memset(payload, 0, sizeof(payload));
    length = 0U;
    TEST_ASSERT_EQUAL_INT(
        STORAGE_PERSISTENCE_STATUS_OK,
        storage_persistence_alarm_records_read(payload, sizeof(payload), &length));
    TEST_ASSERT_EQUAL_UINT8(TEST_ALARM_RECORD_MAX, payload[0]);
    TEST_ASSERT_EQUAL_UINT32(111U, test_load_u32_be(&payload[1]));
}

static void test_alarm_records_clear_returns_empty_and_allows_reuse(void)
{
    uint8_t payload[TEST_ALARM_QUERY_SIZE] = {0U};
    uint16_t length = 0U;
    uint8_t index;

    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_fail_after = -1;
    s_program_calls = 0;

    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_init());
    for (index = 0U; index < TEST_ALARM_RECORD_MAX; index++)
    {
        TEST_ASSERT_EQUAL_INT(
            STORAGE_PERSISTENCE_STATUS_OK,
            storage_persistence_alarm_record_append(
                1234U + index, (uint8_t)(index & 1U), 10.5f, 11.25f));
    }
    TEST_ASSERT_EQUAL_INT(STORAGE_PERSISTENCE_STATUS_OK,
                          storage_persistence_alarm_records_clear());

    TEST_ASSERT_EQUAL_INT(
        STORAGE_PERSISTENCE_STATUS_OK,
        storage_persistence_alarm_records_read(payload, sizeof(payload), &length));
    TEST_ASSERT_EQUAL_UINT16(1U, length);
    TEST_ASSERT_EQUAL_UINT8(0U, payload[0]);

    TEST_ASSERT_EQUAL_INT(
        STORAGE_PERSISTENCE_STATUS_OK,
        storage_persistence_alarm_record_append(5678U, 0U, 1.25f, 2.5f));
    (void)memset(payload, 0, sizeof(payload));
    length = 0U;
    TEST_ASSERT_EQUAL_INT(
        STORAGE_PERSISTENCE_STATUS_OK,
        storage_persistence_alarm_records_read(payload, sizeof(payload), &length));
    TEST_ASSERT_EQUAL_UINT8(1U, payload[0]);
    TEST_ASSERT_EQUAL_UINT32(5678U, test_load_u32_be(&payload[1]));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initialization_on_blank_flash_creates_empty_kv);
    RUN_TEST(test_config_save_and_read_round_trip);
    RUN_TEST(test_reopen_keeps_the_latest_config);
    RUN_TEST(test_failed_rotation_keeps_previous_config_after_reopen);
    RUN_TEST(test_flash_diag_returns_expected_jedec_id);
    RUN_TEST(test_boot_count_persists_and_increments);
    RUN_TEST(test_boot_count_errors);
    RUN_TEST(test_repeated_writes_do_not_exhaust_records);
    RUN_TEST(test_crc_corruption_is_reported_as_data_error);
    RUN_TEST(test_request_handler_saves_and_reads_config);
    RUN_TEST(test_request_handler_maps_invalid_config_to_data_error);
    RUN_TEST(test_request_handler_returns_flash_diagnostic_result);
    RUN_TEST(test_alarm_records_keep_latest_ten_in_reverse_order);
    RUN_TEST(test_alarm_records_clear_returns_empty_and_allows_reuse);
    return UNITY_END();
}
