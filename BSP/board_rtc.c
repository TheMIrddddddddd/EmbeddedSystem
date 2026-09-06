#include "gd32f4xx.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_pmu.h"
#include "gd32f4xx_rtc.h"
 
#include "board_rtc.h"

#define BOARD_RTC_INIT_MAGIC        0x32363130U

/*
 * LSE 未起振时寄存器读出的是默认值 2000-01-01 00:00:00，
 * 用 ready 标志区分"真的在走"和"读到了假默认值"。
 */
static uint8_t s_board_rtc_ready;

static uint8_t board_rtc_bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t board_rtc_bcd_to_bin(uint8_t value)
{
    return (uint8_t)((((value & 0xF0U) >> 4) * 10U) + (value & 0x0FU));
}

/* Sakamoto 法，0=周日；只用于填充 RTC 的 DOW 字段，工程不显示星期 */
static uint8_t board_rtc_day_of_week(uint16_t year, uint8_t month, uint8_t date)
{
    static const uint8_t month_offset[12] = { 0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U };
    uint16_t shifted = year;
 
    if (month < 3U)
    {
        shifted -= 1U;
    }
 
    return (uint8_t)((shifted + (shifted / 4U) - (shifted / 100U) + (shifted / 400U) + month_offset[month - 1U] + date) % 7U);
}

static int board_rtc_write_calendar(const board_rtc_time_t *time)
{
    rtc_parameter_struct init_struct;
 
    init_struct.year = board_rtc_bin_to_bcd((uint8_t)(time->year - 2000U));
    init_struct.month = board_rtc_bin_to_bcd(time->month);
    init_struct.date = board_rtc_bin_to_bcd(time->date);
    init_struct.day_of_week = (uint8_t)(board_rtc_day_of_week(time->year, time->month, time->date) + 1U);
    init_struct.hour = board_rtc_bin_to_bcd(time->hour);
    init_struct.minute = board_rtc_bin_to_bcd(time->minute);
    init_struct.second = board_rtc_bin_to_bcd(time->second);
    /* 32768Hz = (127+1)*(255+1) */
    init_struct.factor_asyn = 0x7FU;
    init_struct.factor_syn = 0xFFU;
    init_struct.am_pm = RTC_AM;
    init_struct.display_format = RTC_24HOUR;
 
    if (rtc_init(&init_struct) == ERROR)
    {
        return 0;
    }
 
    if (rtc_register_sync_wait() == ERROR)
    {
        return 0;
    }
 
    return 1;
}

int board_rtc_init(void)
{
    board_rtc_time_t default_time;
 
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();
    rcu_periph_clock_enable(RCU_RTC);
 
    if (RTC_BKP0 == BOARD_RTC_INIT_MAGIC)
    {
        /* 备份域未掉电：日历在走，直接复用 */
        if (rtc_register_sync_wait() == ERROR)
        {
            return 0;
        }

        s_board_rtc_ready = 1U;

        return 1;
    }
 
    rcu_osci_on(RCU_LXTAL);
 
    /* 内部带 LXTAL_STARTUP_TIMEOUT 超时，晶振不起振返回 ERROR 而不是卡死 */
    if (rcu_osci_stab_wait(RCU_LXTAL) != SUCCESS)
    {
        return 0;
    }
 
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

    /* 冷启动路径（魔数不存在 = 备份域全新）不需要复位脉冲：
     * 复位枚举里没有 RCU_RTC，BDCTL 的 RTCEN 位也不是复位位，
     * 硬凑 rcu_periph_reset_enable 只会开关时钟，还触发 #188 警告。 */
    rcu_periph_clock_enable(RCU_RTC);
 
    /* 上电默认给非 2000-01-01 00:00:00 的时间，test 的 RTC 项以"非默认值"为 PASS */
    default_time.year = 2026U;
    default_time.month = 1U;
    default_time.date = 1U;
    default_time.hour = 0U;
    default_time.minute = 0U;
    default_time.second = 0U;
 
    if (board_rtc_write_calendar(&default_time) == 0)
    {
        return 0;
    }

    RTC_BKP0 = BOARD_RTC_INIT_MAGIC;

    s_board_rtc_ready = 1U;

    return 1;
}

int board_rtc_time_get(board_rtc_time_t *time)
{
    rtc_parameter_struct current;

    if ((time == NULL) || (s_board_rtc_ready == 0U))
    {
        return 0;
    }
 
    rtc_current_time_get(&current);
 
    time->year = (uint16_t)(2000U + board_rtc_bcd_to_bin(current.year));
    time->month = board_rtc_bcd_to_bin(current.month);
    time->date = board_rtc_bcd_to_bin(current.date);
    time->hour = board_rtc_bcd_to_bin(current.hour);
    time->minute = board_rtc_bcd_to_bin(current.minute);
    time->second = board_rtc_bcd_to_bin(current.second);
 
    return 1;
}

int board_rtc_time_set(const board_rtc_time_t *time)
{
    if ((time == NULL) || (s_board_rtc_ready == 0U))
    {
        return 0;
    }
 
    if ((time->year < 2000U) || (time->year > 2099U) ||
        (time->month < 1U) || (time->month > 12U) ||
        (time->date < 1U) || (time->date > 31U) ||
        (time->hour > 23U) || (time->minute > 59U) || (time->second > 59U))
    {
        return 0;
    }
 
    if (board_rtc_write_calendar(time) == 0)
    {
        return 0;
    }
 
    RTC_BKP0 = BOARD_RTC_INIT_MAGIC;
 
    return 1;
}
