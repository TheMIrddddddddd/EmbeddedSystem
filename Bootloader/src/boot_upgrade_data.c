#include "boot_upgrade_data.h"

#include "common_crc.h"
#include "common_flash_layout.h"
#include "boot_upgrade_staging.h"

#define BOOT_UPGRADE_DATA_PREFIX_SIZE       8U
#define BOOT_UPGRADE_DATA_CRC_SIZE          2U
#define BOOT_UPGRADE_DATA_MIN_PAYLOAD_SIZE  (BOOT_UPGRADE_DATA_PREFIX_SIZE + BOOT_UPGRADE_DATA_CRC_SIZE)
#define BOOT_UPGRADE_DATA_MAX_CHUNK_SIZE    256U

static uint8_t s_last_ack_valid;
static uint16_t s_last_sequence;
static uint32_t s_last_offset;
static uint16_t s_last_chunk_length;
static uint32_t s_last_payload_fingerprint;

volatile boot_upgrade_data_status_t g_boot_upgrade_data_status;
volatile uint16_t g_boot_upgrade_data_sequence;
volatile uint32_t g_boot_upgrade_data_offset;
volatile uint16_t g_boot_upgrade_data_chunk_length;
volatile uint16_t g_boot_upgrade_data_received_crc16;
volatile uint16_t g_boot_upgrade_data_calculated_crc16;
volatile board_internal_flash_status_t g_boot_upgrade_data_flash_status;
volatile uint32_t g_boot_upgrade_data_write_fail_count;
volatile uint32_t g_boot_upgrade_data_duplicate_count;
volatile uint8_t g_boot_upgrade_data_write_failed;

static uint16_t boot_upgrade_data_read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t boot_upgrade_data_read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static void boot_upgrade_data_set_status(boot_upgrade_data_status_t status)
{
    g_boot_upgrade_data_status = status;
}

void boot_upgrade_data_session_reset(void)
{
    s_last_ack_valid = 0U;
    s_last_sequence = 0U;
    s_last_offset = 0U;
    s_last_chunk_length = 0U;
    s_last_payload_fingerprint = 0U;

    g_boot_upgrade_data_status = BOOT_UPGRADE_DATA_STATUS_OK;
    g_boot_upgrade_data_sequence = 0U;
    g_boot_upgrade_data_offset = 0U;
    g_boot_upgrade_data_chunk_length = 0U;
    g_boot_upgrade_data_received_crc16 = 0U;
    g_boot_upgrade_data_calculated_crc16 = 0U;
    g_boot_upgrade_data_flash_status = BOARD_INTERNAL_FLASH_STATUS_OK;
    g_boot_upgrade_data_write_fail_count = 0U;
    g_boot_upgrade_data_duplicate_count = 0U;
    g_boot_upgrade_data_write_failed = 0U;
}

