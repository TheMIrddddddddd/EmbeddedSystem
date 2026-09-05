#ifndef ALARM_TASK_H
#define ALARM_TASK_H

#include <stdint.h>

int alarm_task_create(void);
uint32_t alarm_task_get_heartbeat(void);
uint32_t alarm_task_get_stack_high_water_mark(void);

#endif /* ALARM_TASK_H */
