#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>
#include "storage_task.h"

int control_task_create(void);
uint32_t control_task_get_heartbeat(void);
uint32_t control_task_get_stack_high_water_mark(void);
int control_apply_persisted_config(const storage_task_persist_result_t *result);
#endif /* CONTROL_TASK_H */
