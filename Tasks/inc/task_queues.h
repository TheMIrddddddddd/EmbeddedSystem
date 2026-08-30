#ifndef TASK_QUEUES_H
#define TASK_QUEUES_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "ebtn.h"

typedef struct 
{
    uint16_t key_id;
    uint8_t event;
    uint8_t reserved;
    uint32_t timestamp_ms;
} key_event_t;

typedef struct
{
    uint32_t request_id;
    uint8_t origin;
    uint8_t operation;
    uint16_t protocol_sequence;
    TickType_t  deadline_tick;
    uint8_t payload[12];
} protocol_request_t;

int task_queues_init(void);

BaseType_t protocol_request_send(const protocol_request_t *request);
BaseType_t protocol_request_receive(protocol_request_t *request, TickType_t wait_ticks);
BaseType_t key_event_send(const key_event_t *event);
BaseType_t key_event_receive(key_event_t *event, TickType_t wait_ticks);

#endif /* TASK_QUEUES_H */
