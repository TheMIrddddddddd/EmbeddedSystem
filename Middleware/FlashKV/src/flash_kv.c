#include <string.h>
#include "flash_kv.h"
#include "common_crc.h"

#define FLASH_KV_MAGIC         0xA55AU
#define FLASH_KV_COMMIT_MARKER 0x5AU
#define FLASH_KV_HEADER_SIZE   10U

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0XFFU);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFUL);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static int flash_kv_sector_is_valid(const uint8_t *sector)
{
    return (read_u32_le(&sector[0]) ==
            FLASH_KV_SECTOR_MAGIC) &&
           (read_u32_le(&sector[8]) ==
            FLASH_KV_SECTOR_COMMIT);
}

static void flash_kv_erase_sector(uint8_t *sector,
                                  size_t sector_size)
{
    memset(sector, 0xFF, sector_size);
}

static size_t flash_kv_data_start(const flash_kv_t *context)
{
    return (context->dual_mode != 0U) ?
           FLASH_KV_SECTOR_HEADER_SIZE : 0U;
}

static size_t flash_kv_capacity(const flash_kv_t *context)
{
    return (context->dual_mode != 0U) ?
           context->sector_size : context->storage_size;
}

static size_t flash_kv_find_write_offset(const uint8_t *storage,
                                         size_t storage_size,
                                         size_t start_offset)
{
    size_t offset = start_offset;

    while ((offset + FLASH_KV_HEADER_SIZE) <= storage_size)
    {
        const uint8_t *record = &storage[offset];
        size_t key_length;
        size_t value_length;
        size_t record_size;

        if (read_u16_le(&record[0]) != FLASH_KV_MAGIC)
        {
            break;
        }

        key_length = record[2];
        value_length = read_u16_le(&record[3]);
        record_size = FLASH_KV_HEADER_SIZE +
                      key_length + value_length;

        if ((key_length == 0U) ||
            (key_length > FLASH_KV_MAX_KEY_LENGTH) ||
            (value_length > FLASH_KV_MAX_VALUE_LENGTH) ||
            (record_size > (storage_size - offset)) ||
            (record[9] != FLASH_KV_COMMIT_MARKER))
        {
            break;
        }

        offset += record_size;
    }

    return offset;
}

static flash_kv_status_t flash_kv_append_record(uint8_t *storage,
                                                size_t storage_size,
                                                size_t *write_offset,
                                                const uint8_t *key,
                                                size_t key_length,
                                                const uint8_t *value,
                                                size_t value_length)
{
    uint8_t crc_buffer[FLASH_KV_MAX_KEY_LENGTH +
                       FLASH_KV_MAX_VALUE_LENGTH];
    size_t record_size;
    uint8_t *record;
    uint32_t crc;

    record_size = FLASH_KV_HEADER_SIZE + key_length + value_length;

    if ((*write_offset > storage_size) ||
        (record_size > (storage_size - *write_offset)) ||
        (key_length == 0U) ||
        (key_length > FLASH_KV_MAX_KEY_LENGTH) ||
        (value_length > FLASH_KV_MAX_VALUE_LENGTH))
    {
        return FLASH_KV_STATUS_NO_SPACE;
    }

    memcpy(crc_buffer, key, key_length);
    if (value_length > 0U)
    {
        memcpy(&crc_buffer[key_length], value, value_length);
    }

    crc = common_crc32_calc(crc_buffer,
                            (uint32_t)(key_length + value_length));
    record = &storage[*write_offset];

    write_u16_le(&record[0], FLASH_KV_MAGIC);
    record[2] = (uint8_t)key_length;
    write_u16_le(&record[3], (uint16_t)value_length);
    write_u32_le(&record[5], crc);
    record[9] = 0xFFU;
    memcpy(&record[FLASH_KV_HEADER_SIZE], key, key_length);

    if (value_length > 0U)
    {
        memcpy(&record[FLASH_KV_HEADER_SIZE + key_length],
               value,
               value_length);
    }

    record[9] = FLASH_KV_COMMIT_MARKER;
    *write_offset += record_size;

    return FLASH_KV_STATUS_OK;
}

