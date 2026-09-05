#ifndef HEALTH_TASK_H
#define HEALTH_TASK_H

#include <stdint.h>

int health_task_create(void);

uint32_t health_task_get_heartbeat(void);

uint8_t health_task_all_healthy(void);

#endif /* HEALTH_TASK_H */
