#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include <stdint.h>

int display_task_create(void);
uint32_t display_task_get_heartbeat(void);
uint32_t display_task_get_stack_high_water_mark(void);

#endif /* DISPLAY_TASK_H */