static flash_kv_status_t flash_kv_collect_latest(
    const flash_kv_t *context,
    flash_kv_latest_record_t records[],
    size_t *record_count)
{
    size_t count = 0U;
    size_t offset = flash_kv_data_start(context);
    uint8_t crc_buffer[FLASH_KV_MAX_KEY_LENGTH +
                       FLASH_KV_MAX_VALUE_LENGTH];

    if (record_count == NULL)
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    while ((offset + FLASH_KV_HEADER_SIZE) <= context->write_offset)
    {
        const uint8_t *record = &context->storage[offset];
        size_t key_length = record[2];
        size_t value_length = read_u16_le(&record[3]);
        size_t record_size = FLASH_KV_HEADER_SIZE +
                             key_length + value_length;
        const uint8_t *stored_key;
        const uint8_t *stored_value;
        size_t i;
        uint32_t stored_crc;
        uint32_t calculated_crc;

        if ((read_u16_le(&record[0]) != FLASH_KV_MAGIC) ||
            (key_length == 0U) ||
            (key_length > FLASH_KV_MAX_KEY_LENGTH) ||
            (value_length > FLASH_KV_MAX_VALUE_LENGTH) ||
            (record_size > (context->storage_size - offset)) ||
            (record[9] != FLASH_KV_COMMIT_MARKER))
        {
            break;
        }

        stored_key = &record[FLASH_KV_HEADER_SIZE];
        stored_value = stored_key + key_length;
        memcpy(crc_buffer, stored_key, key_length);
        if (value_length > 0U)
        {
            memcpy(&crc_buffer[key_length], stored_value, value_length);
        }

        stored_crc = read_u32_le(&record[5]);
        calculated_crc = common_crc32_calc(
            crc_buffer, (uint32_t)(key_length + value_length));

        if (stored_crc == calculated_crc)
        {
            for (i = 0U; i < count; i++)
            {
                if ((records[i].key_length == key_length) &&
                    (memcmp(records[i].key, stored_key, key_length) == 0))
                {
                    break;
                }
            }

            if (i == count)
            {
                if (count >= FLASH_KV_MAX_RECORDS)
                {
                    return FLASH_KV_STATUS_NO_SPACE;
                }

                count++;
            }

            if (i < count)
            {
                records[i].valid = 1U;
                records[i].key_length = key_length;
                records[i].value_length = value_length;
                memcpy(records[i].key, stored_key, key_length);
                records[i].key[key_length] = '\0';
                memcpy(records[i].value, stored_value, value_length);
            }
        }

        offset += record_size;
    }

    *record_count = count;
    return FLASH_KV_STATUS_OK;
}

static flash_kv_status_t flash_kv_gc(flash_kv_t *context)
{
    flash_kv_latest_record_t records[FLASH_KV_MAX_RECORDS];
    size_t count;
    size_t old_sector = context->active_sector;
    size_t new_sector = (old_sector == 0U) ? 1U : 0U;
    size_t new_offset = FLASH_KV_SECTOR_HEADER_SIZE;
    size_t i;
    uint8_t *target = context->sectors[new_sector];

    memset(records, 0, sizeof(records));
    if (flash_kv_collect_latest(context, records, &count) !=
        FLASH_KV_STATUS_OK)
    {
        return FLASH_KV_STATUS_NO_SPACE;
    }
    flash_kv_erase_sector(target, context->sector_size);

    write_u32_le(&target[0], FLASH_KV_SECTOR_MAGIC);
    write_u32_le(&target[4], context->generation + 1U);

    for (i = 0U; i < count; i++)
    {
        if (records[i].valid == 0U)
        {
            continue;
        }

        if (flash_kv_append_record(target,
                                   context->sector_size,
                                   &new_offset,
                                   records[i].key,
                                   records[i].key_length,
                                   records[i].value,
                                   records[i].value_length) !=
            FLASH_KV_STATUS_OK)
        {
            return FLASH_KV_STATUS_NO_SPACE;
        }
    }

    /* 新扇区所有记录完成后，最后提交扇区。 */
    write_u32_le(&target[8], FLASH_KV_SECTOR_COMMIT);

    context->active_sector = new_sector;
    context->generation++;
    context->storage = target;
    context->storage_size = context->sector_size;
    context->write_offset = new_offset;

    return FLASH_KV_STATUS_OK;
}

