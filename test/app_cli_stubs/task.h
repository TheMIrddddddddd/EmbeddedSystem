#ifndef TEST_APP_CLI_TASK_H
#define TEST_APP_CLI_TASK_H

#include "FreeRTOS.h"

TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);

#endif /* TEST_APP_CLI_TASK_H */
