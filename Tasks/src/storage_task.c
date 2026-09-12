#include "storage_task.h"
#include "storage_persist_queue.h"
#include "storage_persistence.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "board_sdio.h"
#include "board_rtc.h"

#include "diskio.h"
#include "ff.h"
#include "app_config_import.h"
#include "app_config_ini.h"
#include "storage_record_format.h"
#include "storage_mount_policy.h"

#define STORAGE_TASK_PRIORITY              3U
#define STORAGE_TASK_STACK_DEPTH           512U

#define STORAGE_SDIO_NOTIFY_DMA_DONE       (1UL << 0)
#define STORAGE_SDIO_NOTIFY_DTEND          (1UL << 1)
#define STORAGE_SDIO_NOTIFY_DTBLKEND       (1UL << 2)
#define STORAGE_SDIO_NOTIFY_DMA_FIFO       (1UL << 3)
#define STORAGE_SDIO_NOTIFY_DMA_ERROR      (1UL << 4)
#define STORAGE_SDIO_NOTIFY_SDIO_ERROR     (1UL << 5)

#define STORAGE_TASK_REQUEST_QUEUE_LENGTH   4U
#define STORAGE_TASK_RESULT_QUEUE_LENGTH    4U

#define STORAGE_CARD_INSERT_DEBOUNCE_MS     100U
#define STORAGE_FATFS_MOUNT_RETRY_COUNT     2U
#define STORAGE_TASK_ALARM_RPC_QUEUE_LENGTH 4U
#define STORAGE_TASK_ALARM_RPC_TIMEOUT_MS   400U
#define STORAGE_RECORD_PATH_MAX             48U
#define STORAGE_RECORD_MAX_ROWS             10U

typedef enum
{
    STORAGE_TASK_ALARM_RPC_QUERY = 0,
    STORAGE_TASK_ALARM_RPC_CLEAR
} storage_task_alarm_rpc_operation_t;

typedef struct
{
    uint32_t request_id;
    uint8_t operation;
    uint8_t reserved[3];
} storage_task_alarm_rpc_request_t;

typedef struct
{
    uint32_t request_id;
    uint16_t payload_length;
    uint8_t operation;
    uint8_t status;
    uint8_t payload[STORAGE_TASK_ALARM_QUERY_PAYLOAD_MAX];
} storage_task_alarm_rpc_result_t;

static StaticTask_t s_storage_task_tcb;
static StackType_t  s_storage_task_stack[STORAGE_TASK_STACK_DEPTH];

static volatile uint32_t s_storage_task_heartbeat;
static volatile uint32_t s_storage_task_stack_high_water_mark;
static TaskHandle_t s_storage_task_handle;
static StaticQueue_t s_storage_request_queue;
static StaticQueue_t s_storage_result_queue;

static StaticQueue_t s_storage_file_request_queue;
static StaticQueue_t s_storage_file_result_queue;
static StaticQueue_t s_storage_record_queue;
static StaticQueue_t s_storage_alarm_rpc_request_queue;
static StaticQueue_t s_storage_alarm_rpc_result_queue;

__align(8)
static uint8_t s_storage_request_queue_storage[STORAGE_TASK_REQUEST_QUEUE_LENGTH * sizeof(storage_task_request_t)];

__align(8)
static uint8_t s_storage_result_queue_storage[STORAGE_TASK_RESULT_QUEUE_LENGTH * sizeof(storage_task_request_result_t)];

__align(8)
static uint8_t s_storage_file_request_queue_storage[STORAGE_TASK_REQUEST_QUEUE_LENGTH  * sizeof(storage_task_file_request_t)];

__align(8)
static uint8_t s_storage_file_result_queue_storage[STORAGE_TASK_RESULT_QUEUE_LENGTH * sizeof(storage_task_file_result_t)];

__align(8)
static uint8_t s_storage_record_queue_storage[STORAGE_TASK_RECORD_QUEUE_LENGTH * sizeof(storage_task_record_request_t)];

__align(8)
static uint8_t s_storage_alarm_rpc_request_queue_storage[
    STORAGE_TASK_ALARM_RPC_QUEUE_LENGTH *
    sizeof(storage_task_alarm_rpc_request_t)];

__align(8)
static uint8_t s_storage_alarm_rpc_result_queue_storage[
    STORAGE_TASK_ALARM_RPC_QUEUE_LENGTH *
    sizeof(storage_task_alarm_rpc_result_t)];

static QueueHandle_t s_storage_request_queue_handle;
static QueueHandle_t s_storage_result_queue_handle;
static QueueHandle_t s_storage_file_request_queue_handle;
static QueueHandle_t s_storage_file_result_queue_handle;
static QueueHandle_t s_storage_record_queue_handle;
static QueueHandle_t s_storage_alarm_rpc_request_queue_handle;
static QueueHandle_t s_storage_alarm_rpc_result_queue_handle;
static storage_task_persist_request_t s_storage_persist_request;
static storage_task_persist_result_t s_storage_persist_result;
static uint32_t s_storage_alarm_rpc_next_request_id;
static uint8_t s_storage_config_file[APP_CONFIG_INI_FILE_MAX + 1U];
static uint8_t s_storage_config_encoded[APP_CONFIG_SERIALIZED_SIZE];

typedef struct
{
    FIL file;
    char path[STORAGE_RECORD_PATH_MAX];
    uint8_t open;
    uint8_t row_count;
} storage_record_file_t;

static storage_record_file_t s_storage_sample_file;
static storage_record_file_t s_storage_alarm_file;
static storage_record_file_t s_storage_audit_file;
static uint32_t s_storage_audit_boot_count;
static uint8_t s_storage_audit_boot_written;
static char s_storage_record_line[STORAGE_RECORD_TEXT_MAX];
static uint8_t s_storage_record_scan_buffer[128U];
static storage_task_record_request_t s_storage_record_request;
static storage_task_alarm_rpc_request_t s_storage_alarm_rpc_request;
static storage_task_alarm_rpc_result_t s_storage_alarm_rpc_result;
static storage_task_alarm_rpc_result_t s_storage_alarm_rpc_wait_result;
static FATFS s_storage_fatfs;
static volatile FRESULT s_storage_fatfs_mount_result = FR_NOT_READY;
static volatile uint8_t s_storage_fatfs_mounted;
static volatile FRESULT s_storage_fatfs_unmount_result = FR_OK;

static TickType_t s_storage_card_insert_tick;
static uint8_t s_storage_card_insert_pending;
static uint8_t s_storage_card_insert_attempted;

static storage_task_sdio_diag_t s_storage_task_sdio_diag;

static volatile uint32_t s_storage_task_dma_irq_events;
static volatile uint32_t s_storage_task_sdio_irq_events;
static volatile uint32_t s_storage_task_dma_irq_count;
static volatile uint32_t s_storage_task_sdio_irq_count;

/* 记录文件管理函数在插拔卡状态机前置使用。 */
static void storage_task_record_files_close(void);
static int storage_task_record_directories_prepare(void);
static int storage_task_audit_open(uint8_t write_boot_line);
static void storage_task_record_setup_after_mount(uint8_t write_boot_line);
static int storage_task_audit_write_boot_line(void);
static void storage_task_process_alarm_rpc_request(void);

static void storage_sdio_initialize(void)
{
    board_sdio_status_t status;

    diskio_sdio_set_not_ready();

    s_storage_task_sdio_diag.state = STORAGE_TASK_SDIO_STATE_RUNNING;

    status = board_sdio_card_init(&s_storage_task_sdio_diag.card);

    s_storage_task_sdio_diag.last_status = (uint32_t)status;
    s_storage_task_sdio_diag.busy = 0U;

    if (status == BOARD_SDIO_STATUS_OK)
    {
        diskio_sdio_set_ready(s_storage_task_sdio_diag.card.rca);
        s_storage_task_sdio_diag.state = STORAGE_TASK_SDIO_STATE_READY;
    }
    else if (status == BOARD_SDIO_STATUS_NO_CARD)
    {
        s_storage_task_sdio_diag.state = STORAGE_TASK_SDIO_STATE_NO_CARD;
    }
    else
    {
        s_storage_task_sdio_diag.state = STORAGE_TASK_SDIO_STATE_DEGRADED;
    }
}

