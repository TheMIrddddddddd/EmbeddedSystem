#ifndef TASK_MUTEX_H
#define TASK_MUTEX_H

#include "FreeRTOS.h"
#include "semphr.h"

int task_mutex_init(void);

SemaphoreHandle_t task_mutex_get(void);

#endif /* TASK_MUTEX_H */
