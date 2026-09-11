#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stddef.h>
void test_critical_enter(void);
void test_critical_exit(void);
#define taskENTER_CRITICAL() test_critical_enter()
#define taskEXIT_CRITICAL() test_critical_exit()
#endif
