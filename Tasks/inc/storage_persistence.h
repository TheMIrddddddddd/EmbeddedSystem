#ifndef STORAGE_PERSISTENCE_H
#define STORAGE_PERSISTENCE_H

#include <stdint.h>

#include "storage_task.h"

#define STORAGE_PERSISTENCE_FLASH_CAPACITY       0x00080000UL
#define STORAGE_PERSISTENCE_SECTOR_SIZE          0x00001000UL
#define STORAGE_PERSISTENCE_SECTOR_A_ADDRESS     0x00000000UL
#define STORAGE_PERSISTENCE_SECTOR_B_ADDRESS     0x00001000UL
#define STORAGE_PERSISTENCE_CONFIG_KEY           "config"
#define STORAGE_PERSISTENCE_VALUE_MAX            64U

typedef enum
{
    STORAGE_PERSISTENCE_STATUS_OK = 0,
    STORAGE_PERSISTENCE_STATUS_NOT_READY,
    STORAGE_PERSISTENCE_STATUS_NOT_FOUND,
    STORAGE_PERSISTENCE_STATUS_FLASH_ERROR,
    STORAGE_PERSISTENCE_STATUS_DATA_ERROR,
    STORAGE_PERSISTENCE_STATUS_OUTPUT_TOO_SMALL,
    STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT
} storage_persistence_status_t;

storage_persistence_status_t storage_persistence_init(void);

storage_persistence_status_t storage_persistence_config_read(
    uint8_t *payload,
    uint16_t capacity,
    uint16_t *length);

storage_persistence_status_t storage_persistence_config_save(
    const uint8_t *payload,
    uint16_t length);

storage_persistence_status_t storage_persistence_flash_diag(uint8_t id[3]);

int storage_persistence_request_handle(
    const storage_task_persist_request_t *request,
    storage_task_persist_result_t *result);

#endif /* STORAGE_PERSISTENCE_H */
