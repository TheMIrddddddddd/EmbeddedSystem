#include "storage_task.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "board_sdio.h"

#include "diskio.h"
#include "ff.h"

#define STORAGE_TASK_PRIORITY              3U
#define STORAGE_TASK_STACK_DEPTH           512U

#define STORAGE_SDIO_NOTIFY_DMA_DONE       (1UL << 0)
#define STORAGE_SDIO_NOTIFY_DTEND          (1UL << 1)
#define STORAGE_SDIO_NOTIFY_DTBLKEND       (1UL << 2)
#define STORAGE_SDIO_NOTIFY_DMA_FIFO       (1UL << 3)
#define STORAGE_SDIO_NOTIFY_DMA_ERROR      (1UL << 4)
#define STORAGE_SDIO_NOTIFY_SDIO_ERROR     (1UL << 5)

#define STORAGE_TASK_REQUEST_QUEUE_LENGTH         4U
#define STORAGE_TASK_RESULT_QUEUE_LENGTH         4U

static StaticTask_t s_storage_task_tcb;
static StackType_t  s_storage_task_stack[STORAGE_TASK_STACK_DEPTH];

static volatile uint32_t s_storage_task_heartbeat;
static volatile uint32_t s_storage_task_stack_high_water_mark;
static TaskHandle_t s_storage_task_handle;
static StaticQueue_t s_storage_request_queue;
static StaticQueue_t s_storage_result_queue;

static StaticQueue_t s_storage_file_request_queue;
static StaticQueue_t s_storage_file_result_queue;

__align(8)
static uint8_t s_storage_request_queue_storage[STORAGE_TASK_REQUEST_QUEUE_LENGTH * sizeof(storage_task_request_t)];

__align(8)
static uint8_t s_storage_result_queue_storage[STORAGE_TASK_RESULT_QUEUE_LENGTH * sizeof(storage_task_request_result_t)];

__align(8)
static uint8_t s_storage_file_request_queue_storage[STORAGE_TASK_REQUEST_QUEUE_LENGTH  * sizeof(storage_task_file_request_t)];

__align(8)
static uint8_t s_storage_file_result_queue_storage[STORAGE_TASK_RESULT_QUEUE_LENGTH * sizeof(storage_task_file_result_t)];

static QueueHandle_t s_storage_request_queue_handle;
static QueueHandle_t s_storage_result_queue_handle;
static QueueHandle_t s_storage_file_request_queue_handle;
static QueueHandle_t s_storage_file_result_queue_handle;
static FATFS s_storage_fatfs;
static volatile FRESULT s_storage_fatfs_mount_result = FR_NOT_READY;
static volatile uint8_t s_storage_fatfs_mounted;
static storage_task_sdio_diag_t s_storage_task_sdio_diag;

static volatile uint32_t s_storage_task_dma_irq_events;
static volatile uint32_t s_storage_task_sdio_irq_events;
static volatile uint32_t s_storage_task_dma_irq_count;
static volatile uint32_t s_storage_task_sdio_irq_count;

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
    s_storage_fatfs_mounted = 0U;
    s_storage_fatfs_mount_result = FR_NOT_READY;

    if (s_storage_task_sdio_diag.state != STORAGE_TASK_SDIO_STATE_READY)
    {
        return;
    }
    
    s_storage_fatfs_mount_result = f_mount(&s_storage_fatfs, "0:", 1U);

    if (s_storage_fatfs_mount_result == FR_OK)
    {
        s_storage_fatfs_mounted = 1U;
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
        status = board_sdio_read_block(request.block_number, request.buffer);
        s_storage_task_sdio_diag.read_status =
            (uint32_t)status;
        break;
    
    case STORAGE_TASK_REQUEST_WRITE_BLOCK:
        status = board_sdio_write_block(request.block_number, request.buffer);
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

static void storage_task_process_file_request(void)
{
    storage_task_file_request_t request;
    FIL file;
    FRESULT result;
    FRESULT close_result;
    UINT transferred;

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
        result = f_open(&file, request.path, FA_CREATE_ALWAYS | FA_WRITE);

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

static void storage_task(void *argument)
{
    uint32_t notification_value;

    (void)argument;

    storage_sdio_initialize();
    storage_fatfs_mount();

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

        storage_task_process_request();
        storage_task_process_file_request();

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
    
    if ((request->operation != STORAGE_TASK_FILE_WRITE) && (request->operation != STORAGE_TASK_FILE_READ))
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
