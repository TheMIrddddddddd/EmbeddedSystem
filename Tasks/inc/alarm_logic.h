#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H

#include <stdint.h>

#define ALARM_LOGIC_TRIGGER_COUNT       3U
#define ALARM_LOGIC_RECOVERY_HYSTERESIS 0.05f

typedef enum
{
    ALARM_LOGIC_STATE_NORMAL = 0,
    ALARM_LOGIC_STATE_ACTIVE,
    ALARM_LOGIC_STATE_RECOVERED
} alarm_logic_state_t;

typedef enum
{
    ALARM_LOGIC_EVENT_NONE = 0,
    ALARM_LOGIC_EVENT_TRIGGERED,
    ALARM_LOGIC_EVENT_RECOVERED
} alarm_logic_event_t;

typedef enum
{
    ALARM_LOGIC_STATUS_ERROR = 0,
    ALARM_LOGIC_STATUS_NO_EVENT,
    ALARM_LOGIC_STATUS_EVENT
} alarm_logic_status_t;

typedef struct
{
    alarm_logic_state_t state;
    uint8_t consecutive_over_limit;
} alarm_logic_channel_t;

int alarm_logic_init(alarm_logic_channel_t *channel);
alarm_logic_status_t alarm_logic_update(alarm_logic_channel_t *channel,
                                        float value,
                                        float limit,
                                        alarm_logic_event_t *event);

#endif