static void storage_fatfs_mount(void)
{
    uint8_t attempt;

    s_storage_fatfs_mounted = 0U;
    s_storage_fatfs_mount_result = FR_NOT_READY;

    if (s_storage_task_sdio_diag.state != STORAGE_TASK_SDIO_STATE_READY)
    {
        return;
    }

    for (attempt = 0U; attempt < STORAGE_FATFS_MOUNT_RETRY_COUNT; attempt++)
    {
        s_storage_fatfs_mount_result = f_mount(&s_storage_fatfs, "0:", 1U);

        if (s_storage_fatfs_mount_result == FR_OK)
        {
            s_storage_fatfs_mounted = 1U;
            break;
        }

        /*
         * SDIO 初始化后的首次 DMA 读偶发 FEE：重新识别卡并立即重挂一次，
         * 不把瞬态错误暴露给系统自检或 Q-02。
         */
        if ((s_storage_fatfs_mount_result != FR_DISK_ERR) ||
            ((attempt + 1U) >= STORAGE_FATFS_MOUNT_RETRY_COUNT))
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(STORAGE_CARD_INSERT_DEBOUNCE_MS));

        storage_sdio_initialize();

        if (s_storage_task_sdio_diag.state != STORAGE_TASK_SDIO_STATE_READY)
        {
            break;
        }
    }
}

static void storage_card_remove_process(void)
{
    if (board_sdio_card_present() != 0U)
    {
        return;
    }

    /* 无卡状态已处理，避免每轮重复卸载。 */
    if ((s_storage_task_sdio_diag.state == STORAGE_TASK_SDIO_STATE_NO_CARD) && (s_storage_fatfs_mounted == 0U))
    {
        return;
    }

    /* 先关闭文件访问入口，再清除底层就绪状态。 */
    storage_task_record_files_close();
    s_storage_fatfs_mounted = 0U;
    s_storage_fatfs_mount_result = FR_NOT_READY;

    diskio_sdio_set_not_ready();

    /* 注销逻辑盘的文件系统对象。 */
    s_storage_fatfs_unmount_result = f_mount(NULL, "0:", 0U);

    s_storage_task_sdio_diag.state = STORAGE_TASK_SDIO_STATE_NO_CARD;
    s_storage_task_sdio_diag.last_status = (uint32_t)BOARD_SDIO_STATUS_NO_CARD;
    s_storage_task_sdio_diag.busy = 0U;
}

static void storage_card_insert_process(void)
{
    TickType_t now_tick;

    /* 无卡时，允许下一次插卡重新尝试。 */
    if (board_sdio_card_present() == 0U)
    {
        s_storage_card_insert_pending = 0U;
        s_storage_card_insert_attempted = 0U;
        return;
    }

    /* 已挂载，或本次插卡已经尝试过，不重复初始化。 */
    if ((s_storage_fatfs_mounted != 0U) || (s_storage_card_insert_attempted != 0U))
    {
        return;
    }

    now_tick = xTaskGetTickCount();

    if (s_storage_card_insert_pending == 0U)
    {
        s_storage_card_insert_tick = now_tick;
        s_storage_card_insert_pending = 1U;
        return;
    }
    
    if ((TickType_t)(now_tick - s_storage_card_insert_tick) < pdMS_TO_TICKS(STORAGE_CARD_INSERT_DEBOUNCE_MS))
    {
        return;
    }

    s_storage_card_insert_pending = 0U;
    s_storage_card_insert_attempted = 1U;

    /* 重新识别卡，并更新 diskio 的 RCA 和就绪状态。 */
    storage_sdio_initialize();

    /* 初始化失败时保留 BSP 状态，并允许去抖后再次尝试。 */
    if (s_storage_task_sdio_diag.state != STORAGE_TASK_SDIO_STATE_READY)
    {
        s_storage_card_insert_attempted = 0U;
        return;
    }
    storage_fatfs_mount();
    storage_task_record_setup_after_mount(
        (s_storage_audit_boot_written == 0U) ? 1U : 0U);
    if (storage_mount_policy_retry(
            (board_sdio_card_present() != 0U) ? 1U : 0U,
            s_storage_fatfs_mounted,
            s_storage_card_insert_attempted) != 0U)
    {
        s_storage_card_insert_attempted = 0U;
    }
}

