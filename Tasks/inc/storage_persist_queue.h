#ifndef STORAGE_PERSIST_QUEUE_H
#define STORAGE_PERSIST_QUEUE_H

#include "FreeRTOS.h"
#include "queue.h"

#include "storage_task.h"

#define STORAGE_PERSIST_QUEUE_LENGTH 8U

int storage_persist_queue_init(void);

BaseType_t storage_persist_request_send(
    const storage_task_persist_request_t *request);
BaseType_t storage_persist_request_receive(
    storage_task_persist_request_t *request,
    TickType_t wait_ticks);

BaseType_t storage_persist_result_send(
    const storage_task_persist_result_t *result);
BaseType_t storage_persist_result_receive(
    storage_task_persist_result_t *result,
    TickType_t wait_ticks);

#endif /* STORAGE_PERSIST_QUEUE_H */
