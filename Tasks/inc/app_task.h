#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "protocol_task.h"
#include "sample_task.h"
#include "storage_task.h"
#include "alarm_task.h"
#include "control_task.h"
#include "display_task.h"
#include "health_task.h"

typedef enum
{
    APP_TASKS_STATUS_OK = 0,
    APP_TASKS_STATUS_DISPLAY_CREATE_FAILED,
    APP_TASKS_STATUS_SAMPLE_CREATE_FAILED,
    APP_TASKS_STATUS_STORAGE_CREATE_FAILED,
    APP_TASKS_STATUS_ALARM_CREATE_FAILED,
    APP_TASKS_STATUS_CONTROL_CREATE_FAILED,
    APP_TASKS_STATUS_PROTOCOL_CREATE_FAILED,
    APP_TASKS_STATUS_HEALTH_CREATE_FAILED
} app_tasks_status_t;

app_tasks_status_t app_tasks_create(void);

#endif