boot_upgrade_data_status_t boot_upgrade_data_accept(
    const upgrade_meta_t *selected_meta,
    const uint8_t *payload,
    uint16_t payload_length,
    uint16_t *sequence_out)
{
    uint16_t sequence;
    uint32_t offset;
    uint16_t chunk_length;
    uint16_t received_crc16;
    uint16_t calculated_crc16;
    uint32_t payload_fingerprint;
    uint32_t cursor;
    uint32_t remaining;
    uint32_t current_offset;
    board_internal_flash_status_t flash_status;

    if (sequence_out == 0)
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT);
        return BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT;
    }

    *sequence_out = 0U;
    sequence = 0U;
    offset = 0U;
    chunk_length = 0U;
    received_crc16 = 0U;
    calculated_crc16 = 0U;
    g_boot_upgrade_data_flash_status = BOARD_INTERNAL_FLASH_STATUS_OK;

    if (payload == 0)
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT);
        return BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT;
    }

    if (payload_length >= 2U)
    {
        sequence = boot_upgrade_data_read_u16_be(&payload[0]);
    }
    if (payload_length >= 6U)
    {
        offset = boot_upgrade_data_read_u32_be(&payload[2]);
    }
    if (payload_length >= BOOT_UPGRADE_DATA_PREFIX_SIZE)
    {
        chunk_length = boot_upgrade_data_read_u16_be(&payload[6]);
    }

    g_boot_upgrade_data_sequence = sequence;
    g_boot_upgrade_data_offset = offset;
    g_boot_upgrade_data_chunk_length = chunk_length;
    *sequence_out = sequence;

    if (selected_meta == 0)
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT);
        return BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT;
    }

    if ((selected_meta->state != FW_STATE_RECEIVING) ||
        (selected_meta->upgrade_source != UPGRADE_SOURCE_ONLINE))
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_STATE_NOT_ALLOWED);
        return BOOT_UPGRADE_DATA_STATUS_STATE_NOT_ALLOWED;
    }

    /* 失败片可能已经部分编程，当前会话必须停止继续写入。 */
    if (g_boot_upgrade_data_write_failed != 0U)
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_FLASH_WRITE_FAILED);
        return BOOT_UPGRADE_DATA_STATUS_FLASH_WRITE_FAILED;
    }

    if ((payload_length < BOOT_UPGRADE_DATA_MIN_PAYLOAD_SIZE) ||
        (chunk_length == 0U) ||
        (chunk_length > BOOT_UPGRADE_DATA_MAX_CHUNK_SIZE) ||
        (payload_length != (uint16_t)(BOOT_UPGRADE_DATA_PREFIX_SIZE +
                                      chunk_length +
                                      BOOT_UPGRADE_DATA_CRC_SIZE)))
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_LENGTH_INVALID);
        return BOOT_UPGRADE_DATA_STATUS_LENGTH_INVALID;
    }

    received_crc16 = boot_upgrade_data_read_u16_be(
        &payload[BOOT_UPGRADE_DATA_PREFIX_SIZE + chunk_length]
    );
    calculated_crc16 = common_crc16_calc(
        &payload[BOOT_UPGRADE_DATA_PREFIX_SIZE],
        chunk_length
    );

    g_boot_upgrade_data_received_crc16 = received_crc16;
    g_boot_upgrade_data_calculated_crc16 = calculated_crc16;

    if (received_crc16 != calculated_crc16)
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_CHUNK_CRC_INVALID);
        return BOOT_UPGRADE_DATA_STATUS_CHUNK_CRC_INVALID;
    }

    payload_fingerprint = common_crc32_calc(payload, payload_length);

    /* 同一片完整重传：只重发 ACK，不重复写 Flash。 */
    if ((s_last_ack_valid != 0U) &&
        (sequence == s_last_sequence) &&
        (offset == s_last_offset) &&
        (chunk_length == s_last_chunk_length) &&
        (payload_fingerprint == s_last_payload_fingerprint))
    {
        g_boot_upgrade_data_duplicate_count++;
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_DUPLICATE);
        return BOOT_UPGRADE_DATA_STATUS_DUPLICATE;
    }

    if (sequence != g_boot_upgrade_staging_expected_sequence)
    {
        boot_upgrade_data_set_status(
            BOOT_UPGRADE_DATA_STATUS_SEQUENCE_NOT_CONTIGUOUS
        );
        return BOOT_UPGRADE_DATA_STATUS_SEQUENCE_NOT_CONTIGUOUS;
    }

    if ((offset != g_boot_upgrade_staging_received_length) ||
        (offset > selected_meta->pending_size) ||
        (chunk_length > (selected_meta->pending_size - offset)))
    {
        boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_OFFSET_INVALID);
        return BOOT_UPGRADE_DATA_STATUS_OFFSET_INVALID;
    }

    cursor = 0U;
    remaining = chunk_length;
    current_offset = offset;

    while (remaining > 0U)
    {
        uint32_t page_index;
        uint32_t page_offset;
        uint32_t page_remaining;
        uint32_t write_length;

        page_index = current_offset / INTERNAL_FLASH_PAGE_SIZE;
        page_offset = current_offset % INTERNAL_FLASH_PAGE_SIZE;
        page_remaining = INTERNAL_FLASH_PAGE_SIZE - page_offset;
        write_length = (remaining < page_remaining) ? remaining : page_remaining;

        flash_status = board_internal_flash_page_program(
            BOARD_INTERNAL_FLASH_REGION_STAGING,
            page_index,
            page_offset,
            &payload[BOOT_UPGRADE_DATA_PREFIX_SIZE + cursor],
            write_length
        );

        g_boot_upgrade_data_flash_status = flash_status;

        if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
        {
            /* 本片可能已经写入前半段，禁止在本次会话中继续写。 */
            g_boot_upgrade_data_write_failed = 1U;
            g_boot_upgrade_data_write_fail_count++;
            boot_upgrade_data_set_status(
                BOOT_UPGRADE_DATA_STATUS_FLASH_WRITE_FAILED
            );
            return BOOT_UPGRADE_DATA_STATUS_FLASH_WRITE_FAILED;
        }

        cursor += write_length;
        current_offset += write_length;
        remaining -= write_length;
    }

    /* 所有页段写入和读回校验成功后，才推进接收进度。 */
    g_boot_upgrade_staging_received_length = offset + chunk_length;
    g_boot_upgrade_staging_expected_sequence = (uint16_t)(sequence + 1U);

    s_last_ack_valid = 1U;
    s_last_sequence = sequence;
    s_last_offset = offset;
    s_last_chunk_length = chunk_length;
    s_last_payload_fingerprint = payload_fingerprint;

    boot_upgrade_data_set_status(BOOT_UPGRADE_DATA_STATUS_OK);
    return BOOT_UPGRADE_DATA_STATUS_OK;
}