flash_kv_status_t flash_kv_init(flash_kv_t *context,
                                uint8_t *storage,
                                size_t storage_size)
{
    if ((context == NULL) ||
        (storage == NULL) ||
        (storage_size == 0U))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    context->storage = storage;
    context->storage_size = storage_size;
    context->write_offset = 0U;
    context->sectors[0] = NULL;
    context->sectors[1] = NULL;
    context->sector_size = storage_size;
    context->active_sector = 0U;
    context->generation = 0U;
    context->dual_mode = 0U;

    while ((context->write_offset + FLASH_KV_HEADER_SIZE) <=
           context->storage_size)
    {
        const uint8_t *record = &context->storage[context->write_offset];
        size_t key_length;
        size_t value_length;
        size_t record_size;

        if (read_u16_le(&record[0]) != FLASH_KV_MAGIC)
        {
            break;
        }

        key_length = record[2];
        value_length = read_u16_le(&record[3]);

        if ((key_length == 0U) ||
            (key_length > FLASH_KV_MAX_KEY_LENGTH) ||
            (value_length > FLASH_KV_MAX_VALUE_LENGTH))
        {
            break;
        }

        record_size = FLASH_KV_HEADER_SIZE +
                      key_length +
                      value_length;

        if (record_size > (context->storage_size -
                           context->write_offset))
        {
            break;
        }

        if (record[9] != FLASH_KV_COMMIT_MARKER)
        {
            break;
        }

        context->write_offset += record_size;
    }

    return FLASH_KV_STATUS_OK;
}

flash_kv_status_t flash_kv_init_dual(flash_kv_t *context,
                                     uint8_t *sector_a,
                                     uint8_t *sector_b,
                                     size_t sector_size)
{
    int sector_a_valid;
    int sector_b_valid;

    if ((context == NULL) ||
        (sector_a == NULL) ||
        (sector_b == NULL) ||
        (sector_size <= FLASH_KV_SECTOR_HEADER_SIZE))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    context->storage = NULL;
    context->storage_size = 0U;
    context->sectors[0] = sector_a;
    context->sectors[1] = sector_b;
    context->sector_size = sector_size;
    context->dual_mode = 1U;

    sector_a_valid = flash_kv_sector_is_valid(sector_a);
    sector_b_valid = flash_kv_sector_is_valid(sector_b);

    if ((sector_a_valid == 0) && (sector_b_valid == 0))
    {
        flash_kv_erase_sector(sector_a, sector_size);
        flash_kv_erase_sector(sector_b, sector_size);

        write_u32_le(&sector_a[0], FLASH_KV_SECTOR_MAGIC);
        write_u32_le(&sector_a[4], 1U);

        /*
         * commit 必须最后写。
         */
        write_u32_le(&sector_a[8], FLASH_KV_SECTOR_COMMIT);

        context->active_sector = 0U;
        context->generation = 1U;
        context->storage = context->sectors[0];
        context->storage_size = context->sector_size;
        context->write_offset = FLASH_KV_SECTOR_HEADER_SIZE;

        return FLASH_KV_STATUS_OK;
    }

    if ((sector_a_valid != 0) &&
        ((sector_b_valid == 0) ||
         (read_u32_le(&sector_a[4]) >= read_u32_le(&sector_b[4]))))
    {
        context->active_sector = 0U;
        context->generation = read_u32_le(&sector_a[4]);
    }
    else
    {
        context->active_sector = 1U;
        context->generation = read_u32_le(&sector_b[4]);
    }

    context->storage =
        context->sectors[context->active_sector];
    context->storage_size = context->sector_size;

    context->write_offset =
        flash_kv_find_write_offset(context->storage,
                                   context->sector_size,
                                   FLASH_KV_SECTOR_HEADER_SIZE);

    return FLASH_KV_STATUS_OK;
}

flash_kv_status_t flash_kv_set(flash_kv_t *context,
                               const char *key,
                               const uint8_t *value,
                               size_t value_length)
{
    size_t key_length;
    size_t record_size;

    if ((context == NULL) ||
        (context->storage == NULL) ||
        (key == NULL))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    key_length = strlen(key);

    if ((key_length == 0U) ||
        (key_length > FLASH_KV_MAX_KEY_LENGTH))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    if (value_length > FLASH_KV_MAX_VALUE_LENGTH)
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    if ((value_length > 0U) && (value == NULL))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    record_size = FLASH_KV_HEADER_SIZE + key_length + value_length;

    if ((context->write_offset > flash_kv_capacity(context)) ||
        (record_size > (flash_kv_capacity(context) -
                        context->write_offset)))
    {
        if (context->dual_mode == 0U)
        {
            return FLASH_KV_STATUS_NO_SPACE;
        }

        if (flash_kv_gc(context) != FLASH_KV_STATUS_OK)
        {
            return FLASH_KV_STATUS_NO_SPACE;
        }

        if (record_size > (context->sector_size - context->write_offset))
        {
            return FLASH_KV_STATUS_NO_SPACE;
        }
    }

    return flash_kv_append_record(context->storage,
                                  flash_kv_capacity(context),
                                  &context->write_offset,
                                  (const uint8_t *)key,
                                  key_length,
                                  value,
                                  value_length);
}

