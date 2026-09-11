#ifndef TEST_APP_PROTOCOL_STORAGE_TASK_H
#define TEST_APP_PROTOCOL_STORAGE_TASK_H

#include <stdint.h>

#define STORAGE_TASK_SDIO_STATE_READY 1U

typedef struct
{
    uint8_t state;
} storage_task_sdio_diag_t;

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag);

#endif
