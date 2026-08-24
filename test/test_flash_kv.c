/* PC tests for the FlashKV public interface. */

#include "unity.h"
#include "flash_kv.h"

#include <string.h>

static void flash_kv_prepare(flash_kv_t *context,
                             uint8_t *storage,
                             size_t storage_size)
{
    memset(storage, 0xFF, storage_size);
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_init(context, storage, storage_size));
}

void test_flash_kv_initializes_empty_storage(void)
{
    flash_kv_t context;
    uint8_t storage[128];
    uint8_t value[8];
    size_t value_length = 99U;

    flash_kv_prepare(&context, storage, sizeof(storage));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_NOT_FOUND,
                      flash_kv_get(&context, "mode", value, sizeof(value),
                                   &value_length));
    TEST_ASSERT_EQUAL(0U, value_length);
}

void test_flash_kv_sets_and_gets_value(void)
{
    flash_kv_t context;
    uint8_t storage[128];
    const uint8_t expected[] = {0x10U, 0x20U, 0x30U};
    uint8_t actual[sizeof(expected)] = {0};
    size_t actual_length = 0U;

    flash_kv_prepare(&context, storage, sizeof(storage));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "mode", expected,
                                   sizeof(expected)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_get(&context, "mode", actual, sizeof(actual),
                                   &actual_length));
    TEST_ASSERT_EQUAL(sizeof(expected), actual_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}

void test_flash_kv_latest_value_replaces_previous_value(void)
{
    flash_kv_t context;
    uint8_t storage[128];
    const uint8_t first[] = {1U};
    const uint8_t second[] = {2U, 3U};
    uint8_t actual[sizeof(second)] = {0};
    size_t actual_length = 0U;

    flash_kv_prepare(&context, storage, sizeof(storage));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "mode", first, sizeof(first)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "mode", second, sizeof(second)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_get(&context, "mode", actual, sizeof(actual),
                                   &actual_length));
    TEST_ASSERT_EQUAL(sizeof(second), actual_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, actual, sizeof(second));
}

void test_flash_kv_keeps_different_keys_independent(void)
{
    flash_kv_t context;
    uint8_t storage[128];
    const uint8_t mode[] = {1U};
    const uint8_t baudrate[] = {2U, 3U};
    uint8_t actual[sizeof(baudrate)] = {0};
    size_t actual_length = 0U;

    flash_kv_prepare(&context, storage, sizeof(storage));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "mode", mode, sizeof(mode)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "baud", baudrate,
                                   sizeof(baudrate)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_get(&context, "baud", actual, sizeof(actual),
                                   &actual_length));
    TEST_ASSERT_EQUAL(sizeof(baudrate), actual_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(baudrate, actual, sizeof(baudrate));
}

void test_flash_kv_rejects_invalid_arguments(void)
{
    flash_kv_t context;
    uint8_t storage[128];
    uint8_t value = 1U;
    size_t value_length = 0U;

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_init(NULL, storage, sizeof(storage)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_init(&context, NULL, sizeof(storage)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_init(&context, storage, 0U));

    flash_kv_prepare(&context, storage, sizeof(storage));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_set(NULL, "x", &value, 1U));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_set(&context, NULL, &value, 1U));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_set(&context, "x", NULL, 1U));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_get(NULL, "x", &value, 1U, &value_length));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_get(&context, NULL, &value, 1U, &value_length));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_get(&context, "x", &value, 1U, NULL));
}

void test_flash_kv_rejects_oversized_key_and_value(void)
{
    flash_kv_t context;
    uint8_t storage[256];
    uint8_t value[FLASH_KV_MAX_VALUE_LENGTH + 1U] = {0};
    char key[FLASH_KV_MAX_KEY_LENGTH + 2U];

    memset(key, 'k', sizeof(key));
    key[sizeof(key) - 1U] = '\0';
    flash_kv_prepare(&context, storage, sizeof(storage));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_set(&context, key, value, 1U));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_INVALID_ARGUMENT,
                      flash_kv_set(&context, "value", value,
                                   sizeof(value)));
}

void test_flash_kv_reports_small_output_buffer(void)
{
    flash_kv_t context;
    uint8_t storage[128];
    const uint8_t expected[] = {1U, 2U, 3U, 4U};
    uint8_t actual[2] = {0};
    size_t actual_length = 0U;

    flash_kv_prepare(&context, storage, sizeof(storage));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "data", expected,
                                   sizeof(expected)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OUTPUT_TOO_SMALL,
                      flash_kv_get(&context, "data", actual, sizeof(actual),
                                   &actual_length));
    TEST_ASSERT_EQUAL(sizeof(expected), actual_length);
}

void test_flash_kv_reports_no_space(void)
{
    flash_kv_t context;
    uint8_t storage[16];
    const uint8_t value[] = {1U, 2U, 3U, 4U};

    flash_kv_prepare(&context, storage, sizeof(storage));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_NO_SPACE,
                      flash_kv_set(&context, "long_key", value,
                                   sizeof(value)));
}

void test_flash_kv_recovers_records_after_reinitialization(void)
{
    flash_kv_t context;
    flash_kv_t reopened_context;
    uint8_t storage[128];
    const uint8_t expected[] = {9U, 8U, 7U};
    uint8_t actual[sizeof(expected)] = {0};
    size_t actual_length = 0U;

    flash_kv_prepare(&context, storage, sizeof(storage));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_set(&context, "mode", expected,
                                   sizeof(expected)));

    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_init(&reopened_context,
                                    storage,
                                    sizeof(storage)));
    TEST_ASSERT_EQUAL(FLASH_KV_STATUS_OK,
                      flash_kv_get(&reopened_context, "mode", actual,
                                   sizeof(actual), &actual_length));
    TEST_ASSERT_EQUAL(sizeof(expected), actual_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}
