#include "storage_persistence.h"

#include <stddef.h>
#include <string.h>

#include "app_config.h"
#include "board_spi_flash.h"
#include "common_crc.h"
#include "flash_kv.h"

#define STORAGE_PERSISTENCE_PAGE_SIZE       0x0100U
#define STORAGE_PERSISTENCE_RECORD_MAGIC    FLASH_KV_RECORD_MAGIC
#define STORAGE_PERSISTENCE_RECORD_HEADER   FLASH_KV_RECORD_HEADER_SIZE
#define STORAGE_PERSISTENCE_RECORD_COMMIT   FLASH_KV_RECORD_COMMIT_OFFSET
#define STORAGE_PERSISTENCE_RECORD_MARKER   FLASH_KV_RECORD_COMMIT_MARKER

static uint8_t s_sector_a[STORAGE_PERSISTENCE_SECTOR_SIZE];
static uint8_t s_sector_b[STORAGE_PERSISTENCE_SECTOR_SIZE];
static uint8_t s_candidate_sector[STORAGE_PERSISTENCE_SECTOR_SIZE];
static uint8_t s_program_sector[STORAGE_PERSISTENCE_SECTOR_SIZE];
static uint8_t s_verify_sector[STORAGE_PERSISTENCE_SECTOR_SIZE];
static uint8_t s_config_value[STORAGE_PERSISTENCE_VALUE_MAX];
static uint8_t s_canonical_config[APP_CONFIG_SERIALIZED_SIZE];
static uint8_t s_verify_value[STORAGE_PERSISTENCE_VALUE_MAX];
static uint8_t s_boot_count_value[sizeof(uint32_t)];
static app_config_t s_config_decode;
static app_config_t s_config_verify;
static uint16_t s_record_commit_offsets[FLASH_KV_MAX_RECORDS];
static flash_kv_t s_flash_kv;
static flash_kv_t s_reopened_flash_kv;
static uint8_t s_storage_persistence_initialized;

/* 提交前按 key 去重所需的静态工作区：避免上电/保存次数累积记录。 */
static flash_kv_latest_record_t s_latest_records[FLASH_KV_MAX_RECORDS];
static uint8_t s_record_crc_buffer[FLASH_KV_MAX_KEY_LENGTH +
                                    FLASH_KV_MAX_VALUE_LENGTH];

static uint16_t storage_persistence_u16_load_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U);
}

