#include "task_mutex.h"
static StaticSemaphore_t s_task_mutex;
static SemaphoreHandle_t s_task_mutex_handle;

int task_mutex_init(void)
{
    s_task_mutex_handle = xSemaphoreCreateMutexStatic(&s_task_mutex);

    if (s_task_mutex_handle == NULL)
    {
        return 0;
    }

    return 1;
}

SemaphoreHandle_t task_mutex_get(void)
{
    return s_task_mutex_handle;
}
