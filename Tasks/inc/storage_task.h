#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <stdint.h>

#include "board_sdio.h"

#define STORAGE_TASK_FILE_PATH_MAX      32U

typedef enum
{
    STORAGE_TASK_SDIO_STATE_NOT_STARTED = 0,
    STORAGE_TASK_SDIO_STATE_RUNNING,
    STORAGE_TASK_SDIO_STATE_NO_CARD,
    STORAGE_TASK_SDIO_STATE_READY,
    STORAGE_TASK_SDIO_STATE_DEGRADED
} storage_task_sdio_state_t;

typedef enum
{
    STORAGE_TASK_REQUEST_READ_BLOCK = 0,
    STORAGE_TASK_REQUEST_WRITE_BLOCK
} storage_task_request_operation_t;

typedef enum
{
    STORAGE_TASK_FILE_WRITE = 0,
    STORAGE_TASK_FILE_READ
} storage_task_file_operation_t;

typedef struct
{
    storage_task_sdio_state_t state;
    uint8_t busy;
    uint8_t reserved[3];
    uint32_t last_status;

    board_sdio_card_info_t card;

    uint32_t read_status;
    uint32_t write_status;
    uint32_t write_ready_status;
    uint32_t write_ready_response;

    uint32_t notification_events;
    uint32_t dma_irq_events;
    uint32_t sdio_irq_events;
    uint32_t dma_irq_count;
    uint32_t sdio_irq_count;
} storage_task_sdio_diag_t;

typedef struct 
{
    uint32_t request_id;
    storage_task_request_operation_t operation;
    uint32_t block_number;
    uint16_t length;
    uint16_t reserved;
    uint8_t *buffer;
} storage_task_request_t;

typedef struct 
{
    uint32_t request_id;
    board_sdio_status_t status;
} storage_task_request_result_t;

typedef struct 
{
    uint32_t request_id;
    storage_task_file_operation_t operation;
    char path[STORAGE_TASK_FILE_PATH_MAX];
    uint8_t *buffer;
    uint32_t length;
} storage_task_file_request_t;

typedef struct
{
    uint32_t request_id;
    uint32_t result;
    uint32_t transferred;
} storage_task_file_result_t;

int storage_task_file_request_submit(const storage_task_file_request_t *request);
int storage_task_file_result_get(storage_task_file_result_t *result, uint32_t timeout_ms);

int storage_task_create(void);

uint32_t storage_task_get_heartbeat(void);
uint32_t storage_task_get_stack_high_water_mark(void);

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag);

int storage_task_request_submit(const storage_task_request_t *request);
int storage_task_request_result_get(storage_task_request_result_t *result, uint32_t timeout_ms);

#endif /* STORAGE_TASK_H */
