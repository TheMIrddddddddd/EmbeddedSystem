#ifndef TEST_APP_CLI_FREERTOS_H
#define TEST_APP_CLI_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#define taskENTER_CRITICAL() test_critical_enter()
#define taskEXIT_CRITICAL()  test_critical_exit()
#define pdMS_TO_TICKS(value) ((TickType_t)(value))

void test_critical_enter(void);
void test_critical_exit(void);

#endif /* TEST_APP_CLI_FREERTOS_H */
