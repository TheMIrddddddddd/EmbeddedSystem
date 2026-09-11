#include "board_timebase.h"
#include "gd32f4xx.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_timer.h"
#include "gd32f4xx_misc.h"
#include "board_config.h"

static board_timebase_alarm_callback_t s_timebase_alarm_callback;

static volatile uint32_t s_timebase_timer_clock_hz;
static volatile uint32_t s_timebase_prescaler;
static volatile uint8_t s_timebase_initialized;
static volatile uint32_t s_timebase_alarm_deadline_us;
static volatile uint32_t s_timebase_alarm_fire_count;
static volatile uint8_t s_timebase_alarm_armed;

static uint32_t board_timebase_timer_clock_get(void)
{
    uint32_t hclk;
    uint32_t pclk1;
    uint32_t apb1_divider;

    hclk = rcu_clock_freq_get(CK_AHB);
    pclk1 = rcu_clock_freq_get(CK_APB1);

    if ((hclk == 0U) || (pclk1 == 0U))
    {
        return 0U;
    }

    apb1_divider = hclk / pclk1;

    if ((RCU_CFG1 & RCU_CFG1_TIMERSEL) == 0U)
    {
        if (apb1_divider == 1U)
        {
            return hclk;
        }

        return pclk1 * 2U;
    }

    if (apb1_divider <= 4U)
    {
        return hclk;
    }

    return pclk1 * 4U;
}

int board_timebase_init(void)
{
    timer_parameter_struct timer_config;
    uint32_t timer_clock;
    uint32_t divider;

    if (s_timebase_initialized != 0U)
    {
        return 1;
    }

    timer_clock = board_timebase_timer_clock_get();

    if ((timer_clock == 0U) ||
        ((timer_clock % BOARD_TIMEBASE_FREQUENCY_HZ) != 0U))
    {
        return 0;
    }

    divider = timer_clock / BOARD_TIMEBASE_FREQUENCY_HZ;

    /*
     * TIMER PSC 为 16 位；
     * 实际分频系数 = PSC + 1。
     */
    if ((divider == 0U) || (divider > 65536U))
    {
        return 0;
    }

    rcu_periph_clock_enable(BOARD_TIMEBASE_TIMER_RCU);

    timer_deinit(BOARD_TIMEBASE_TIMER);

    timer_struct_para_init(&timer_config);

    timer_config.prescaler = (uint16_t)(divider - 1U);
    timer_config.alignedmode = TIMER_COUNTER_EDGE;
    timer_config.counterdirection = TIMER_COUNTER_UP;
    timer_config.period = 0xFFFFFFFFUL;
    timer_config.clockdivision = TIMER_CKDIV_DIV1;
    timer_config.repetitioncounter = 0U;

    timer_init(BOARD_TIMEBASE_TIMER, &timer_config);

    /*
     * 显式装载预分频器，再从 0 开始计数。
     */
    timer_prescaler_config(BOARD_TIMEBASE_TIMER, timer_config.prescaler, TIMER_PSC_RELOAD_NOW);

    timer_counter_value_config(BOARD_TIMEBASE_TIMER, 0U);

    timer_channel_output_mode_config(BOARD_TIMEBASE_TIMER, BOARD_TIMEBASE_ALARM_CHANNEL, TIMER_OC_MODE_TIMING);
    timer_channel_output_shadow_config(BOARD_TIMEBASE_TIMER, BOARD_TIMEBASE_ALARM_CHANNEL, TIMER_OC_SHADOW_DISABLE);
    timer_interrupt_disable(BOARD_TIMEBASE_TIMER, TIMER_INT_CH0);
    timer_interrupt_flag_clear(BOARD_TIMEBASE_TIMER, TIMER_INT_FLAG_CH0);

    s_timebase_alarm_callback = NULL;
    s_timebase_alarm_deadline_us = 0U;
    s_timebase_alarm_fire_count = 0U;
    s_timebase_alarm_armed = 0U;

    NVIC_ClearPendingIRQ(BOARD_TIMEBASE_ALARM_IRQ);

    nvic_irq_enable(BOARD_TIMEBASE_ALARM_IRQ, BOARD_TIMEBASE_ALARM_PRIORITY, 0U);

    timer_enable(BOARD_TIMEBASE_TIMER);

    s_timebase_timer_clock_hz = timer_clock;
    s_timebase_prescaler = divider - 1U;
    s_timebase_initialized = 1U;

    return 1;
}

