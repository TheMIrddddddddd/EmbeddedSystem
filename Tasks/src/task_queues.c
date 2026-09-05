#include "task_queues.h"

#define PROTOCOL_REQUEST_QUEUE_LENGTH       8U
#define PROTOCOL_REQUEST_ITEM_SIZE          sizeof(protocol_request_t)
#define KEY_EVENT_QUEUE_LENGTH              8U
#define KEY_EVENT_ITEM_SIZE                 sizeof(key_event_t)

static StaticQueue_t s_protocol_request_queue;
static uint8_t s_protocol_request_queue_storage[PROTOCOL_REQUEST_QUEUE_LENGTH * PROTOCOL_REQUEST_ITEM_SIZE];
static StaticQueue_t s_key_event_queue;
static uint8_t s_key_event_queue_storage[KEY_EVENT_QUEUE_LENGTH * KEY_EVENT_ITEM_SIZE];

static QueueHandle_t s_protocol_request_queue_handle;
static QueueHandle_t s_key_event_queue_handle;

int task_queues_init(void)
{
    s_protocol_request_queue_handle = xQueueCreateStatic(
        PROTOCOL_REQUEST_QUEUE_LENGTH,
        PROTOCOL_REQUEST_ITEM_SIZE,
        s_protocol_request_queue_storage,
        &s_protocol_request_queue
    );

    s_key_event_queue_handle = xQueueCreateStatic(
        KEY_EVENT_QUEUE_LENGTH,
        KEY_EVENT_ITEM_SIZE,
        s_key_event_queue_storage,
        &s_key_event_queue
    );

    if (s_protocol_request_queue_handle == NULL)
    {
        return 0;
    }

    if (s_key_event_queue_handle == NULL)
    {
        return 0;
    }

    return 1;
}

BaseType_t protocol_request_send(const protocol_request_t *request)
{
    if (request == NULL)
    {
        return errQUEUE_FULL;
    }

    return xQueueSend(
        s_protocol_request_queue_handle,
        request,
        0U
    );
}

BaseType_t protocol_request_receive(protocol_request_t *request, TickType_t wait_ticks)
{
    if (request == NULL)
    {
        return pdFALSE;
    }

    return xQueueReceive(
        s_protocol_request_queue_handle,
        request,
        wait_ticks
    );
}

BaseType_t key_event_send(const key_event_t *event)
{
    if (event == NULL)
    {
        return errQUEUE_FULL;
    }

    return xQueueSend(s_key_event_queue_handle, event, 0U);
}

BaseType_t key_event_receive(key_event_t *event, TickType_t wait_ticks)
{
    if (event == NULL)
    {
        return pdFALSE;
    }

    return xQueueReceive(
        s_key_event_queue_handle,
        event,
        wait_ticks
    );
    
}
