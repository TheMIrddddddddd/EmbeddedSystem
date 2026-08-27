#include "app_task.h"
#include "task_queues.h"
#include "task_events.h"
#include "task_timer.h"
#include "task_mutex.h"

app_tasks_status_t app_tasks_create(void)
{   
    if (task_timer_init() == 0)
    {
        return APP_TASKS_STATUS_TIMER_CREATE_FAILED;
    }
    if (task_mutex_init() == 0)
    {
        return APP_TASKS_STATUS_MUTEX_CREATE_FAILED;
    }
    if (task_events_init() == 0)
    {
        return APP_TASKS_STATUS_EVENT_CREATE_FAILED;
    }
    if (task_queues_init() == 0)
    {
        return APP_TASKS_STATUS_QUEUE_CREATE_FAILED;
    }
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
