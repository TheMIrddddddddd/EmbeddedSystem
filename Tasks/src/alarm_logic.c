#include "alarm_logic.h"

#include <float.h>
#include <stddef.h>

/* 检查浮点数是否为有限值，NaN 和正负无穷均视为非法输入。 */
static int alarm_logic_float_finite(float value)
{
    if (value != value)
    {
        return 0;
    }

    if ((value > FLT_MAX) || (value < -FLT_MAX))
    {
        return 0;
    }

    return 1;
}

/* 检查阈值是否为有限的非负数。 */
static int alarm_logic_limit_valid(float limit)
{
    if (alarm_logic_float_finite(limit) == 0)
    {
        return 0;
    }

    return (limit >= 0.0f) ? 1 : 0;
}

/* 将单通道告警状态初始化为 NORMAL，并清除连续超限计数。 */
int alarm_logic_init(alarm_logic_channel_t *channel)
{
    if (channel == NULL)
    {
        return 0;
    }

    channel->state = ALARM_LOGIC_STATE_NORMAL;
    channel->consecutive_over_limit = 0U;

    return 1;
}

/* 更新一个采样值对应的告警状态，并只在真正发生状态事件时写出事件。 */
alarm_logic_status_t alarm_logic_update(alarm_logic_channel_t *channel,
                                        float value,
                                        float limit,
                                        alarm_logic_event_t *event)
{
    alarm_logic_channel_t next;
    alarm_logic_event_t next_event;

    if ((channel == NULL) ||
        (event == NULL) ||
        (alarm_logic_float_finite(value) == 0) ||
        (alarm_logic_limit_valid(limit) == 0))
    {
        return ALARM_LOGIC_STATUS_ERROR;
    }

    if ((channel->state != ALARM_LOGIC_STATE_NORMAL) &&
        (channel->state != ALARM_LOGIC_STATE_ACTIVE) &&
        (channel->state != ALARM_LOGIC_STATE_RECOVERED))
    {
        return ALARM_LOGIC_STATUS_ERROR;
    }

    next = *channel;
    next_event = ALARM_LOGIC_EVENT_NONE;

    if (next.state == ALARM_LOGIC_STATE_NORMAL)
    {
        if (value > limit)
        {
            if (next.consecutive_over_limit < ALARM_LOGIC_TRIGGER_COUNT)
            {
                next.consecutive_over_limit++;
            }

            if (next.consecutive_over_limit >= ALARM_LOGIC_TRIGGER_COUNT)
            {
                next.state = ALARM_LOGIC_STATE_ACTIVE;
                next.consecutive_over_limit = ALARM_LOGIC_TRIGGER_COUNT;
                next_event = ALARM_LOGIC_EVENT_TRIGGERED;
            }
        }
        else
        {
            next.consecutive_over_limit = 0U;
        }
    }
    else if (next.state == ALARM_LOGIC_STATE_ACTIVE)
    {
        if (value < (limit - ALARM_LOGIC_RECOVERY_HYSTERESIS))
        {
            next.state = ALARM_LOGIC_STATE_RECOVERED;
            next.consecutive_over_limit = 0U;
            next_event = ALARM_LOGIC_EVENT_RECOVERED;
        }
        else
        {
            next.consecutive_over_limit = ALARM_LOGIC_TRIGGER_COUNT;
        }
    }
    else
    {
        /* RECOVERED 只表示本次恢复事件，下一样本重新进入 NORMAL。 */
        next.state = ALARM_LOGIC_STATE_NORMAL;
        next.consecutive_over_limit = (value > limit) ? 1U : 0U;
    }

    *channel = next;

    if (next_event != ALARM_LOGIC_EVENT_NONE)
    {
        *event = next_event;
        return ALARM_LOGIC_STATUS_EVENT;
    }

    return ALARM_LOGIC_STATUS_NO_EVENT;
}
