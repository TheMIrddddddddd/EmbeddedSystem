#ifndef TEST_APP_CLI_EVENT_GROUPS_H
#define TEST_APP_CLI_EVENT_GROUPS_H

#include "FreeRTOS.h"

typedef void *EventGroupHandle_t;
BaseType_t xEventGroupSetBits(EventGroupHandle_t group, uint32_t bits);

#endif