static void storage_task_sdio_irq_callback(uint32_t dma_events, uint32_t sdio_events)
{
    uint32_t notify_value;
    BaseType_t higher_priority_task_woken;

    notify_value = 0U;
    higher_priority_task_woken = pdFALSE;

    if (dma_events != 0U)
    {
        s_storage_task_dma_irq_events |= dma_events;
        s_storage_task_dma_irq_count++;
    }

    if (sdio_events != 0U)
    {
        s_storage_task_sdio_irq_events |= sdio_events;
        s_storage_task_sdio_irq_count++;
    }

    if ((dma_events & BOARD_SDIO_DMA_IRQ_EVENT_FTF) != 0U)
    {
        notify_value |= STORAGE_SDIO_NOTIFY_DMA_DONE;
    }

    if ((sdio_events & BOARD_SDIO_IRQ_EVENT_DTEND) != 0U)
    {
        notify_value |= STORAGE_SDIO_NOTIFY_DTEND;
    }

    if ((sdio_events & BOARD_SDIO_IRQ_EVENT_DTBLKEND) != 0U)
    {
        notify_value |= STORAGE_SDIO_NOTIFY_DTBLKEND;
    }

    if ((dma_events & BOARD_SDIO_DMA_IRQ_EVENT_FEE) != 0U)
    {
        notify_value |= STORAGE_SDIO_NOTIFY_DMA_FIFO;
    }

    if ((dma_events &
         (BOARD_SDIO_DMA_IRQ_EVENT_FATAL |
          BOARD_SDIO_DMA_IRQ_EVENT_SDE    |
          BOARD_SDIO_DMA_IRQ_EVENT_TAE)) != 0U)
    {
        notify_value |= STORAGE_SDIO_NOTIFY_DMA_ERROR;
    }

    if ((sdio_events &
         (BOARD_SDIO_IRQ_EVENT_DTCRCERR |
          BOARD_SDIO_IRQ_EVENT_DTTMOUT  |
          BOARD_SDIO_IRQ_EVENT_TXURE    |
          BOARD_SDIO_IRQ_EVENT_RXORE    |
          BOARD_SDIO_IRQ_EVENT_STBITE)) != 0U)
    {
        notify_value |= STORAGE_SDIO_NOTIFY_SDIO_ERROR;
    }

    if ((notify_value != 0U) &&
        (s_storage_task_handle != NULL))
    {
        (void)xTaskNotifyFromISR(
            s_storage_task_handle,
            notify_value,
            eSetBits,
            &higher_priority_task_woken);

        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static void storage_task_request_result_send(uint32_t request_id, board_sdio_status_t status)
{
    storage_task_request_result_t result;

    if (s_storage_result_queue_handle == NULL)
    {
        return;
    }
    
    result.request_id = request_id;
    result.status = status;

    (void)xQueueSend(s_storage_result_queue_handle, &result, 0U);
}

static void storage_task_file_result_send(uint32_t request_id, FRESULT result, UINT transferred)
{
    storage_task_file_result_t response;

    if (s_storage_file_result_queue_handle == NULL)
    {
        return;
    }

    response.request_id = request_id;
    response.result = (uint32_t)result;
    response.transferred = (uint32_t)transferred;

    (void)xQueueSend(s_storage_file_result_queue_handle, &response, 0U);
}

static void storage_task_process_request(void)
{
    storage_task_request_t request;
    board_sdio_status_t status;
    uint32_t ready_response;

    if (s_storage_request_queue_handle == NULL)
    {
        return;
    }
    
    if (xQueueReceive(s_storage_request_queue_handle, &request, 0U) != pdPASS)
    {
        return;
    }

    if (s_storage_task_sdio_diag.state != STORAGE_TASK_SDIO_STATE_READY)
    {
        s_storage_task_sdio_diag.last_status =
            (uint32_t)BOARD_SDIO_STATUS_NOT_READY;
        storage_task_request_result_send(request.request_id, BOARD_SDIO_STATUS_NOT_READY);

        return;
    }
    
    if ((request.buffer == NULL) || (request.length != BOARD_SDIO_BLOCK_SIZE) || ((((uint32_t)request.buffer) & 0x03U) != 0U))
    {
        s_storage_task_sdio_diag.last_status =
            (uint32_t)BOARD_SDIO_STATUS_INVALID_ARGUMENT;
        storage_task_request_result_send(request.request_id, BOARD_SDIO_STATUS_INVALID_ARGUMENT);
        return;
    }

    s_storage_task_sdio_diag.busy = 1U;
    ready_response = 0U;

    switch (request.operation)
    {
    case STORAGE_TASK_REQUEST_READ_BLOCK:
        status = board_sdio_read_block_dma_polling(
            request.block_number,
            request.buffer);
        s_storage_task_sdio_diag.read_status =
            (uint32_t)status;
        break;
    
    case STORAGE_TASK_REQUEST_WRITE_BLOCK:
        status = board_sdio_write_block_dma_polling(
            request.block_number,
            request.buffer);
        s_storage_task_sdio_diag.write_status =
            (uint32_t)status;

        if (status == BOARD_SDIO_STATUS_OK)
        {
            status = board_sdio_wait_card_ready(
                s_storage_task_sdio_diag.card.rca,
                &ready_response);
            s_storage_task_sdio_diag.write_ready_status =
                (uint32_t)status;
            s_storage_task_sdio_diag.write_ready_response =
                ready_response;
        }
        break;
    default:
        status = BOARD_SDIO_STATUS_INVALID_ARGUMENT;
        break;
    }

    s_storage_task_sdio_diag.last_status =
        (uint32_t)status;
    s_storage_task_sdio_diag.busy = 0U;

    storage_task_request_result_send(request.request_id, status);
}

static int storage_task_file_path_valid(const char *path)
{
    uint32_t index;

    if (path == NULL)
    {
        return 0;
    }
    
    for (index = 0U; index < STORAGE_TASK_FILE_PATH_MAX; index++)
    {
        if (path[index] == '\0')
        {
            return (index > 0U) ? 1 : 0;
        }
    }
    return 0;
}

/* 向路径/日志临时缓冲区追加文本，保留末尾 NUL 空间。 */
static int storage_task_text_append(char *buffer, uint16_t capacity,
                                    uint16_t *position, const char *text)
{
    uint16_t index = 0U;
    while (text[index] != '\0')
    {
        if ((*position + 1U) >= capacity) return 0;
        buffer[*position] = text[index++];
        *position = (uint16_t)(*position + 1U);
    }
    return 1;
}

/* 向文本追加固定宽度十进制数字。 */
static int storage_task_text_fixed(char *buffer, uint16_t capacity,
                                   uint16_t *position, uint32_t value,
                                   uint8_t digits)
{
    uint32_t divisor = 1U;
    uint8_t index;
    for (index = 1U; index < digits; index++) divisor *= 10U;
    if (value >= divisor * 10U) return 0;
    for (index = 0U; index < digits; index++)
    {
        if ((*position + 1U) >= capacity) return 0;
        buffer[*position] = (char)('0' + ((value / divisor) % 10U));
        *position = (uint16_t)(*position + 1U);
        divisor /= 10U;
    }
    return 1;
}

/* 向审计文本追加无符号十进制数。 */
static int storage_task_text_u32(char *buffer, uint16_t capacity,
                                 uint16_t *position, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;
    uint8_t index;
    do { digits[count++] = (char)('0' + (value % 10U)); value /= 10U; } while (value != 0U);
    for (index = count; index > 0U; index--)
    {
        if ((*position + 1U) >= capacity) return 0;
        buffer[*position] = digits[index - 1U];
        *position = (uint16_t)(*position + 1U);
    }
    return 1;
}

/* 向审计文本追加 4 位十六进制设备 ID。 */
static int storage_task_text_hex4(char *buffer, uint16_t capacity,
                                  uint16_t *position, uint16_t value)
{
    uint8_t index;
    for (index = 0U; index < 4U; index++)
    {
        uint8_t shift = (uint8_t)(12U - (index * 4U));
        uint8_t digit = (uint8_t)((value >> shift) & 0x0FU);
        if ((*position + 1U) >= capacity) return 0;
        buffer[*position] = (char)((digit < 10U) ? ('0' + digit) : ('A' + digit - 10U));
        *position = (uint16_t)(*position + 1U);
    }
    return 1;
}

/* 向审计文本追加非负浮点数的两位小数。 */
static int storage_task_text_fixed2(char *buffer, uint16_t capacity,
                                    uint16_t *position, float value)
{
    uint32_t scaled;
    if (!(value >= 0.0f) || !(value <= 1000000.0f)) return 0;
    scaled = (uint32_t)((value * 100.0f) + 0.5f);
    return (storage_task_text_u32(buffer, capacity, position, scaled / 100U) != 0) &&
           (storage_task_text_append(buffer, capacity, position, ".") != 0) &&
           (storage_task_text_fixed(buffer, capacity, position, scaled % 100U, 2U) != 0);
}

/* 路径构造使用的 NUL 终止辅助函数前置声明。 */
static int storage_task_text_terminate(char *buffer, uint16_t capacity,
                                       uint16_t *position);

/* 构造 sample/alarm 时间文件路径，使用 ASCII 固定格式兼容 FatFs 当前配置。 */
static int storage_task_build_time_path(const char *directory, const char *prefix,
                                        const board_rtc_time_t *time,
                                        char *path, uint16_t capacity)
{
    uint16_t position = 0U;
    if ((directory == 0) || (prefix == 0) || (time == 0) || (path == 0)) return 0;
    if ((storage_task_text_append(path, capacity, &position, "0:/") == 0) ||
        (storage_task_text_append(path, capacity, &position, directory) == 0) ||
        (storage_task_text_append(path, capacity, &position, "/") == 0) ||
        (storage_task_text_append(path, capacity, &position, prefix) == 0) ||
        (storage_task_text_fixed(path, capacity, &position, time->year, 4U) == 0) ||
        (storage_task_text_fixed(path, capacity, &position, time->month, 2U) == 0) ||
        (storage_task_text_fixed(path, capacity, &position, time->date, 2U) == 0) ||
        (storage_task_text_append(path, capacity, &position, "_") == 0) ||
        (storage_task_text_fixed(path, capacity, &position, time->hour, 2U) == 0) ||
        (storage_task_text_fixed(path, capacity, &position, time->minute, 2U) == 0) ||
        (storage_task_text_fixed(path, capacity, &position, time->second, 2U) == 0) ||
        (storage_task_text_append(path, capacity, &position, ".csv") == 0)) return 0;
    if (storage_task_text_terminate(path, capacity, &position) == 0) return 0;
    return 1;
}

/* 统计已有 CSV 文件的换行条数，避免重启后向已满文件继续追加。 */
static int storage_task_count_file_rows(const char *path, uint8_t *count)
{
    FIL file;
    UINT transferred;
    FRESULT result;
    uint8_t rows = 0U;
    uint16_t index;
    result = f_open(&file, path, FA_READ);
    if (result != FR_OK) return 0;
    do
    {
        result = f_read(&file, s_storage_record_scan_buffer,
                        sizeof(s_storage_record_scan_buffer), &transferred);
        if (result != FR_OK) { (void)f_close(&file); return 0; }
        for (index = 0U; index < transferred; index++)
            if (s_storage_record_scan_buffer[index] == '\n' && rows < 255U) rows++;
    } while (transferred == sizeof(s_storage_record_scan_buffer));
    if (f_close(&file) != FR_OK) return 0;
    *count = rows;
    return 1;
}

/* 打开一个可滚动 CSV 文件；已有未满文件从原记录数继续追加。 */
static int storage_task_roll_file_open(storage_record_file_t *state,
                                       const char *directory, const char *prefix,
                                       const board_rtc_time_t *time)
{
    uint8_t rows = 0U;
    FRESULT result;
    if ((state == 0) || (directory == 0) || (prefix == 0) || (time == 0)) return 0;
    if (state->open != 0U) return (state->row_count < STORAGE_RECORD_MAX_ROWS) ? 1 : 0;
    if (storage_task_build_time_path(directory, prefix, time,
                                     state->path, sizeof(state->path)) == 0) return 0;
    result = f_open(&state->file, state->path, FA_OPEN_APPEND | FA_WRITE);
    if (result != FR_OK) return 0;
    if (f_size(&state->file) != 0U)
    {
        (void)f_close(&state->file);
        if ((storage_task_count_file_rows(state->path, &rows) == 0) ||
            (rows >= STORAGE_RECORD_MAX_ROWS))
        {
            return 0;
        }
        result = f_open(&state->file, state->path, FA_OPEN_APPEND | FA_WRITE);
        if (result != FR_OK) return 0;
    }
    state->row_count = rows;
    state->open = 1U;
    return 1;
}

/* 写入一行并强制 f_sync；失败时关闭当前文件，避免继续使用坏句柄。 */
static int storage_task_record_write_line(storage_record_file_t *state,
                                          const char *line, uint16_t length,
                                          uint8_t count_row)
{
    UINT transferred;
    FRESULT result;
    if ((state == 0) || (state->open == 0U) || (line == 0) || (length == 0U)) return 0;
    result = f_write(&state->file, line, length, &transferred);
    if ((result != FR_OK) || (transferred != length) ||
        (f_sync(&state->file) != FR_OK))
    {
        (void)f_close(&state->file);
        state->open = 0U;
        state->row_count = 0U;
        return 0;
    }
    if (count_row != 0U) state->row_count++;
    return 1;
}

/* 关闭一个业务文件并同步；StorageTask 拔卡前统一调用。 */
static void storage_task_record_file_close(storage_record_file_t *state)
{
    if ((state != 0) && (state->open != 0U))
    {
        (void)f_sync(&state->file);
        (void)f_close(&state->file);
        state->open = 0U;
        state->row_count = 0U;
    }
}

/* 关闭 sample、alarm、audit 三个当前文件。 */
static void storage_task_record_files_close(void)
{
    storage_task_record_file_close(&s_storage_sample_file);
    storage_task_record_file_close(&s_storage_alarm_file);
    storage_task_record_file_close(&s_storage_audit_file);
}

/* 确保三类业务目录存在；目录已存在视为成功。 */
static int storage_task_record_directories_prepare(void)
{
    FRESULT sample = f_mkdir("0:/sample");
    FRESULT alarm = f_mkdir("0:/alarm");
    FRESULT audit = f_mkdir("0:/audit");
    return (((sample == FR_OK) || (sample == FR_EXIST)) &&
            ((alarm == FR_OK) || (alarm == FR_EXIST)) &&
            ((audit == FR_OK) || (audit == FR_EXIST))) ? 1 : 0;
}

/* 挂载后初始化业务目录和 audit 文件；失败则主动卸载，交给插卡状态机重试。 */
static void storage_task_record_setup_after_mount(uint8_t write_boot_line)
{
    if (s_storage_fatfs_mounted == 0U)
    {
        return;
    }

    if ((storage_task_record_directories_prepare() == 0) ||
        ((s_storage_audit_boot_count != 0U) &&
         (storage_task_audit_open(write_boot_line) == 0)))
    {
        storage_task_record_files_close();
        s_storage_fatfs_mounted = 0U;
        s_storage_fatfs_mount_result = FR_DISK_ERR;
        diskio_sdio_set_not_ready();
        s_storage_fatfs_unmount_result = f_mount(NULL, "0:", 0U);
    }
}

/* 格式化审计事件，所有值先写入静态记录缓冲区后再交给 FatFs。 */
static int storage_task_format_audit_event(
    const storage_task_record_request_t *request,
    const board_rtc_time_t *time, uint16_t *length)
{
    char timestamp[24];
    uint16_t position = 0U;
    uint16_t timestamp_length = 0U;
    if ((request == 0) || (time == 0) || (length == 0U)) return 0;
    if (storage_record_format_time(time, timestamp, sizeof(timestamp), &timestamp_length) == 0) return 0;
    if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "[") == 0) ||
        (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, timestamp) == 0) ||
        (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "] ") == 0)) return 0;
    switch ((storage_task_audit_event_t)request->event)
    {
    case STORAGE_TASK_AUDIT_SAMPLE_START: if (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "sampling started") == 0) return 0; break;
    case STORAGE_TASK_AUDIT_SAMPLE_STOP: if (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "sampling stopped") == 0) return 0; break;
    case STORAGE_TASK_AUDIT_RATIO_SET:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "ratio ch") == 0) || (storage_task_text_fixed(s_storage_record_line, sizeof(s_storage_record_line), &position, request->channel, 1U) == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, " set to ") == 0) || (storage_task_text_fixed2(s_storage_record_line, sizeof(s_storage_record_line), &position, request->value0) == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_LIMIT_SET:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "limit ch") == 0) || (storage_task_text_fixed(s_storage_record_line, sizeof(s_storage_record_line), &position, request->channel, 1U) == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, " set to ") == 0) || (storage_task_text_fixed2(s_storage_record_line, sizeof(s_storage_record_line), &position, request->value0) == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_PROTOCOL_SET:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "protocol mode ") == 0) || (storage_task_text_u32(s_storage_record_line, sizeof(s_storage_record_line), &position, request->argument) == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_DEVICE_ID_SET:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "device id set to ") == 0) || (storage_task_text_hex4(s_storage_record_line, sizeof(s_storage_record_line), &position, (uint16_t)request->argument) == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_BAUDRATE_SET:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "baudrate set to ") == 0) || (storage_task_text_u32(s_storage_record_line, sizeof(s_storage_record_line), &position, request->argument) == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_HIDE_ON: if (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "hide mode on") == 0) return 0; break;
    case STORAGE_TASK_AUDIT_HIDE_OFF: if (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "hide mode off") == 0) return 0; break;
    case STORAGE_TASK_AUDIT_SYSTEM_TEST: if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "system test: ") == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, (request->argument != 0U) ? "PASS" : "FAIL") == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_CONFIG_IMPORT: if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "config import: ") == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, (request->argument != 0U) ? "OK" : "FAIL") == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_ALARM_ACTIVE:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "alarm: CH") == 0) || (storage_task_text_fixed(s_storage_record_line, sizeof(s_storage_record_line), &position, request->channel, 1U) == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, " ") == 0) || (storage_task_text_fixed2(s_storage_record_line, sizeof(s_storage_record_line), &position, request->actual) == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, " > ") == 0) || (storage_task_text_fixed2(s_storage_record_line, sizeof(s_storage_record_line), &position, request->threshold) == 0)) return 0; break;
    case STORAGE_TASK_AUDIT_ALARM_RECOVERED:
        if ((storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "alarm recovered: CH") == 0) || (storage_task_text_fixed(s_storage_record_line, sizeof(s_storage_record_line), &position, request->channel, 1U) == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, " ") == 0) || (storage_task_text_fixed2(s_storage_record_line, sizeof(s_storage_record_line), &position, request->actual) == 0) || (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, " < ") == 0) || (storage_task_text_fixed2(s_storage_record_line, sizeof(s_storage_record_line), &position, request->threshold) == 0)) return 0; break;
    default: return 0;
    }
    if (storage_task_text_append(s_storage_record_line, sizeof(s_storage_record_line), &position, "\r\n") == 0) return 0;
    if (storage_task_text_terminate(s_storage_record_line, sizeof(s_storage_record_line), &position) == 0) return 0;
    *length = position;
    (void)timestamp_length;
    return 1;
}

