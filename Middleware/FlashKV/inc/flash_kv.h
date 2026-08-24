#ifndef FLASH_KV_H
#define FLASH_KV_H

#include <stddef.h>
#include <stdint.h>

#define FLASH_KV_MAX_KEY_LENGTH     16U
#define FLASH_KV_MAX_VALUE_LENGTH   64U

#define FLASH_KV_SECTOR_COUNT       2U
#define FLASH_KV_SECTOR_MAGIC       0x53454354UL
#define FLASH_KV_SECTOR_COMMIT      0xA55AC33CUL
#define FLASH_KV_SECTOR_HEADER_SIZE 12U
#define FLASH_KV_MAX_RECORDS        16U

typedef enum
{
    FLASH_KV_STATUS_OK = 0,
    FLASH_KV_STATUS_INVALID_ARGUMENT,
    FLASH_KV_STATUS_NOT_FOUND,
    FLASH_KV_STATUS_NO_SPACE,
    FLASH_KV_STATUS_OUTPUT_TOO_SMALL,
    FLASH_KV_STATUS_CRC_ERROR
} flash_kv_status_t;

typedef struct
{
    uint8_t *storage;
    size_t storage_size;
    size_t write_offset;

    uint8_t *sectors[FLASH_KV_SECTOR_COUNT];
    size_t sector_size;
    size_t active_sector;
    uint32_t generation;
    uint8_t dual_mode;
} flash_kv_t;

typedef struct
{
    uint8_t valid;
    uint8_t key[FLASH_KV_MAX_KEY_LENGTH + 1U];
    uint8_t value[FLASH_KV_MAX_VALUE_LENGTH];
    size_t key_length;
    size_t value_length;
} flash_kv_latest_record_t;

flash_kv_status_t flash_kv_init(flash_kv_t *context,
                                uint8_t *storage,
                                size_t storage_size);

flash_kv_status_t flash_kv_init_dual(flash_kv_t *context,
                                     uint8_t *sector_a,
                                     uint8_t *sector_b,
                                     size_t sector_size);

flash_kv_status_t flash_kv_set(flash_kv_t *context,
                               const char *key,
                               const uint8_t *value,
                               size_t value_length);

flash_kv_status_t flash_kv_get(const flash_kv_t *context,
                               const char *key,
                               uint8_t *value,
                               size_t value_capacity,
                               size_t *value_length);

#endif /* FLASH_KV_H */
