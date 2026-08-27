#include "task_queues.h"

#define PROTOCOL_REQUEST_QUEUE_LENGTH       8U
#define PROTOCOL_REQUEST_ITEM_SIZE          sizeof(protocol_request_t)

static StaticQueue_t s_protocol_request_queue;
static uint8_t s_protocol_request_queue_storage[PROTOCOL_REQUEST_QUEUE_LENGTH * PROTOCOL_REQUEST_ITEM_SIZE];

static QueueHandle_t s_protocol_request_queue_handle;

int task_queues_init(void)
{
    s_protocol_request_queue_handle = xQueueCreateStatic(
        PROTOCOL_REQUEST_QUEUE_LENGTH,
        PROTOCOL_REQUEST_ITEM_SIZE,
        s_protocol_request_queue_storage,
        &s_protocol_request_queue
    );

    if (s_protocol_request_queue_handle == NULL)
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
