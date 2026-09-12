#ifndef TEST_APP_PROTOCOL_STORAGE_TASK_H
#define TEST_APP_PROTOCOL_STORAGE_TASK_H

#include <stdint.h>

#define STORAGE_TASK_SDIO_STATE_READY 1U

typedef struct
{
    uint8_t state;
} storage_task_sdio_diag_t;

enum
{
    STORAGE_TASK_AUDIT_RATIO_SET = 2U,
    STORAGE_TASK_AUDIT_LIMIT_SET = 3U,
    STORAGE_TASK_AUDIT_PROTOCOL_SET = 4U,
    STORAGE_TASK_AUDIT_DEVICE_ID_SET = 5U,
    STORAGE_TASK_AUDIT_BAUDRATE_SET = 6U
};

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag);
uint8_t storage_task_fatfs_mounted_get(void);
int storage_task_audit_event_submit(uint8_t event, uint8_t channel,
                                    float value0, float value1,
                                    uint32_t argument);
int storage_task_alarm_records_get(uint8_t *payload, uint16_t capacity,
                                   uint16_t *length);
int storage_task_alarm_records_clear(void);

#endif
