#ifndef TEST_TASK_QUEUE_QUEUE_H
#define TEST_TASK_QUEUE_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

typedef struct test_queue test_queue_t;
typedef test_queue_t *QueueHandle_t;
typedef struct
{
    uint8_t reserved[4];
} StaticQueue_t;

QueueHandle_t xQueueCreateStatic(size_t length,
                                 size_t item_size,
                                 uint8_t *storage,
                                 StaticQueue_t *queue);
BaseType_t xQueueSend(QueueHandle_t queue,
                      const void *item,
                      TickType_t wait_ticks);
BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *item,
                         TickType_t wait_ticks);
BaseType_t xQueueReset(QueueHandle_t queue);

#endif
