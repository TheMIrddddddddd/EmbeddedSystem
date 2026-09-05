#ifndef PROTOCOL_TASK_H
#define PROTOCOL_TASK_H

#include <stdint.h>

int protocol_task_create(void);
uint32_t protocol_task_get_heartbeat(void);
uint32_t protocol_task_get_stack_high_water_mark(void);

#endif /* PROTOCOL_TASK_H */
