#include "app_task.h"

app_tasks_status_t app_tasks_create(void)
{
    if (display_task_create() == 0) {
        return APP_TASKS_STATUS_DISPLAY_CREATE_FAILED;
    }

    if (sample_task_create() == 0) {
        return APP_TASKS_STATUS_SAMPLE_CREATE_FAILED;
    }

    if (storage_task_create() == 0) {
        return APP_TASKS_STATUS_STORAGE_CREATE_FAILED;
    }

    if (alarm_task_create() == 0) {
        return APP_TASKS_STATUS_ALARM_CREATE_FAILED;
    }

    if (control_task_create() == 0) {
        return APP_TASKS_STATUS_CONTROL_CREATE_FAILED;
    }

    if (protocol_task_create() == 0) {
        return APP_TASKS_STATUS_PROTOCOL_CREATE_FAILED;
    }

    if (health_task_create() == 0) {
        return APP_TASKS_STATUS_HEALTH_CREATE_FAILED;
    }

    return APP_TASKS_STATUS_OK;
}
