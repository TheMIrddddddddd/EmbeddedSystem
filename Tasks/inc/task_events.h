#ifndef TASK_EVENTS_H
#define TASK_EVENTS_H

#include "FreeRTOS.h"
#include "event_groups.h"

#define TASK_EVENT_SYSTEM_READY         (1U << 0)
#define TASK_EVENT_CONFIG_READY         (1U << 1)

int task_events_init(void);

EventGroupHandle_t task_events_get(void);

#endif /* TASK_EVENTS_H */