/* 直接在临时缓冲区末尾写入 NUL，供路径和日志交给 FatFs。 */
static int storage_task_text_terminate(char *buffer, uint16_t capacity,
                                       uint16_t *position)
{
    if (*position >= capacity) return 0;
    buffer[*position] = '\0';
    return 1;
}

/* 写入当前 boot 文件的唯一 boot #N 行。 */
static int storage_task_audit_write_boot_line(void)
{
    board_rtc_time_t time;
    char timestamp[24];
    uint16_t position = 0U;
    uint16_t time_length = 0U;

    if ((s_storage_audit_file.open == 0U) ||
        (s_storage_audit_boot_written != 0U))
    {
        return (s_storage_audit_boot_written != 0U) ? 1 : 0;
    }
    if ((board_rtc_time_get(&time) == 0) ||
        (storage_record_format_time(&time, timestamp, sizeof(timestamp),
                                    &time_length) == 0) ||
        (storage_task_text_append(s_storage_record_line,
                                   sizeof(s_storage_record_line), &position,
                                   "[") == 0) ||
        (storage_task_text_append(s_storage_record_line,
                                   sizeof(s_storage_record_line), &position,
                                   timestamp) == 0) ||
        (storage_task_text_append(s_storage_record_line,
                                   sizeof(s_storage_record_line), &position,
                                   "] boot #") == 0) ||
        (storage_task_text_u32(s_storage_record_line,
                               sizeof(s_storage_record_line), &position,
                               s_storage_audit_boot_count) == 0) ||
        (storage_task_text_append(s_storage_record_line,
                                   sizeof(s_storage_record_line), &position,
                                   "\r\n") == 0) ||
        (storage_task_text_terminate(s_storage_record_line,
                                     sizeof(s_storage_record_line), &position) == 0) ||
        (storage_task_record_write_line(&s_storage_audit_file,
                                        s_storage_record_line,
                                        position, 0U) == 0))
    {
        return 0;
    }
    s_storage_audit_boot_written = 1U;
    (void)time_length;
    return 1;
}

