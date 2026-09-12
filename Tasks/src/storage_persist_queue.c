#include "storage_persist_queue.h"

#define STORAGE_PERSIST_REQUEST_ITEM_SIZE sizeof(storage_task_persist_request_t)
#define STORAGE_PERSIST_RESULT_ITEM_SIZE  sizeof(storage_task_persist_result_t)

typedef union
{
    uint8_t bytes[STORAGE_PERSIST_QUEUE_LENGTH *
                  STORAGE_PERSIST_REQUEST_ITEM_SIZE];
    uint64_t alignment;
} storage_persist_request_storage_t;

typedef union
{
    uint8_t bytes[STORAGE_PERSIST_QUEUE_LENGTH *
                  STORAGE_PERSIST_RESULT_ITEM_SIZE];
    uint64_t alignment;
} storage_persist_result_storage_t;

static StaticQueue_t s_storage_persist_request_queue;
static StaticQueue_t s_storage_persist_result_queue;
static storage_persist_request_storage_t s_storage_persist_request_storage;
static storage_persist_result_storage_t s_storage_persist_result_storage;
static QueueHandle_t s_storage_persist_request_queue_handle;
static QueueHandle_t s_storage_persist_result_queue_handle;

int storage_persist_queue_init(void)
{
    if ((s_storage_persist_request_queue_handle != NULL) &&
        (s_storage_persist_result_queue_handle != NULL))
    {
        return 1;
    }

    s_storage_persist_request_queue_handle = xQueueCreateStatic(
        STORAGE_PERSIST_QUEUE_LENGTH,
        STORAGE_PERSIST_REQUEST_ITEM_SIZE,
        s_storage_persist_request_storage.bytes,
        &s_storage_persist_request_queue);

    if (s_storage_persist_request_queue_handle == NULL)
    {
        return 0;
    }

    s_storage_persist_result_queue_handle = xQueueCreateStatic(
        STORAGE_PERSIST_QUEUE_LENGTH,
        STORAGE_PERSIST_RESULT_ITEM_SIZE,
        s_storage_persist_result_storage.bytes,
        &s_storage_persist_result_queue);

    if (s_storage_persist_result_queue_handle == NULL)
    {
        return 0;
    }

    return 1;
}

BaseType_t storage_persist_request_send(
    const storage_task_persist_request_t *request)
{
    if ((request == NULL) ||
        (s_storage_persist_request_queue_handle == NULL))
    {
        return errQUEUE_FULL;
    }

    return xQueueSend(s_storage_persist_request_queue_handle, request, 0U);
}

BaseType_t storage_persist_request_receive(
    storage_task_persist_request_t *request,
    TickType_t wait_ticks)
{
    if ((request == NULL) ||
        (s_storage_persist_request_queue_handle == NULL))
    {
        return pdFALSE;
    }

    return xQueueReceive(s_storage_persist_request_queue_handle,
                         request,
                         wait_ticks);
}

BaseType_t storage_persist_result_send(
    const storage_task_persist_result_t *result)
{
    if ((result == NULL) ||
        (s_storage_persist_result_queue_handle == NULL))
    {
        return errQUEUE_FULL;
    }

    return xQueueSend(s_storage_persist_result_queue_handle, result, 0U);
}

BaseType_t storage_persist_result_receive(
    storage_task_persist_result_t *result,
    TickType_t wait_ticks)
{
    if ((result == NULL) ||
        (s_storage_persist_result_queue_handle == NULL))
    {
        return pdFALSE;
    }

    return xQueueReceive(s_storage_persist_result_queue_handle,
                         result,
                         wait_ticks);
}
