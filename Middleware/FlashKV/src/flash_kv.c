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

flash_kv_status_t flash_kv_set(flash_kv_t *context,
                               const char *key,
                               const uint8_t *value,
                               size_t value_length)
{
    size_t key_length;
    size_t record_size;
    uint8_t crc_buffer[FLASH_KV_MAX_KEY_LENGTH + FLASH_KV_MAX_VALUE_LENGTH];

    uint32_t crc;
    uint8_t *record;

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

    if ((context->write_offset > context->storage_size) ||
        (record_size > (context->storage_size - context->write_offset)))
    {
        return FLASH_KV_STATUS_NO_SPACE;
    }
    
    memcpy(crc_buffer, key, key_length);

    if (value_length > 0U)
    {
        memcpy(crc_buffer + key_length, value, value_length);
    }
    
    crc = common_crc32_calc(crc_buffer, (uint32_t)(key_length + value_length));

    record = &context->storage[context->write_offset];

    write_u16_le(&record[0], FLASH_KV_MAGIC);
    record[2] = (uint8_t)key_length;
    write_u16_le(&record[3], (uint16_t)value_length);
    write_u32_le(&record[5], crc);

    record[9] = 0xFFU;

    memcpy(&record[FLASH_KV_HEADER_SIZE], key, key_length);

    if (value_length > 0U)
    {
        memcpy(&record[FLASH_KV_HEADER_SIZE + key_length], value, value_length);
    }

    record[9] = FLASH_KV_COMMIT_MARKER;

    context->write_offset += record_size;

    return FLASH_KV_STATUS_OK;
}

flash_kv_status_t flash_kv_get(const flash_kv_t *context,
                               const char *key,
                               uint8_t *value,
                               size_t value_capacity,
                               size_t *value_length)
{
    size_t key_length;
    size_t offset = 0U;
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