/* 打开当前 boot 对应的审计文件，并按需补写 boot #N。 */
static int storage_task_audit_open(uint8_t write_boot_line)
{
    uint16_t position = 0U;
    FRESULT result;
    if (s_storage_fatfs_mounted == 0U || s_storage_audit_boot_count == 0U) return 0;
    if (s_storage_audit_file.open != 0U)
    {
        return (write_boot_line != 0U) ?
               storage_task_audit_write_boot_line() : 1;
    }
    if (storage_task_record_directories_prepare() == 0) return 0;
    (void)memset(s_storage_audit_file.path, 0, sizeof(s_storage_audit_file.path));
    if ((storage_task_text_append(s_storage_audit_file.path, sizeof(s_storage_audit_file.path), &position, "0:/audit/boot_") == 0) ||
        (storage_task_text_fixed(s_storage_audit_file.path, sizeof(s_storage_audit_file.path), &position, s_storage_audit_boot_count, 6U) == 0) ||
        (storage_task_text_append(s_storage_audit_file.path, sizeof(s_storage_audit_file.path), &position, ".log") == 0) ||
        (storage_task_text_terminate(s_storage_audit_file.path, sizeof(s_storage_audit_file.path), &position) == 0)) return 0;
    result = f_open(&s_storage_audit_file.file, s_storage_audit_file.path,
                    FA_OPEN_APPEND | FA_WRITE);
    if (result != FR_OK) return 0;
    s_storage_audit_file.open = 1U;
    s_storage_audit_file.row_count = 0U;
    if ((write_boot_line != 0U) &&
        (storage_task_audit_write_boot_line() == 0))
    {
        storage_task_record_file_close(&s_storage_audit_file);
        return 0;
    }
    return 1;
}

/* 写入一条采样记录；达到 10 条后关闭当前文件，下一条使用新时间文件。 */
static void storage_task_write_sample_record(const storage_task_record_request_t *request)
{
    board_rtc_time_t time;
    uint16_t length = 0U;
    if ((request == 0) || (s_storage_fatfs_mounted == 0U) ||
        (board_rtc_time_get(&time) == 0)) return;
    if (s_storage_sample_file.row_count >= STORAGE_RECORD_MAX_ROWS)
        storage_task_record_file_close(&s_storage_sample_file);
    if ((s_storage_sample_file.open == 0U) &&
        (storage_task_roll_file_open(&s_storage_sample_file, "sample", "sample_", &time) == 0)) return;
    if ((storage_record_format_sample(&time, request->value0, request->value1,
                                     s_storage_record_line,
                                     sizeof(s_storage_record_line), &length) == 0) ||
        (storage_task_record_write_line(&s_storage_sample_file,
                                        s_storage_record_line, length, 1U) == 0)) return;
}

/* 写入一条告警记录；Flash 记录独立于 TF 卡，CSV 作为可选副本。 */
static void storage_task_write_alarm_record(const storage_task_record_request_t *request)
{
    board_rtc_time_t time;
    uint32_t timestamp;
    uint8_t rtc_ok;
    uint16_t length = 0U;

    if (request == 0) return;

    rtc_ok = (uint8_t)((board_rtc_time_get(&time) != 0) ? 1U : 0U);
    timestamp = request->argument;
    if (timestamp == 0U)
    {
        timestamp = (rtc_ok != 0U) ? storage_record_time_to_unix(&time) :
                    (uint32_t)(xTaskGetTickCount() /
                               (TickType_t)configTICK_RATE_HZ);
    }

    /* Flash 是告警记录的主存储，TF 卡不可用时也必须尝试写入。 */
    (void)storage_persistence_alarm_record_append(
        timestamp, request->channel, request->threshold, request->actual);

    if ((s_storage_fatfs_mounted == 0U) || (rtc_ok == 0U)) return;

    if (s_storage_alarm_file.row_count >= STORAGE_RECORD_MAX_ROWS)
        storage_task_record_file_close(&s_storage_alarm_file);
    if ((s_storage_alarm_file.open == 0U) &&
        (storage_task_roll_file_open(&s_storage_alarm_file, "alarm", "alarm_", &time) == 0)) return;
    if ((storage_record_format_alarm(&time, request->channel, request->threshold,
                                     request->actual, s_storage_record_line,
                                     sizeof(s_storage_record_line), &length) == 0) ||
        (storage_task_record_write_line(&s_storage_alarm_file,
                                        s_storage_record_line, length, 1U) == 0)) return;
}

/* 写入一条关键审计事件；审计记录同样使用 f_sync 保证断电可见。 */
static void storage_task_write_audit_record(const storage_task_record_request_t *request)
{
    board_rtc_time_t time;
    uint16_t length = 0U;
    if ((request == 0) || (s_storage_fatfs_mounted == 0U)) return;
    if (s_storage_audit_file.open == 0U && storage_task_audit_open(0U) == 0) return;
    if ((board_rtc_time_get(&time) == 0) ||
        (storage_task_format_audit_event(request, &time, &length) == 0)) return;
    (void)storage_task_record_write_line(&s_storage_audit_file,
                                         s_storage_record_line, length, 0U);
}

/* 从按值队列取出一条业务记录，由 StorageTask 统一执行文件操作。 */
static void storage_task_process_record_request(void)
{
    if ((s_storage_record_queue_handle == 0) ||
        (xQueueReceive(s_storage_record_queue_handle,
                       &s_storage_record_request, 0U) != pdPASS)) return;
    switch ((storage_task_record_type_t)s_storage_record_request.type)
    {
    case STORAGE_TASK_RECORD_SAMPLE:
        storage_task_write_sample_record(&s_storage_record_request);
        break;
    case STORAGE_TASK_RECORD_ALARM:
        storage_task_write_alarm_record(&s_storage_record_request);
        break;
    case STORAGE_TASK_RECORD_AUDIT:
        storage_task_write_audit_record(&s_storage_record_request);
        break;
    default:
        break;
    }
}

static uint8_t storage_task_alarm_status_to_result(
    storage_persistence_status_t status)
{
    switch (status)
    {
    case STORAGE_PERSISTENCE_STATUS_OK:
        return STORAGE_TASK_PERSIST_STATUS_OK;
    case STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT:
        return STORAGE_TASK_PERSIST_STATUS_INVALID_ARGUMENT;
    case STORAGE_PERSISTENCE_STATUS_NOT_FOUND:
        return STORAGE_TASK_PERSIST_STATUS_NOT_FOUND;
    case STORAGE_PERSISTENCE_STATUS_NOT_READY:
        return STORAGE_TASK_PERSIST_STATUS_NOT_READY;
    case STORAGE_PERSISTENCE_STATUS_FLASH_ERROR:
        return STORAGE_TASK_PERSIST_STATUS_FLASH_ERROR;
    case STORAGE_PERSISTENCE_STATUS_DATA_ERROR:
    case STORAGE_PERSISTENCE_STATUS_OUTPUT_TOO_SMALL:
    default:
        return STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;
    }
}