static void storage_persistence_u16_store_le(uint8_t *buffer,
                                             uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void storage_persistence_u32_store_le(uint8_t *buffer,
                                             uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFUL);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint32_t storage_persistence_u32_load_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static uint32_t storage_persistence_sector_address(uint8_t sector)
{
    return (sector == 0U) ?
           STORAGE_PERSISTENCE_SECTOR_A_ADDRESS :
           STORAGE_PERSISTENCE_SECTOR_B_ADDRESS;
}

static int storage_persistence_sector_header_valid(const uint8_t *sector)
{
    return (storage_persistence_u32_load_le(&sector[0]) ==
            FLASH_KV_SECTOR_MAGIC) &&
           (storage_persistence_u32_load_le(&sector[8]) ==
            FLASH_KV_SECTOR_COMMIT);
}

static int storage_persistence_records_scan(const uint8_t *sector,
                                            size_t used_length,
                                            uint16_t offsets[],
                                            uint8_t *count)
{
    size_t offset;
    uint8_t record_count;

    if ((sector == NULL) ||
        (offsets == NULL) ||
        (count == NULL) ||
        (used_length < FLASH_KV_SECTOR_HEADER_SIZE) ||
        (used_length > STORAGE_PERSISTENCE_SECTOR_SIZE))
    {
        return 0;
    }

    offset = FLASH_KV_SECTOR_HEADER_SIZE;
    record_count = 0U;

    while (offset < used_length)
    {
        uint16_t key_length;
        uint16_t value_length;
        size_t record_size;

        if (record_count >= FLASH_KV_MAX_RECORDS)
        {
            return 0;
        }

        if ((used_length - offset) < STORAGE_PERSISTENCE_RECORD_HEADER)
        {
            return 0;
        }

        if (storage_persistence_u16_load_le(&sector[offset]) !=
            STORAGE_PERSISTENCE_RECORD_MAGIC)
        {
            return 0;
        }

        key_length = sector[offset + 2U];
        value_length = storage_persistence_u16_load_le(&sector[offset + 3U]);
        record_size = STORAGE_PERSISTENCE_RECORD_HEADER +
                      (size_t)key_length +
                      (size_t)value_length;

        if ((key_length == 0U) ||
            (key_length > FLASH_KV_MAX_KEY_LENGTH) ||
            (value_length > FLASH_KV_MAX_VALUE_LENGTH) ||
            (record_size > (used_length - offset)) ||
            (sector[offset + STORAGE_PERSISTENCE_RECORD_COMMIT] !=
             STORAGE_PERSISTENCE_RECORD_MARKER))
        {
            return 0;
        }

        offsets[record_count] =
            (uint16_t)(offset + STORAGE_PERSISTENCE_RECORD_COMMIT);
        record_count++;
        offset += record_size;
    }

    *count = record_count;
    return (offset == used_length) ? 1 : 0;
}

static int storage_persistence_write_full_sector(uint8_t sector,
                                                 const uint8_t *image,
                                                 size_t used_length)
{
    uint32_t address;
    uint16_t page_offset;
    uint8_t record_count;
    uint8_t record_index;
    uint8_t marker;

    if ((image == NULL) ||
        (storage_persistence_records_scan(image,
                                          used_length,
                                          s_record_commit_offsets,
                                          &record_count) == 0))
    {
        return 0;
    }

    (void)memset(s_program_sector, 0xFF,
                 sizeof(s_program_sector));
    (void)memcpy(s_program_sector, image, sizeof(s_program_sector));

    /* 数据区先写，记录和扇区提交标志最后写入。 */
    for (record_index = 0U; record_index < record_count; record_index++)
    {
        s_program_sector[s_record_commit_offsets[record_index]] = 0xFFU;
    }

    s_program_sector[8] = 0xFFU;
    s_program_sector[9] = 0xFFU;
    s_program_sector[10] = 0xFFU;
    s_program_sector[11] = 0xFFU;

    address = storage_persistence_sector_address(sector);

    if (board_spi_flash_sector_erase(address) == 0)
    {
        return 0;
    }

    for (page_offset = 0U;
         page_offset < STORAGE_PERSISTENCE_SECTOR_SIZE;
         page_offset = (uint16_t)(page_offset +
                                  STORAGE_PERSISTENCE_PAGE_SIZE))
    {
        if (board_spi_flash_page_program(
                address + page_offset,
                &s_program_sector[page_offset],
                STORAGE_PERSISTENCE_PAGE_SIZE) == 0)
        {
            return 0;
        }
    }

    marker = STORAGE_PERSISTENCE_RECORD_MARKER;

    for (record_index = 0U; record_index < record_count; record_index++)
    {
        if (board_spi_flash_page_program(
                address + s_record_commit_offsets[record_index],
                &marker,
                1U) == 0)
        {
            return 0;
        }
    }

    if (board_spi_flash_page_program(address + 8U,
                                     &image[8],
                                     4U) == 0)
    {
        return 0;
    }

    if (board_spi_flash_read(address,
                             s_verify_sector,
                             STORAGE_PERSISTENCE_SECTOR_SIZE) == 0)
    {
        return 0;
    }

    return (memcmp(s_verify_sector,
                   image,
                   STORAGE_PERSISTENCE_SECTOR_SIZE) == 0) ? 1 : 0;
}

static storage_persistence_status_t storage_persistence_reload(void)
{
    if ((board_spi_flash_read(STORAGE_PERSISTENCE_SECTOR_A_ADDRESS,
                              s_sector_a,
                              STORAGE_PERSISTENCE_SECTOR_SIZE) == 0) ||
        (board_spi_flash_read(STORAGE_PERSISTENCE_SECTOR_B_ADDRESS,
                              s_sector_b,
                              STORAGE_PERSISTENCE_SECTOR_SIZE) == 0))
    {
        s_storage_persistence_initialized = 0U;
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    if (flash_kv_init_dual(&s_flash_kv,
                           s_sector_a,
                           s_sector_b,
                           STORAGE_PERSISTENCE_SECTOR_SIZE) !=
        FLASH_KV_STATUS_OK)
    {
        s_storage_persistence_initialized = 0U;
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    s_storage_persistence_initialized = 1U;
    return STORAGE_PERSISTENCE_STATUS_OK;
}

/* 扫描当前扇区，按 key 只保留 CRC 校验通过的最新一条记录。
 * 语义与 FlashKV 自身的压缩一致：遇到格式损坏立即停止，
 * CRC 错误的单条记录跳过，不阻塞后续有效记录。 */
static int storage_persistence_collect_latest(size_t source_length,
                                              size_t *record_count)
{
    size_t offset;
    size_t count = 0U;

    if ((s_flash_kv.storage == NULL) || (record_count == NULL))
    {
        return 0;
    }

    offset = FLASH_KV_SECTOR_HEADER_SIZE;

    while ((offset + STORAGE_PERSISTENCE_RECORD_HEADER) <= source_length)
    {
        const uint8_t *record = &s_flash_kv.storage[offset];
        size_t key_length = record[2];
        size_t value_length =
            storage_persistence_u16_load_le(&record[3]);
        size_t record_size = STORAGE_PERSISTENCE_RECORD_HEADER +
                             key_length + value_length;
        size_t index;
        uint32_t stored_crc;
        uint32_t calculated_crc;

        if ((storage_persistence_u16_load_le(&record[0]) !=
             STORAGE_PERSISTENCE_RECORD_MAGIC) ||
            (key_length == 0U) ||
            (key_length > FLASH_KV_MAX_KEY_LENGTH) ||
            (value_length > FLASH_KV_MAX_VALUE_LENGTH) ||
            (record_size > (source_length - offset)) ||
            (record[STORAGE_PERSISTENCE_RECORD_COMMIT] !=
             STORAGE_PERSISTENCE_RECORD_MARKER))
        {
            break;
        }

        (void)memcpy(s_record_crc_buffer,
                     &record[STORAGE_PERSISTENCE_RECORD_HEADER],
                     key_length);

        if (value_length > 0U)
        {
            (void)memcpy(&s_record_crc_buffer[key_length],
                         &record[STORAGE_PERSISTENCE_RECORD_HEADER +
                                 key_length],
                         value_length);
        }

        stored_crc = storage_persistence_u32_load_le(&record[5]);
        calculated_crc = common_crc32_calc(
            s_record_crc_buffer,
            (uint32_t)(key_length + value_length));

        if (stored_crc == calculated_crc)
        {
            for (index = 0U; index < count; index++)
            {
                if ((s_latest_records[index].key_length == key_length) &&
                    (memcmp(s_latest_records[index].key,
                            &record[STORAGE_PERSISTENCE_RECORD_HEADER],
                            key_length) == 0))
                {
                    break;
                }
            }

            if (index == count)
            {
                if (count >= FLASH_KV_MAX_RECORDS)
                {
                    return 0;
                }

                count++;
            }

            s_latest_records[index].valid = 1U;
            s_latest_records[index].key_length = key_length;
            s_latest_records[index].value_length = value_length;
            (void)memcpy(s_latest_records[index].key,
                         &record[STORAGE_PERSISTENCE_RECORD_HEADER],
                         key_length);
            s_latest_records[index].key[key_length] = '\0';

            if (value_length > 0U)
            {
                (void)memcpy(s_latest_records[index].value,
                             &record[STORAGE_PERSISTENCE_RECORD_HEADER +
                                     key_length],
                             value_length);
            }
        }

        offset += record_size;
    }

    *record_count = count;
    return 1;
}

/* 组装目标扇区镜像：写入新 generation，并把去重后的记录逐条重写，
 * 使提交后的记录数恒定，不随上电/保存次数增长。 */
static int storage_persistence_build_candidate(uint32_t generation,
                                               size_t *used_length)
{
    size_t source_length;
    size_t record_count = 0U;
    uint8_t scanned_count = 0U;
    size_t offset;
    size_t index;

    if ((s_flash_kv.storage == NULL) ||
        (s_flash_kv.write_offset < FLASH_KV_SECTOR_HEADER_SIZE) ||
        (s_flash_kv.write_offset > STORAGE_PERSISTENCE_SECTOR_SIZE))
    {
        return 0;
    }

    source_length = s_flash_kv.write_offset;

    (void)memset(s_latest_records, 0, sizeof(s_latest_records));

    if (storage_persistence_collect_latest(source_length,
                                           &record_count) == 0)
    {
        return 0;
    }

    (void)memset(s_candidate_sector, 0xFF,
                 sizeof(s_candidate_sector));
    storage_persistence_u32_store_le(&s_candidate_sector[0],
                                     FLASH_KV_SECTOR_MAGIC);
    storage_persistence_u32_store_le(&s_candidate_sector[4], generation);
    storage_persistence_u32_store_le(&s_candidate_sector[8],
                                     FLASH_KV_SECTOR_COMMIT);

    offset = FLASH_KV_SECTOR_HEADER_SIZE;

    for (index = 0U; index < record_count; index++)
    {
        uint8_t *record;
        size_t key_length;
        size_t value_length;
        size_t record_size;
        uint32_t crc;

        if (s_latest_records[index].valid == 0U)
        {
            continue;
        }

        key_length = s_latest_records[index].key_length;
        value_length = s_latest_records[index].value_length;
        record_size = STORAGE_PERSISTENCE_RECORD_HEADER +
                      key_length + value_length;

        if (record_size > (STORAGE_PERSISTENCE_SECTOR_SIZE - offset))
        {
            return 0;
        }

        (void)memcpy(s_record_crc_buffer,
                     s_latest_records[index].key,
                     key_length);

        if (value_length > 0U)
        {
            (void)memcpy(&s_record_crc_buffer[key_length],
                         s_latest_records[index].value,
                         value_length);
        }

        crc = common_crc32_calc(s_record_crc_buffer,
                                (uint32_t)(key_length + value_length));

        record = &s_candidate_sector[offset];
        storage_persistence_u16_store_le(&record[0],
                                         STORAGE_PERSISTENCE_RECORD_MAGIC);
        record[2] = (uint8_t)key_length;
        storage_persistence_u16_store_le(&record[3],
                                         (uint16_t)value_length);
        storage_persistence_u32_store_le(&record[5], crc);
        record[STORAGE_PERSISTENCE_RECORD_COMMIT] =
            STORAGE_PERSISTENCE_RECORD_MARKER;
        (void)memcpy(&record[STORAGE_PERSISTENCE_RECORD_HEADER],
                     s_latest_records[index].key,
                     key_length);

        if (value_length > 0U)
        {
            (void)memcpy(&record[STORAGE_PERSISTENCE_RECORD_HEADER +
                                 key_length],
                         s_latest_records[index].value,
                         value_length);
        }

        offset += record_size;
    }

    if (storage_persistence_records_scan(s_candidate_sector,
                                         offset,
                                         s_record_commit_offsets,
                                         &scanned_count) == 0)
    {
        return 0;
    }

    *used_length = offset;
    return 1;
}

static int storage_persistence_commit_current_context(
    const char *expected_key,
    const uint8_t *expected,
    uint16_t expected_length)
{
    uint8_t target_sector;
    size_t used_length;
    size_t verify_length;

    if ((expected_key == NULL) || (expected == NULL) ||
        (expected_length == 0U))
    {
        return 0;
    }

    target_sector = (s_flash_kv.active_sector == 0U) ? 1U : 0U;

    if (storage_persistence_build_candidate(
            s_flash_kv.generation + 1U,
            &used_length) == 0)
    {
        return 0;
    }

    if (storage_persistence_write_full_sector(target_sector,
                                              s_candidate_sector,
                                              used_length) == 0)
    {
        return 0;
    }

    if (board_spi_flash_read(
            storage_persistence_sector_address(target_sector),
            (target_sector == 0U) ? s_sector_a : s_sector_b,
            STORAGE_PERSISTENCE_SECTOR_SIZE) == 0)
    {
        return 0;
    }

    if (flash_kv_init_dual(&s_reopened_flash_kv,
                           s_sector_a,
                           s_sector_b,
                           STORAGE_PERSISTENCE_SECTOR_SIZE) !=
        FLASH_KV_STATUS_OK)
    {
        return 0;
    }

    verify_length = 0U;

    if (flash_kv_get(&s_reopened_flash_kv,
                     expected_key,
                     s_verify_value,
                     sizeof(s_verify_value),
                     &verify_length) != FLASH_KV_STATUS_OK)
    {
        return 0;
    }

    if ((verify_length != expected_length) ||
        (memcmp(s_verify_value, expected, expected_length) != 0) ||
        ((strcmp(expected_key, STORAGE_PERSISTENCE_CONFIG_KEY) == 0) &&
         (app_config_decode(s_verify_value,
                            (uint16_t)verify_length,
                            &s_config_verify) == 0)))
    {
        return 0;
    }

    s_flash_kv = s_reopened_flash_kv;
    return 1;
}

storage_persistence_status_t storage_persistence_init(void)
{
    int sector_a_valid;
    int sector_b_valid;
    size_t used_length;

    s_storage_persistence_initialized = 0U;

    if ((board_spi_flash_read(STORAGE_PERSISTENCE_SECTOR_A_ADDRESS,
                              s_sector_a,
                              STORAGE_PERSISTENCE_SECTOR_SIZE) == 0) ||
        (board_spi_flash_read(STORAGE_PERSISTENCE_SECTOR_B_ADDRESS,
                              s_sector_b,
                              STORAGE_PERSISTENCE_SECTOR_SIZE) == 0))
    {
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    sector_a_valid = storage_persistence_sector_header_valid(s_sector_a);
    sector_b_valid = storage_persistence_sector_header_valid(s_sector_b);

    if (flash_kv_init_dual(&s_flash_kv,
                           s_sector_a,
                           s_sector_b,
                           STORAGE_PERSISTENCE_SECTOR_SIZE) !=
        FLASH_KV_STATUS_OK)
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    if ((sector_a_valid == 0) && (sector_b_valid == 0))
    {
        if (storage_persistence_build_candidate(s_flash_kv.generation,
                                                 &used_length) == 0)
        {
            return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
        }

        if (storage_persistence_write_full_sector(0U,
                                                  s_candidate_sector,
                                                  used_length) == 0)
        {
            return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
        }

        if (storage_persistence_reload() != STORAGE_PERSISTENCE_STATUS_OK)
        {
            return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
        }
    }
    else
    {
        s_storage_persistence_initialized = 1U;
    }

    return STORAGE_PERSISTENCE_STATUS_OK;
}

storage_persistence_status_t storage_persistence_config_read(
    uint8_t *payload,
    uint16_t capacity,
    uint16_t *length)
{
    size_t value_length;
    flash_kv_status_t kv_status;

    if ((payload == NULL) || (length == NULL))
    {
        return STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT;
    }

    *length = 0U;

    if (s_storage_persistence_initialized == 0U)
    {
        return STORAGE_PERSISTENCE_STATUS_NOT_READY;
    }

    if (capacity < APP_CONFIG_SERIALIZED_SIZE)
    {
        return STORAGE_PERSISTENCE_STATUS_OUTPUT_TOO_SMALL;
    }

    value_length = 0U;
    kv_status = flash_kv_get(&s_flash_kv,
                             STORAGE_PERSISTENCE_CONFIG_KEY,
                             s_config_value,
                             sizeof(s_config_value),
                             &value_length);

    if (kv_status == FLASH_KV_STATUS_NOT_FOUND)
    {
        return STORAGE_PERSISTENCE_STATUS_NOT_FOUND;
    }

    if (kv_status == FLASH_KV_STATUS_CRC_ERROR)
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    if (kv_status != FLASH_KV_STATUS_OK)
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    if ((value_length > 0xFFFFU) ||
        (app_config_decode(s_config_value,
                           (uint16_t)value_length,
                           &s_config_decode) == 0))
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    if (app_config_encode(&s_config_decode,
                          payload,
                          capacity,
                          length) == 0)
    {
        *length = 0U;
        return STORAGE_PERSISTENCE_STATUS_OUTPUT_TOO_SMALL;
    }

    return STORAGE_PERSISTENCE_STATUS_OK;
}

storage_persistence_status_t storage_persistence_config_save(
    const uint8_t *payload,
    uint16_t length)
{
    uint16_t canonical_length;
    flash_kv_status_t kv_status;

    if ((payload == NULL) ||
        (length == 0U) ||
        (length > STORAGE_PERSISTENCE_VALUE_MAX))
    {
        return STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT;
    }

    if (s_storage_persistence_initialized == 0U)
    {
        return STORAGE_PERSISTENCE_STATUS_NOT_READY;
    }

    if (app_config_decode(payload, length, &s_config_decode) == 0)
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    canonical_length = 0U;

    if (app_config_encode(&s_config_decode,
                          s_canonical_config,
                          sizeof(s_canonical_config),
                          &canonical_length) == 0)
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }

    kv_status = flash_kv_set(&s_flash_kv,
                             STORAGE_PERSISTENCE_CONFIG_KEY,
                             s_canonical_config,
                             canonical_length);

    if (kv_status != FLASH_KV_STATUS_OK)
    {
        (void)storage_persistence_reload();
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    if (storage_persistence_commit_current_context(
                                                   STORAGE_PERSISTENCE_CONFIG_KEY,
                                                   s_canonical_config,
                                                   canonical_length) == 0)
    {
        (void)storage_persistence_reload();
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    return STORAGE_PERSISTENCE_STATUS_OK;
}

/* 读取、递增并原子保存 FlashKV 中的 boot_count；成功后才写出 count。 */
storage_persistence_status_t storage_persistence_boot_count_next(
    uint32_t *count)
{
    size_t value_length = 0U;
    flash_kv_status_t kv_status;
    uint32_t current;

    if (count == NULL)
    {
        return STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT;
    }
    if (s_storage_persistence_initialized == 0U)
    {
        return STORAGE_PERSISTENCE_STATUS_NOT_READY;
    }

    kv_status = flash_kv_get(&s_flash_kv,
                             STORAGE_PERSISTENCE_BOOT_COUNT_KEY,
                             s_boot_count_value,
                             sizeof(s_boot_count_value),
                             &value_length);
    if (kv_status == FLASH_KV_STATUS_NOT_FOUND)
    {
        current = 0U;
    }
    else if ((kv_status != FLASH_KV_STATUS_OK) ||
             (value_length != sizeof(s_boot_count_value)))
    {
        return STORAGE_PERSISTENCE_STATUS_DATA_ERROR;
    }
    else
    {
        current = storage_persistence_u32_load_le(s_boot_count_value);
    }

    current = (current == 0xFFFFFFFFUL) ? 1U : (current + 1U);
    storage_persistence_u32_store_le(s_boot_count_value, current);

    kv_status = flash_kv_set(&s_flash_kv,
                             STORAGE_PERSISTENCE_BOOT_COUNT_KEY,
                             s_boot_count_value,
                             sizeof(s_boot_count_value));
    if (kv_status != FLASH_KV_STATUS_OK)
    {
        (void)storage_persistence_reload();
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    if (storage_persistence_commit_current_context(
            STORAGE_PERSISTENCE_BOOT_COUNT_KEY,
            s_boot_count_value,
            sizeof(s_boot_count_value)) == 0)
    {
        (void)storage_persistence_reload();
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    *count = current;
    return STORAGE_PERSISTENCE_STATUS_OK;
}

storage_persistence_status_t storage_persistence_flash_diag(uint8_t id[3])
{
    if (id == NULL)
    {
        return STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT;
    }

    if (board_spi_flash_read_jedec_id(id) == 0)
    {
        return STORAGE_PERSISTENCE_STATUS_FLASH_ERROR;
    }

    return STORAGE_PERSISTENCE_STATUS_OK;
}

static uint8_t storage_persistence_status_to_task_status(
    storage_persistence_status_t status)
{
    switch (status)
    {
    case STORAGE_PERSISTENCE_STATUS_OK:
        return STORAGE_TASK_PERSIST_STATUS_OK;

    case STORAGE_PERSISTENCE_STATUS_NOT_READY:
        return STORAGE_TASK_PERSIST_STATUS_NOT_READY;

    case STORAGE_PERSISTENCE_STATUS_NOT_FOUND:
        return STORAGE_TASK_PERSIST_STATUS_NOT_FOUND;

    case STORAGE_PERSISTENCE_STATUS_FLASH_ERROR:
        return STORAGE_TASK_PERSIST_STATUS_FLASH_ERROR;

    case STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT:
        return STORAGE_TASK_PERSIST_STATUS_INVALID_ARGUMENT;

    case STORAGE_PERSISTENCE_STATUS_DATA_ERROR:
    case STORAGE_PERSISTENCE_STATUS_OUTPUT_TOO_SMALL:
    default:
        return STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;
    }
}

int storage_persistence_request_handle(
    const storage_task_persist_request_t *request,
    storage_task_persist_result_t *result)
{
    storage_persistence_status_t status;

    if ((request == NULL) || (result == NULL))
    {
        return 0;
    }

    (void)memset(result, 0, sizeof(*result));
    result->request_id = request->request_id;
    result->operation = request->operation;
    result->status = STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;

    switch (request->operation)
    {
    case STORAGE_TASK_PERSIST_CONFIG_LOAD:
    case STORAGE_TASK_PERSIST_CONFIG_READ:
        status = storage_persistence_config_read(
            result->payload,
            sizeof(result->payload),
            &result->payload_length);
        break;

    case STORAGE_TASK_PERSIST_CONFIG_SAVE:
        status = storage_persistence_config_save(
            request->payload,
            request->payload_length);
        break;

    case STORAGE_TASK_PERSIST_FLASH_DIAG:
        status = storage_persistence_flash_diag(result->payload);
        if (status == STORAGE_PERSISTENCE_STATUS_OK)
        {
            result->payload_length = 3U;
        }
        break;

    default:
        status = STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT;
        break;
    }

    result->status = storage_persistence_status_to_task_status(status);
    return 1;
}