flash_kv_status_t flash_kv_get(const flash_kv_t *context,
                               const char *key,
                               uint8_t *value,
                               size_t value_capacity,
                               size_t *value_length)
{
    size_t key_length;
    size_t offset;
    size_t latest_value_offset = 0U;
    size_t latest_value_length = 0U;
    int found = 0;
    int crc_error = 0;
    uint8_t crc_buffer[FLASH_KV_MAX_KEY_LENGTH +
                       FLASH_KV_MAX_VALUE_LENGTH];

    if ((context == NULL) ||
        (context->storage == NULL) ||
        (key == NULL) ||
        (value_length == NULL))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    offset = flash_kv_data_start(context);

    key_length = strlen(key);

    if ((key_length == 0U) ||
        (key_length > FLASH_KV_MAX_KEY_LENGTH))
    {
        return FLASH_KV_STATUS_INVALID_ARGUMENT;
    }

    while ((offset + FLASH_KV_HEADER_SIZE) <= context->write_offset)
    {
        const uint8_t *record = &context->storage[offset];
        uint16_t magic = read_u16_le(&record[0]);
        size_t stored_key_length = record[2];
        size_t stored_value_length = read_u16_le(&record[3]);
        size_t record_size;
        const uint8_t *stored_key;
        const uint8_t *stored_value;
        uint32_t stored_crc;
        uint32_t calculated_crc;

        if (magic != FLASH_KV_MAGIC)
        {
            break;
        }

        if ((stored_key_length == 0U) ||
            (stored_key_length > FLASH_KV_MAX_KEY_LENGTH) ||
            (stored_value_length > FLASH_KV_MAX_VALUE_LENGTH))
        {
            break;
        }

        record_size = FLASH_KV_HEADER_SIZE +
                      stored_key_length +
                      stored_value_length;

        if ((record_size > (context->storage_size - offset)) ||
            ((offset + record_size) > context->write_offset))
        {
            break;
        }

        if (record[9] != FLASH_KV_COMMIT_MARKER)
        {
            offset += record_size;
            continue;
        }

        stored_key = &record[FLASH_KV_HEADER_SIZE];
        stored_value = stored_key + stored_key_length;

        memcpy(crc_buffer, stored_key, stored_key_length);
        if (stored_value_length > 0U)
        {
            memcpy(crc_buffer + stored_key_length,
                   stored_value,
                   stored_value_length);
        }

        stored_crc = read_u32_le(&record[5]);
        calculated_crc = common_crc32_calc(
            crc_buffer,
            (uint32_t)(stored_key_length + stored_value_length));

        if (stored_crc != calculated_crc)
        {
            if ((stored_key_length == key_length) &&
                (memcmp(stored_key, key, key_length) == 0))
            {
                crc_error = 1;
            }
            offset += record_size;
            continue;
        }

        if ((stored_key_length == key_length) &&
            (memcmp(stored_key, key, key_length) == 0))
        {
            found = 1;
            latest_value_offset = (size_t)(stored_value - context->storage);
            latest_value_length = stored_value_length;
        }

        offset += record_size;
    }

    if (found == 0)
    {
        *value_length = 0U;
        return (crc_error != 0) ? FLASH_KV_STATUS_CRC_ERROR
                                : FLASH_KV_STATUS_NOT_FOUND;
    }

    *value_length = latest_value_length;

    if ((latest_value_length > 0U) &&
        ((value == NULL) || (value_capacity < latest_value_length)))
    {
        return FLASH_KV_STATUS_OUTPUT_TOO_SMALL;
    }

    if (latest_value_length > 0U)
    {
        memcpy(value,
               &context->storage[latest_value_offset],
               latest_value_length);
    }

    return FLASH_KV_STATUS_OK;
}