/* Alarm 查询/清除也在 StorageTask 中执行；调用者只拿按值返回的结果。 */
static void storage_task_process_alarm_rpc_request(void)
{
    storage_persistence_status_t status;

    if ((s_storage_alarm_rpc_request_queue_handle == NULL) ||
        (s_storage_alarm_rpc_result_queue_handle == NULL) ||
        (xQueueReceive(s_storage_alarm_rpc_request_queue_handle,
                       &s_storage_alarm_rpc_request, 0U) != pdPASS))
    {
        return;
    }

    (void)memset(&s_storage_alarm_rpc_result, 0,
                 sizeof(s_storage_alarm_rpc_result));
    s_storage_alarm_rpc_result.request_id =
        s_storage_alarm_rpc_request.request_id;
    s_storage_alarm_rpc_result.operation =
        s_storage_alarm_rpc_request.operation;

    switch ((storage_task_alarm_rpc_operation_t)
            s_storage_alarm_rpc_request.operation)
    {
    case STORAGE_TASK_ALARM_RPC_QUERY:
        status = storage_persistence_alarm_records_read(
            s_storage_alarm_rpc_result.payload,
            sizeof(s_storage_alarm_rpc_result.payload),
            &s_storage_alarm_rpc_result.payload_length);
        break;
    case STORAGE_TASK_ALARM_RPC_CLEAR:
        status = storage_persistence_alarm_records_clear();
        break;
    default:
        status = STORAGE_PERSISTENCE_STATUS_INVALID_ARGUMENT;
        break;
    }

    s_storage_alarm_rpc_result.status =
        storage_task_alarm_status_to_result(status);
    (void)xQueueSend(s_storage_alarm_rpc_result_queue_handle,
                     &s_storage_alarm_rpc_result, 0U);
}

static int storage_task_alarm_rpc_call(uint8_t operation,
                                       uint8_t *payload,
                                       uint16_t capacity,
                                       uint16_t *length)
{
    storage_task_alarm_rpc_request_t request;
    TickType_t deadline;
    TickType_t now;
    TickType_t remaining;

    if ((s_storage_alarm_rpc_request_queue_handle == NULL) ||
        (s_storage_alarm_rpc_result_queue_handle == NULL) ||
        (length == NULL) ||
        ((payload == NULL) && (capacity != 0U)))
    {
        return 0;
    }

    (void)memset(&request, 0, sizeof(request));
    request.request_id = ++s_storage_alarm_rpc_next_request_id;
    request.operation = operation;

    if (xQueueSend(s_storage_alarm_rpc_request_queue_handle,
                   &request, 0U) != pdPASS)
    {
        return 0;
    }

    deadline = xTaskGetTickCount() +
               pdMS_TO_TICKS(STORAGE_TASK_ALARM_RPC_TIMEOUT_MS);

    for (;;)
    {
        now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0)
        {
            return 0;
        }

        remaining = deadline - now;
        if (xQueueReceive(s_storage_alarm_rpc_result_queue_handle,
                          &s_storage_alarm_rpc_wait_result,
                          remaining) != pdPASS)
        {
            return 0;
        }

        if ((s_storage_alarm_rpc_wait_result.request_id !=
             request.request_id) ||
            (s_storage_alarm_rpc_wait_result.operation != operation))
        {
            continue;
        }

        if ((s_storage_alarm_rpc_wait_result.status !=
             STORAGE_TASK_PERSIST_STATUS_OK) ||
            (s_storage_alarm_rpc_wait_result.payload_length > capacity))
        {
            return 0;
        }

        if (s_storage_alarm_rpc_wait_result.payload_length > 0U)
        {
            (void)memcpy(payload,
                         s_storage_alarm_rpc_wait_result.payload,
                         s_storage_alarm_rpc_wait_result.payload_length);
        }
        *length = s_storage_alarm_rpc_wait_result.payload_length;
        return 1;
    }
}

static void storage_task_process_file_request(void)
{
    storage_task_file_request_t request;
    FIL file;
    FRESULT result;
    FRESULT close_result;
    UINT transferred;
    BYTE open_mode;

    if (s_storage_file_request_queue_handle == NULL)
    {
        return;
    }
    if (xQueueReceive(s_storage_file_request_queue_handle, &request, 0U) != pdPASS)
    {
        return;
    }

    result = FR_INVALID_PARAMETER;
    close_result = FR_OK;
    transferred = 0U;

    if (s_storage_fatfs_mounted == 0U)
    {
        result = FR_NOT_READY;
        goto send_result;
    }
    switch (request.operation)
    {
    case STORAGE_TASK_FILE_WRITE:
    case STORAGE_TASK_FILE_APPEND:
        if (request.operation == STORAGE_TASK_FILE_APPEND)
        {
            open_mode = FA_OPEN_APPEND | FA_WRITE;
        }
        else
        {
            open_mode = FA_CREATE_ALWAYS | FA_WRITE;
        }

        result = f_open(&file, request.path, open_mode);

        if (result != FR_OK)
        {
            goto send_result;
        }

        result = f_write(&file, request.buffer, (UINT)request.length, &transferred);

        if ((result == FR_OK) && (transferred != (UINT)request.length))
        {
            result = FR_DISK_ERR;
        }

        if (result == FR_OK)
        {
            result = f_sync(&file);
        }

        close_result = f_close(&file);

        if (result == FR_OK)
        {
            result = close_result;
        }

        break;

    case STORAGE_TASK_FILE_READ:
        result = f_open(&file, request.path, FA_READ);

        if (result != FR_OK)
        {
            goto send_result;
        }

        result = f_read(&file, request.buffer, (UINT)request.length, &transferred);

        close_result = f_close(&file);

        if (result == FR_OK)
        {
            result = close_result;
        }

        break;

    default:
        result = FR_INVALID_PARAMETER;
        break;
    }
    
send_result:
    storage_task_file_result_send(request.request_id, result, transferred);
}

