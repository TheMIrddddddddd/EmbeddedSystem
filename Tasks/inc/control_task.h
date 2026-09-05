#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>

int control_task_create(void);
uint32_t control_task_get_heartbeat(void);
uint32_t control_task_get_stack_high_water_mark(void);
#endif /* CONTROL_TASK_H */
