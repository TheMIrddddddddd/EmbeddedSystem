#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <stdint.h>

#include "board_sdio.h"

#define STORAGE_TASK_FILE_PATH_MAX      32U
#define STORAGE_TASK_PERSIST_PAYLOAD_MAX 64U
#define STORAGE_TASK_RECORD_QUEUE_LENGTH 16U
#define STORAGE_TASK_ALARM_RECORD_SIZE   13U
#define STORAGE_TASK_ALARM_RECORD_MAX    10U
#define STORAGE_TASK_ALARM_QUERY_PAYLOAD_MAX \
    (1U + (STORAGE_TASK_ALARM_RECORD_SIZE * STORAGE_TASK_ALARM_RECORD_MAX))
#define STORAGE_TASK_CONFIG_IMPORT_ERROR_OPEN 1U
#define STORAGE_TASK_CONFIG_IMPORT_ERROR_READ 2U
#define STORAGE_TASK_CONFIG_IMPORT_ERROR_PARSE 3U
#define STORAGE_TASK_CONFIG_IMPORT_ERROR_SIZE 4U
#define STORAGE_TASK_CONFIG_IMPORT_ERROR_SERVICE 5U

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
    STORAGE_TASK_FILE_READ,
    STORAGE_TASK_FILE_APPEND
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

typedef enum
{
    STORAGE_TASK_RECORD_SAMPLE = 0,
    STORAGE_TASK_RECORD_ALARM,
    STORAGE_TASK_RECORD_AUDIT
} storage_task_record_type_t;

typedef enum
{
    STORAGE_TASK_AUDIT_SAMPLE_START = 0,
    STORAGE_TASK_AUDIT_SAMPLE_STOP,
    STORAGE_TASK_AUDIT_RATIO_SET,
    STORAGE_TASK_AUDIT_LIMIT_SET,
    STORAGE_TASK_AUDIT_PROTOCOL_SET,
    STORAGE_TASK_AUDIT_DEVICE_ID_SET,
    STORAGE_TASK_AUDIT_BAUDRATE_SET,
    STORAGE_TASK_AUDIT_HIDE_ON,
    STORAGE_TASK_AUDIT_HIDE_OFF,
    STORAGE_TASK_AUDIT_SYSTEM_TEST,
    STORAGE_TASK_AUDIT_CONFIG_IMPORT,
    STORAGE_TASK_AUDIT_ALARM_ACTIVE,
    STORAGE_TASK_AUDIT_ALARM_RECOVERED
} storage_task_audit_event_t;

typedef struct
{
    uint8_t type;
    uint8_t event;
    uint8_t channel;
    uint8_t reserved;
    float value0;
    float value1;
    float threshold;
    float actual;
    uint32_t argument;
} storage_task_record_request_t;

typedef enum
{
    STORAGE_TASK_PERSIST_CONFIG_LOAD = 0,
    STORAGE_TASK_PERSIST_CONFIG_READ,
    STORAGE_TASK_PERSIST_CONFIG_SAVE,
    STORAGE_TASK_PERSIST_FLASH_DIAG,
    STORAGE_TASK_PERSIST_CONFIG_IMPORT
} storage_task_persist_operation_t;

typedef enum
{
    STORAGE_TASK_PERSIST_STATUS_OK = 0,
    STORAGE_TASK_PERSIST_STATUS_INVALID_ARGUMENT,
    STORAGE_TASK_PERSIST_STATUS_NOT_FOUND,
    STORAGE_TASK_PERSIST_STATUS_NOT_READY,
    STORAGE_TASK_PERSIST_STATUS_BUSY,
    STORAGE_TASK_PERSIST_STATUS_FLASH_ERROR,
    STORAGE_TASK_PERSIST_STATUS_UNSUPPORTED,
    STORAGE_TASK_PERSIST_STATUS_DATA_ERROR
} storage_task_persist_status_t;

#define STORAGE_TASK_PERSIST_ORIGIN_BOOT     1U
#define STORAGE_TASK_PERSIST_ORIGIN_CONTROL  2U
#define STORAGE_TASK_PERSIST_ORIGIN_ALARM    3U

typedef struct
{
    uint32_t request_id;
    uint32_t deadline_tick;
    uint16_t payload_length;
    uint8_t operation;
    uint8_t origin;
    uint8_t payload[STORAGE_TASK_PERSIST_PAYLOAD_MAX];
} storage_task_persist_request_t;

typedef struct
{
    uint32_t request_id;
    uint16_t payload_length;
    uint8_t operation;
    uint8_t status;
    uint8_t payload[STORAGE_TASK_PERSIST_PAYLOAD_MAX];
} storage_task_persist_result_t;

int storage_task_file_request_submit(const storage_task_file_request_t *request);
int storage_task_file_result_get(storage_task_file_result_t *result, uint32_t timeout_ms);

int storage_task_sample_record_submit(float ch0, float ch1);
int storage_task_alarm_record_submit(uint8_t channel, float threshold,
                                     float actual, uint32_t timestamp);
int storage_task_audit_event_submit(uint8_t event, uint8_t channel,
                                    float value0, float value1,
                                    uint32_t argument);

int storage_task_alarm_records_get(uint8_t *payload, uint16_t capacity,
                                   uint16_t *length);
int storage_task_alarm_records_clear(void);

int storage_task_persist_request_submit(
    const storage_task_persist_request_t *request);
/* 提交 TF 卡 config.ini 导入请求，由 StorageTask 读取并保存。 */
int storage_task_config_import_submit(uint32_t request_id,
                                      uint32_t deadline_tick,
                                      uint8_t origin);
int storage_task_persist_result_get(
    storage_task_persist_result_t *result,
    uint32_t timeout_ms);

int storage_task_create(void);

uint32_t storage_task_get_heartbeat(void);
uint32_t storage_task_get_stack_high_water_mark(void);

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag);
uint8_t storage_task_fatfs_mounted_get(void);
uint32_t storage_task_fatfs_mount_result_get(void);

int storage_task_request_submit(const storage_task_request_t *request);
int storage_task_request_result_get(storage_task_request_result_t *result, uint32_t timeout_ms);

#endif /* STORAGE_TASK_H */