static void storage_task_process_persist_request(void)
{
    FIL file;
    UINT transferred;
    FRESULT file_status;
    app_config_t base;
    app_config_t candidate;
    uint16_t encoded_length;
    uint16_t error_line;

    /* 将导入阶段和错误明细放入结果 payload，供 CLI 显示具体原因。 */
    #define STORAGE_TASK_SET_IMPORT_ERROR(code, detail) do { \
        s_storage_persist_result.payload_length = 3U; \
        s_storage_persist_result.payload[0] = (code); \
        s_storage_persist_result.payload[1] = (uint8_t)((detail) & 0xFFU); \
        s_storage_persist_result.payload[2] = (uint8_t)(((detail) >> 8U) & 0xFFU); \
    } while (0)

    if (storage_persist_request_receive(&s_storage_persist_request, 0U) != pdPASS)
    {
        return;
    }

    if (s_storage_persist_request.operation == STORAGE_TASK_PERSIST_CONFIG_IMPORT)
    {
        (void)memset(&s_storage_persist_result, 0, sizeof(s_storage_persist_result));
        s_storage_persist_result.request_id = s_storage_persist_request.request_id;
        s_storage_persist_result.operation = s_storage_persist_request.operation;
        if (s_storage_fatfs_mounted == 0U)
        {
            s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_NOT_READY;
            (void)storage_persist_result_send(&s_storage_persist_result);
            return;
        }
        file_status = f_open(&file, "0:/config/config.ini", FA_READ);
        if (file_status == FR_NO_FILE)
        {
            s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_NOT_FOUND;
            (void)storage_persist_result_send(&s_storage_persist_result);
            return;
        }
        if (file_status != FR_OK)
        {
            STORAGE_TASK_SET_IMPORT_ERROR(STORAGE_TASK_CONFIG_IMPORT_ERROR_OPEN,
                                          (uint16_t)file_status);
            s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;
            (void)storage_persist_result_send(&s_storage_persist_result);
            return;
        }
        file_status = f_read(&file, s_storage_config_file,
                             sizeof(s_storage_config_file), &transferred);
        {
            FRESULT close_status = f_close(&file);
            if ((file_status != FR_OK) || (close_status != FR_OK))
            {
                STORAGE_TASK_SET_IMPORT_ERROR(
                    STORAGE_TASK_CONFIG_IMPORT_ERROR_READ,
                    (uint16_t)((file_status != FR_OK) ? file_status : close_status));
                s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;
                (void)storage_persist_result_send(&s_storage_persist_result);
                return;
            }
        }
        if (transferred > APP_CONFIG_INI_FILE_MAX)
        {
            STORAGE_TASK_SET_IMPORT_ERROR(STORAGE_TASK_CONFIG_IMPORT_ERROR_SIZE,
                                          (uint16_t)transferred);
            s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;
            (void)storage_persist_result_send(&s_storage_persist_result);
            return;
        }
        (void)app_config_get(&base);
        encoded_length = 0U;
        error_line = 0U;
        if (app_config_import_prepare((const char *)s_storage_config_file,
                                      (uint16_t)transferred, &base, &candidate,
                                      s_storage_config_encoded,
                                      sizeof(s_storage_config_encoded),
                                      &encoded_length, &error_line) == 0)
        {
            STORAGE_TASK_SET_IMPORT_ERROR(
                (error_line != 0U) ? STORAGE_TASK_CONFIG_IMPORT_ERROR_PARSE :
                                    STORAGE_TASK_CONFIG_IMPORT_ERROR_SERVICE,
                error_line);
            s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_DATA_ERROR;
            (void)storage_persist_result_send(&s_storage_persist_result);
            return;
        }
        if (storage_persistence_config_save(s_storage_config_encoded,
                                             encoded_length) != STORAGE_PERSISTENCE_STATUS_OK)
        {
            s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_FLASH_ERROR;
            (void)storage_persist_result_send(&s_storage_persist_result);
            return;
        }
        s_storage_persist_result.payload_length = encoded_length;
        (void)memcpy(s_storage_persist_result.payload,
                     s_storage_config_encoded, encoded_length);
        s_storage_persist_result.status = STORAGE_TASK_PERSIST_STATUS_OK;
        (void)storage_persist_result_send(&s_storage_persist_result);
        return;
    }
    #undef STORAGE_TASK_SET_IMPORT_ERROR
    if (storage_persistence_request_handle(&s_storage_persist_request,
                                           &s_storage_persist_result) == 0)
    {
        return;
    }

    (void)storage_persist_result_send(&s_storage_persist_result);
}

static void storage_task_publish_config_load(void)
{
    static storage_task_persist_request_t request;
    static storage_task_persist_result_t result;

    (void)memset(&request, 0, sizeof(request));
    request.request_id = 0U;
    request.operation = STORAGE_TASK_PERSIST_CONFIG_LOAD;
    request.origin = STORAGE_TASK_PERSIST_ORIGIN_BOOT;

    if (storage_persistence_request_handle(&request, &result) != 0)
    {
        (void)storage_persist_result_send(&result);
    }
}

static void storage_task(void *argument)
{
    uint32_t notification_value;

    (void)argument;

    if (storage_persistence_init() == STORAGE_PERSISTENCE_STATUS_OK)
    {
        (void)storage_persistence_boot_count_next(&s_storage_audit_boot_count);
    }
    storage_task_publish_config_load();
    storage_sdio_initialize();
    storage_fatfs_mount();
    storage_task_record_setup_after_mount(1U);
    s_storage_card_insert_attempted =
        (board_sdio_card_present() != 0U) ? 1U : 0U;
    if (storage_mount_policy_retry(
            (board_sdio_card_present() != 0U) ? 1U : 0U,
            s_storage_fatfs_mounted,
            s_storage_card_insert_attempted) != 0U)
    {
        s_storage_card_insert_attempted = 0U;
    }

    for (;;)
    {
        notification_value = 0U;

        (void)xTaskNotifyWait(
            0U,
            0xFFFFFFFFUL,
            &notification_value,
            pdMS_TO_TICKS(10U));

        if (notification_value != 0U)
        {
            s_storage_task_sdio_diag.notification_events |=
                notification_value;
        }

        storage_card_remove_process();
        storage_card_insert_process();

        storage_task_process_request();
        storage_task_process_file_request();
        storage_task_process_persist_request();
        storage_task_process_record_request();
        storage_task_process_alarm_rpc_request();

        s_storage_task_sdio_diag.dma_irq_events =
            s_storage_task_dma_irq_events;
        s_storage_task_sdio_diag.sdio_irq_events =
            s_storage_task_sdio_irq_events;
        s_storage_task_sdio_diag.dma_irq_count =
            s_storage_task_dma_irq_count;
        s_storage_task_sdio_diag.sdio_irq_count =
            s_storage_task_sdio_irq_count;

        s_storage_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_storage_task_heartbeat++;
    }
}

int storage_task_create(void)
{
    s_storage_task_sdio_diag.state = STORAGE_TASK_SDIO_STATE_NOT_STARTED;
    s_storage_audit_boot_written = 0U;

    s_storage_request_queue_handle = xQueueCreateStatic(
        STORAGE_TASK_REQUEST_QUEUE_LENGTH,
        sizeof(storage_task_request_t),
        s_storage_request_queue_storage,
        &s_storage_request_queue
    );

    if (s_storage_request_queue_handle  == NULL)
    {
        return 0;
    }
    
    s_storage_result_queue_handle  = xQueueCreateStatic(
        STORAGE_TASK_RESULT_QUEUE_LENGTH,
        sizeof(storage_task_request_result_t),
        s_storage_result_queue_storage,
        &s_storage_result_queue
    );

    if (s_storage_result_queue_handle == NULL)
    {
        return 0;
    }
    
    s_storage_file_request_queue_handle = xQueueCreateStatic(
        STORAGE_TASK_REQUEST_QUEUE_LENGTH,
        sizeof(storage_task_file_request_t),
        s_storage_file_request_queue_storage,
        &s_storage_file_request_queue
    );

    if (s_storage_file_request_queue_handle == NULL)
    {
        return 0;
    }

    s_storage_file_result_queue_handle = xQueueCreateStatic(
        STORAGE_TASK_RESULT_QUEUE_LENGTH,
        sizeof(storage_task_file_result_t),
        s_storage_file_result_queue_storage,
        &s_storage_file_result_queue
    );

    if (s_storage_file_result_queue_handle == NULL)
    {
        return 0;
    }

    s_storage_record_queue_handle = xQueueCreateStatic(
        STORAGE_TASK_RECORD_QUEUE_LENGTH,
        sizeof(storage_task_record_request_t),
        s_storage_record_queue_storage,
        &s_storage_record_queue);

    if (s_storage_record_queue_handle == NULL)
    {
        return 0;
    }

    s_storage_alarm_rpc_request_queue_handle = xQueueCreateStatic(
        STORAGE_TASK_ALARM_RPC_QUEUE_LENGTH,
        sizeof(storage_task_alarm_rpc_request_t),
        s_storage_alarm_rpc_request_queue_storage,
        &s_storage_alarm_rpc_request_queue);

    if (s_storage_alarm_rpc_request_queue_handle == NULL)
    {
        return 0;
    }

    s_storage_alarm_rpc_result_queue_handle = xQueueCreateStatic(
        STORAGE_TASK_ALARM_RPC_QUEUE_LENGTH,
        sizeof(storage_task_alarm_rpc_result_t),
        s_storage_alarm_rpc_result_queue_storage,
        &s_storage_alarm_rpc_result_queue);

    if (s_storage_alarm_rpc_result_queue_handle == NULL)
    {
        return 0;
    }

    if (storage_persist_queue_init() == 0)
    {
        return 0;
    }

    s_storage_task_handle = xTaskCreateStatic(
        storage_task,
        "Storage",
        STORAGE_TASK_STACK_DEPTH,
        NULL,
        STORAGE_TASK_PRIORITY,
        s_storage_task_stack,
        &s_storage_task_tcb);

    if (s_storage_task_handle == NULL)
    {
        return 0;
    }

    board_sdio_irq_callback_register(storage_task_sdio_irq_callback);

    return 1;
}

