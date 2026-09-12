#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "diskio.h"
#include "board_sdio.h"

static uint32_t s_legacy_read_calls;
static uint32_t s_legacy_write_calls;
static uint32_t s_dma_read_calls;
static uint32_t s_dma_write_calls;
static uint32_t s_wait_ready_calls;
static uint32_t s_last_read_sector;
static uint32_t s_last_write_sector;
static uint8_t s_read_buffer_was_unaligned;
static uint8_t s_write_buffer_was_unaligned;
static uint8_t s_write_data[BOARD_SDIO_BLOCK_SIZE];

static void reset_stub_state(void)
{
    s_legacy_read_calls = 0U;
    s_legacy_write_calls = 0U;
    s_dma_read_calls = 0U;
    s_dma_write_calls = 0U;
    s_wait_ready_calls = 0U;
    s_last_read_sector = 0U;
    s_last_write_sector = 0U;
    s_read_buffer_was_unaligned = 0U;
    s_write_buffer_was_unaligned = 0U;
    (void)memset(s_write_data, 0, sizeof(s_write_data));
}

static void expect_read_pattern(const uint8_t *buffer, uint32_t sector)
{
    uint32_t index;

    for (index = 0U; index < BOARD_SDIO_BLOCK_SIZE; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(
            (uint8_t)((sector + index) & 0xFFU),
            buffer[index]);
    }
}

void setUp(void)
{
    reset_stub_state();
    diskio_sdio_set_ready(1U);
}

void tearDown(void)
{
}

uint8_t board_sdio_card_present(void)
{
    return 1U;
}

board_sdio_status_t board_sdio_read_block(
    uint32_t block_number,
    uint8_t *buffer)
{
    uint32_t index;

    s_legacy_read_calls++;
    s_last_read_sector = block_number;
    if ((((uintptr_t)buffer) & 0x03U) != 0U)
    {
        s_read_buffer_was_unaligned = 1U;
    }

    for (index = 0U; index < BOARD_SDIO_BLOCK_SIZE; index++)
    {
        buffer[index] = (uint8_t)((block_number + index) & 0xFFU);
    }

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_write_block(
    uint32_t block_number,
    const uint8_t *buffer)
{
    s_last_write_sector = block_number;
    s_legacy_write_calls++;
    if ((((uintptr_t)buffer) & 0x03U) != 0U)
    {
        s_write_buffer_was_unaligned = 1U;
    }
    (void)memcpy(s_write_data, buffer, BOARD_SDIO_BLOCK_SIZE);
    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_read_block_dma_polling(
    uint32_t block_number,
    uint8_t *buffer)
{
    uint32_t index;

    s_dma_read_calls++;
    s_last_read_sector = block_number;
    if ((((uintptr_t)buffer) & 0x03U) != 0U)
    {
        s_read_buffer_was_unaligned = 1U;
    }

    for (index = 0U; index < BOARD_SDIO_BLOCK_SIZE; index++)
    {
        buffer[index] = (uint8_t)((block_number + index) & 0xFFU);
    }

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_write_block_dma_polling(
    uint32_t block_number,
    const uint8_t *buffer)
{
    s_dma_write_calls++;
    s_last_write_sector = block_number;
    if ((((uintptr_t)buffer) & 0x03U) != 0U)
    {
        s_write_buffer_was_unaligned = 1U;
    }
    (void)memcpy(s_write_data, buffer, BOARD_SDIO_BLOCK_SIZE);
    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_wait_card_ready(
    uint16_t rca,
    uint32_t *response)
{
    (void)rca;
    s_wait_ready_calls++;
    if (response != NULL)
    {
        *response = 0U;
    }
    return BOARD_SDIO_STATUS_OK;
}

static void test_disk_read_routes_to_polling(void)
{
    union
    {
        uint32_t alignment;
        uint8_t bytes[BOARD_SDIO_BLOCK_SIZE];
    } buffer;
    DRESULT result;

    result = disk_read(0U, buffer.bytes, 100U, 1U);

    TEST_ASSERT_EQUAL(RES_OK, result);
    TEST_ASSERT_EQUAL_UINT32(0U, s_dma_read_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, s_legacy_read_calls);
    TEST_ASSERT_EQUAL_UINT32(100U, s_last_read_sector);
    TEST_ASSERT_EQUAL_UINT8(0U, s_read_buffer_was_unaligned);
    expect_read_pattern(buffer.bytes, 100U);
}

static void test_disk_read_polling_accepts_unaligned_fatfs_buffer(void)
{
    uint8_t storage[BOARD_SDIO_BLOCK_SIZE + 1U];
    DRESULT result;

    result = disk_read(0U, &storage[1], 200U, 1U);

    TEST_ASSERT_EQUAL(RES_OK, result);
    TEST_ASSERT_EQUAL_UINT32(0U, s_dma_read_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, s_legacy_read_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, s_read_buffer_was_unaligned);
    expect_read_pattern(&storage[1], 200U);
}

static void test_disk_write_routes_to_polling(void)
{
    union
    {
        uint32_t alignment;
        uint8_t bytes[BOARD_SDIO_BLOCK_SIZE];
    } buffer;
    uint32_t index;
    DRESULT result;

    for (index = 0U; index < BOARD_SDIO_BLOCK_SIZE; index++)
    {
        buffer.bytes[index] = (uint8_t)(index ^ 0x5AU);
    }

    result = disk_write(0U, buffer.bytes, 300U, 1U);

    TEST_ASSERT_EQUAL(RES_OK, result);
    TEST_ASSERT_EQUAL_UINT32(0U, s_dma_write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, s_legacy_write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, s_wait_ready_calls);
    TEST_ASSERT_EQUAL_UINT32(300U, s_last_write_sector);
    TEST_ASSERT_EQUAL_UINT8(0U, s_write_buffer_was_unaligned);
    TEST_ASSERT_EQUAL_MEMORY(buffer.bytes, s_write_data, BOARD_SDIO_BLOCK_SIZE);
}

static void test_disk_write_polling_accepts_unaligned_fatfs_buffer(void)
{
    uint8_t storage[BOARD_SDIO_BLOCK_SIZE + 1U];
    uint32_t index;
    DRESULT result;

    for (index = 0U; index < BOARD_SDIO_BLOCK_SIZE; index++)
    {
        storage[index + 1U] = (uint8_t)(0xA5U ^ index);
    }

    result = disk_write(0U, &storage[1], 400U, 1U);

    TEST_ASSERT_EQUAL(RES_OK, result);
    TEST_ASSERT_EQUAL_UINT32(0U, s_dma_write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, s_legacy_write_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, s_write_buffer_was_unaligned);
    TEST_ASSERT_EQUAL_MEMORY(&storage[1], s_write_data, BOARD_SDIO_BLOCK_SIZE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_disk_read_routes_to_polling);
    RUN_TEST(test_disk_read_polling_accepts_unaligned_fatfs_buffer);
    RUN_TEST(test_disk_write_routes_to_polling);
    RUN_TEST(test_disk_write_polling_accepts_unaligned_fatfs_buffer);
    return UNITY_END();
}