uint32_t board_timebase_now_us(void)
{
    return timer_counter_read(BOARD_TIMEBASE_TIMER);
}

static int board_timebase_deadline_reached(uint32_t now, uint32_t deadline)
{
    return ((uint32_t)(now - deadline) < 0x80000000UL) ? 1 : 0;
}

int board_timebase_alarm_start(uint32_t delay_us, board_timebase_alarm_callback_t callback)
{
    uint32_t primask;
    uint32_t deadline;
    uint32_t now;

    if ((s_timebase_initialized == 0U) ||
        (callback == NULL) ||
        (delay_us == 0U) ||
        (delay_us > 0x7FFFFFFFUL))
    {
        return 0;
    }

    /*
     * 任务与 USART1 中断都可能重新设置期限。
     * 保存原来的中断状态，结束时原样恢复。
     */
    primask = __get_PRIMASK();
    __disable_irq();

    timer_interrupt_disable(BOARD_TIMEBASE_TIMER, TIMER_INT_CH0);
    timer_interrupt_flag_clear(BOARD_TIMEBASE_TIMER, TIMER_INT_FLAG_CH0);
    NVIC_ClearPendingIRQ(BOARD_TIMEBASE_ALARM_IRQ);

    deadline = board_timebase_now_us() + delay_us;

    s_timebase_alarm_callback = callback;
    s_timebase_alarm_deadline_us = deadline;
    s_timebase_alarm_armed = 1U;

    timer_channel_output_pulse_value_config(BOARD_TIMEBASE_TIMER, BOARD_TIMEBASE_ALARM_CHANNEL, deadline);

    timer_interrupt_enable(BOARD_TIMEBASE_TIMER,TIMER_INT_CH0);

    now = board_timebase_now_us();

    if (board_timebase_deadline_reached(now, deadline) != 0)
    {
        NVIC_SetPendingIRQ(BOARD_TIMEBASE_ALARM_IRQ);
    }

    __set_PRIMASK(primask);

    return 1;
}

void board_timebase_alarm_cancel(void)
{
    uint32_t primask;

    if (s_timebase_initialized == 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    timer_interrupt_disable(BOARD_TIMEBASE_TIMER, TIMER_INT_CH0);

    timer_interrupt_flag_clear(BOARD_TIMEBASE_TIMER, TIMER_INT_FLAG_CH0);

    NVIC_ClearPendingIRQ(BOARD_TIMEBASE_ALARM_IRQ);

    s_timebase_alarm_armed = 0U;
    s_timebase_alarm_callback = NULL;

    __set_PRIMASK(primask);
}

void board_timebase_irq_handler(void)
{
    board_timebase_alarm_callback_t callback;
    uint32_t now;

    /*
     * 此模块独占 TIMER1 中断，目前仅使用 CH0。
     * 同时支持硬件比较事件与补挂的软件 pending。
     */
    timer_interrupt_flag_clear(BOARD_TIMEBASE_TIMER, TIMER_INT_FLAG_CH0);

    if (s_timebase_alarm_armed == 0U)
    {
        return;
    }

    now = board_timebase_now_us();

    /*
     * 旧的 pending 不得提前触发新期限。
     * 如果期限尚未到，保留 CH0 中断等待真正到期。
     */
    if (board_timebase_deadline_reached(now, s_timebase_alarm_deadline_us) == 0)
    {
        return;
    }

    timer_interrupt_disable(BOARD_TIMEBASE_TIMER, TIMER_INT_CH0);

    s_timebase_alarm_armed = 0U;

    callback = s_timebase_alarm_callback;
    s_timebase_alarm_callback = NULL;

    s_timebase_alarm_fire_count++;

    /*
     * 先解除本次定时，再调用回调。
     * 回调可以重新启动下一阶段的定时。
     */
    if (callback != NULL)
    {
        callback();
    }
}
