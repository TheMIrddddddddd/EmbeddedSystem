#include "task_events.h"

static StaticEventGroup_t s_task_event_group;
static EventGroupHandle_t s_task_event_group_handle;

int task_events_init(void)
{
    s_task_event_group_handle = xEventGroupCreateStatic(&s_task_event_group);

    if (s_task_event_group_handle == NULL)
    {
        return 0;
    }

    return 1;
}

EventGroupHandle_t task_events_get(void)
{
    return s_task_event_group_handle;
}