int storage_task_file_request_submit(const storage_task_file_request_t *request)
{
    if (request == NULL)
    {
        return 0;
    }
    
    if (s_storage_file_request_queue_handle == NULL)
    {
        return 0;
    }
    
    if ((request->operation != STORAGE_TASK_FILE_WRITE) &&
        (request->operation != STORAGE_TASK_FILE_READ)  &&
        (request->operation != STORAGE_TASK_FILE_APPEND))
    {
        return 0;
    }
    
    if (!storage_task_file_path_valid(request->path))
    {
        return 0;
    }
    
    if ((request->buffer == NULL) || (request->length == 0U))
    {
        return 0;
    }
    
    if (xQueueSend(s_storage_file_request_queue_handle, request, 0U) != pdPASS)
    {
        return 0;
    }
    return 1;
}

int storage_task_file_result_get(storage_task_file_result_t *result, uint32_t timeout_ms)
{
    TickType_t wait_ticks;

    if (result == NULL)
    {
        return 0;
    }

    if (s_storage_file_result_queue_handle == NULL)
    {
        return 0;
    }
    
    wait_ticks = pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(s_storage_file_result_queue_handle, result, wait_ticks) != pdPASS)
    {
        return 0;
    }
    return 1;    
}

/* 提交一条采样值拷贝；调用者不向 StorageTask 传递可复用缓冲区指针。 */
int storage_task_sample_record_submit(float ch0, float ch1)
{
    storage_task_record_request_t request;
    if (s_storage_record_queue_handle == 0) return 0;
    (void)memset(&request, 0, sizeof(request));
    request.type = STORAGE_TASK_RECORD_SAMPLE;
    request.value0 = ch0;
    request.value1 = ch1;
    return (xQueueSend(s_storage_record_queue_handle, &request, 0U) == pdPASS) ? 1 : 0;
}

/* 提交一条告警值拷贝；AlarmTask 只负责产生事件，存储操作留在 StorageTask。 */
int storage_task_alarm_record_submit(uint8_t channel, float threshold,
                                     float actual, uint32_t timestamp)
{
    storage_task_record_request_t request;
    if ((s_storage_record_queue_handle == 0) || (channel >= 2U)) return 0;
    (void)memset(&request, 0, sizeof(request));
    request.type = STORAGE_TASK_RECORD_ALARM;
    request.channel = channel;
    request.threshold = threshold;
    request.actual = actual;
    request.argument = timestamp;
    return (xQueueSend(s_storage_record_queue_handle, &request, 0U) == pdPASS) ? 1 : 0;
}

int storage_task_alarm_records_get(uint8_t *payload, uint16_t capacity,
                                   uint16_t *length)
{
    return storage_task_alarm_rpc_call(STORAGE_TASK_ALARM_RPC_QUERY,
                                       payload, capacity, length);
}

int storage_task_alarm_records_clear(void)
{
    uint16_t length = 0U;

    return storage_task_alarm_rpc_call(STORAGE_TASK_ALARM_RPC_CLEAR,
                                       NULL, 0U, &length);
}

/* 提交关键审计事件，参数按值复制进静态队列，避免跨任务裸指针。 */
int storage_task_audit_event_submit(uint8_t event, uint8_t channel,
                                    float value0, float value1,
                                    uint32_t argument)
{
    storage_task_record_request_t request;
    if ((s_storage_record_queue_handle == 0) ||
        (event > STORAGE_TASK_AUDIT_ALARM_RECOVERED) || (channel >= 2U &&
         ((event == STORAGE_TASK_AUDIT_RATIO_SET) ||
          (event == STORAGE_TASK_AUDIT_LIMIT_SET) ||
          (event == STORAGE_TASK_AUDIT_ALARM_ACTIVE) ||
          (event == STORAGE_TASK_AUDIT_ALARM_RECOVERED)))) return 0;
    (void)memset(&request, 0, sizeof(request));
    request.type = STORAGE_TASK_RECORD_AUDIT;
    request.event = event;
    request.channel = channel;
    request.value0 = value0;
    request.value1 = value1;
    request.actual = value0;
    request.threshold = value1;
    request.argument = argument;
    return (xQueueSend(s_storage_record_queue_handle, &request, 0U) == pdPASS) ? 1 : 0;
}

int storage_task_persist_request_submit(
    const storage_task_persist_request_t *request)
{
    if (request == NULL)
    {
        return 0;
    }

    if (request->operation > STORAGE_TASK_PERSIST_CONFIG_IMPORT)
    {
        return 0;
    }

    if (request->payload_length > STORAGE_TASK_PERSIST_PAYLOAD_MAX)
    {
        return 0;
    }

    if (storage_persist_request_send(request) != pdTRUE)
    {
        return 0;
    }

    return 1;
}

/* 构造并提交 config.ini 导入请求，避免上层复制操作码和队列约束。 */
int storage_task_config_import_submit(uint32_t request_id,
                                      uint32_t deadline_tick,
                                      uint8_t origin)
{
    storage_task_persist_request_t request;
    (void)memset(&request, 0, sizeof(request));
    request.request_id = request_id;
    request.deadline_tick = deadline_tick;
    request.operation = STORAGE_TASK_PERSIST_CONFIG_IMPORT;
    request.origin = origin;
    return storage_task_persist_request_submit(&request);
}

int storage_task_persist_result_get(
    storage_task_persist_result_t *result,
    uint32_t timeout_ms)
{
    TickType_t wait_ticks;

    if (result == NULL)
    {
        return 0;
    }

    wait_ticks = pdMS_TO_TICKS(timeout_ms);

    if (storage_persist_result_receive(result, wait_ticks) != pdTRUE)
    {
        return 0;
    }

    return 1;
}

int storage_task_request_submit(const storage_task_request_t *request)
{
    if (request == NULL)
    {
        return 0;
    }

    if (s_storage_request_queue_handle == NULL)
    {
        return 0;
    }
    
    if ((request->operation != STORAGE_TASK_REQUEST_READ_BLOCK) && (request->operation != STORAGE_TASK_REQUEST_WRITE_BLOCK))
    {
        return 0;
    }
    
    if ((request->length != BOARD_SDIO_BLOCK_SIZE))
    {
        return 0;
    }
    
    if (request->buffer == NULL)
    {
        return 0;
    }
    
    if ((((uint32_t)request->buffer) & 0x03U) != 0U)
    {
        return 0;
    }
    
    if (xQueueSend(s_storage_request_queue_handle, request, 0U) != pdPASS)
    {
        return 0;
    }
    
    return 1;
}

int storage_task_request_result_get(storage_task_request_result_t *result, uint32_t timeout_ms)
{
    TickType_t wait_ticks;

    if (result == NULL)
    {
        return 0;
    }
    
    if (s_storage_result_queue_handle == NULL)
    {
        return 0;
    }
    
    wait_ticks = pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(s_storage_result_queue_handle, result, wait_ticks) != pdPASS)
    {
        return 0;
    }

    return 1;
}

uint32_t storage_task_get_heartbeat(void)
{
    return s_storage_task_heartbeat;
}

uint32_t storage_task_get_stack_high_water_mark(void)
{
    return s_storage_task_stack_high_water_mark;
}

int storage_task_sdio_diag_get(storage_task_sdio_diag_t *diag)
{
    if (diag == NULL)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    *diag = s_storage_task_sdio_diag;

    diag->notification_events =
        s_storage_task_sdio_diag.notification_events;
    diag->dma_irq_events = s_storage_task_dma_irq_events;
    diag->sdio_irq_events = s_storage_task_sdio_irq_events;
    diag->dma_irq_count = s_storage_task_dma_irq_count;
    diag->sdio_irq_count = s_storage_task_sdio_irq_count;

    taskEXIT_CRITICAL();

    return 1;
}

/* 返回 FatFs 是否已经成功挂载，区别于仅表示 SDIO 就绪的卡状态。 */
uint8_t storage_task_fatfs_mounted_get(void)
{
    return (s_storage_fatfs_mounted != 0U) ? 1U : 0U;
}

/* 返回最近一次 FatFs 挂载结果，供系统自检定位底层状态。 */
uint32_t storage_task_fatfs_mount_result_get(void)
{
    return (uint32_t)s_storage_fatfs_mount_result;
}
