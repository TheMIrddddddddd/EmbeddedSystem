#include "board_internal_flash.h"
#include "common_flash_layout.h"
#include "gd32f4xx_fmc.h"
#include "gd32f4xx_fwdgt.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_timer.h"
#include "board_config.h"

#define BOARD_INTERNAL_FLASH_FMC_CLEAR_FLAGS \
    (FMC_FLAG_END | FMC_FLAG_RDDERR | FMC_FLAG_PGSERR | \
     FMC_FLAG_PGMERR | FMC_FLAG_WPERR | FMC_FLAG_OPERR)

#define BOARD_INTERNAL_FLASH_TIMEBASE_FREQUENCY_HZ          1000000U
#define BOARD_INTERNAL_FLASH_PAGE_ERASE_TIMEOUT_US          1000000U
#define BOARD_INTERNAL_FLASH_WATCHDOG_FEED_INTERVAL_US      100000U
#define BOARD_INTERNAL_FLASH_PROGRAM_TIMEOUT_US             1000U

static uint8_t s_internal_flash_timebase_initialized;

typedef struct
{
    uint32_t base;
    uint32_t end;
    uint32_t page_count;
} board_internal_flash_region_info_t;


static uint32_t board_internal_flash_timer_clock_get(void)
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

static uint8_t board_internal_flash_timebase_init(void)
{
    timer_parameter_struct timer_config;
    uint32_t timer_clock;
    uint32_t divider;

    if (s_internal_flash_timebase_initialized != 0U)
    {
        return 1U;
    }

    timer_clock = board_internal_flash_timer_clock_get();

    if ((timer_clock == 0U) || ((timer_clock % BOARD_INTERNAL_FLASH_TIMEBASE_FREQUENCY_HZ) != 0U))
    {
        return 0U;
    }

    divider = timer_clock / BOARD_INTERNAL_FLASH_TIMEBASE_FREQUENCY_HZ;

    /*
     * TIMER1 是 32 位自由运行计数器，
     * PSC 实际分频系数为 prescaler + 1。
     */
    if ((divider == 0U) || (divider > 65536U))
    {
        return 0U;
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

    timer_prescaler_config(BOARD_TIMEBASE_TIMER, timer_config.prescaler, TIMER_PSC_RELOAD_NOW);
    timer_counter_value_config(BOARD_TIMEBASE_TIMER, 0U);

    /*
     * Boot 时基不启用 TIMER1 中断，只读取硬件 CNT。
     */
    timer_enable(BOARD_TIMEBASE_TIMER);

    s_internal_flash_timebase_initialized = 1U;

    return 1U;

}

static uint32_t board_internal_flash_timebase_now_us(void)
{
    return timer_counter_read(BOARD_TIMEBASE_TIMER);
}

static fmc_state_enum board_internal_flash_fmc_wait_ready_watchdog(uint32_t timeout_us,uint32_t feed_interval_us)
{
    fmc_state_enum state;
    uint32_t start_us;
    uint32_t last_feed_us;
    uint32_t now_us;

    if ((timeout_us == 0U) || (feed_interval_us == 0U) || (board_internal_flash_timebase_init() == 0U))
    {
        return FMC_TOERR;
    }

    fwdgt_counter_reload();
    start_us = board_internal_flash_timebase_now_us();
    last_feed_us = start_us;

    for (;;)
    {
        state = fmc_state_get();

        if (state != FMC_BUSY)
        {
            return state;
        }

        now_us = board_internal_flash_timebase_now_us();

        if ((uint32_t)(now_us - start_us) >= timeout_us)
        {
            return FMC_TOERR;
        }
        if ((uint32_t)(now_us - last_feed_us) >= feed_interval_us)
        {
            fwdgt_counter_reload();
            last_feed_us = now_us;
        }
    }
}

/*
 * 仅负责把逻辑区域映射为 Common 中的唯一边界宏。
 * 本函数不访问 FMC、不擦除 Flash、不执行编程。
 */
static board_internal_flash_status_t board_internal_flash_region_info_get(board_internal_flash_region_t region, board_internal_flash_region_info_t *info)
{
    if (info == 0)
    {
        return BOARD_INTERNAL_FLASH_STATUS_INVALID_ARGUMENT;
    }
    
    switch (region)
    {
    case BOARD_INTERNAL_FLASH_REGION_APP:
        info->base = APP_BASE;
        info->end = APP_END;
        info->page_count = APP_PAGE_COUNT;
        break;
    
    case BOARD_INTERNAL_FLASH_REGION_BACKUP:
        info->base = BACKUP_BASE;
        info->end = BACKUP_END;
        info->page_count = BACKUP_PAGE_COUNT;
        break;

    case BOARD_INTERNAL_FLASH_REGION_STAGING:
        info->base = STAGING_BASE;
        info->end = STAGING_END;
        info->page_count = STAGING_PAGE_COUNT;
        break;

    default:
        return BOARD_INTERNAL_FLASH_STATUS_INVALID_REGION;
    }

    if ((INTERNAL_FLASH_PAGE_SIZE == 0U) ||
        ((INTERNAL_FLASH_PAGE_SIZE & (INTERNAL_FLASH_PAGE_SIZE - 1UL)) != 0U) ||
        (info->base > info->end) ||
        (info->page_count == 0U) ||
        ((info->base & (INTERNAL_FLASH_PAGE_SIZE - 1UL)) != 0U) ||
        ((info->end & (INTERNAL_FLASH_PAGE_SIZE - 1UL)) != (INTERNAL_FLASH_PAGE_SIZE - 1UL)) ||
        (((uint64_t)info->page_count * (uint64_t)INTERNAL_FLASH_PAGE_SIZE) != ((uint64_t)info->end - (uint64_t)info->base + 1ULL)))
    {
        return BOARD_INTERNAL_FLASH_STATUS_LAYOUT_INVALID;
    }
    
    return BOARD_INTERNAL_FLASH_STATUS_OK;

}

/*
 * 将 region + page_index 解析为一个合法的 4 KiB 页首地址。
 *
 * - 不调用 fmc_unlock()
 * - 不调用 fmc_page_erase()
 * - 不调用任何 program 函数
 * - 不访问 SPI Flash
 */
static board_internal_flash_status_t board_internal_flash_page_address_resolve(board_internal_flash_region_t region, uint32_t page_index, uint32_t *page_address)
{
    board_internal_flash_region_info_t info;
    board_internal_flash_status_t status;

    if (page_address == 0)
    {
        return BOARD_INTERNAL_FLASH_STATUS_INVALID_ARGUMENT;
    }
    
    status = board_internal_flash_region_info_get(region, &info);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return status;
    }

    if (page_index >= info.page_count)
    {
        return BOARD_INTERNAL_FLASH_STATUS_PAGE_INDEX_OUT_OF_RANGE;
    }

    /*
     * 先防止 base + page_index * page_size 发生 uint32_t 溢出。
     */
    if (page_index > ((0xFFFFFFFFUL - info.base) / INTERNAL_FLASH_PAGE_SIZE))
    {
        return BOARD_INTERNAL_FLASH_STATUS_LAYOUT_INVALID;
    }

    *page_address = info.base + page_index * INTERNAL_FLASH_PAGE_SIZE;

    /*
     * 再次验证计算出的地址确实是页首，并且完整页仍位于
     * 所选逻辑区域内。
     */
    if (((*page_address & (INTERNAL_FLASH_PAGE_SIZE - 1UL)) != 0U) ||
        (*page_address < info.base) ||
        (*page_address > info.end) ||
        ((INTERNAL_FLASH_PAGE_SIZE - 1UL) > (info.end - *page_address)))
    {
        return BOARD_INTERNAL_FLASH_STATUS_LAYOUT_INVALID;
    }

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

/*
 * 将 GD32 SPL 的 FMC 状态转换为项目 BSP 层状态。
 *
 * FMC_BUSY 只允许作为内部等待过程中的瞬态状态。
 * 如果同步操作在等待期限结束时仍然是 BUSY，
 * 对外统一返回 FMC_TIMEOUT。
 */
static board_internal_flash_status_t board_internal_flash_fmc_state_to_status(fmc_state_enum fmc_state)
{
    switch (fmc_state)
    {
        case FMC_READY:
            return BOARD_INTERNAL_FLASH_STATUS_OK;

        case FMC_BUSY:
        case FMC_TOERR:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_TIMEOUT;

        case FMC_RDDERR:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_RDDERR;

        case FMC_PGSERR:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_PGSERR;

        case FMC_PGMERR:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_PGMERR;

        case FMC_WPERR:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_WPERR;

        case FMC_OPERR:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_OPERR;

        default:
            return BOARD_INTERNAL_FLASH_STATUS_FMC_UNEXPECTED;
    }
}

/*
 * 编程公共接口的参数前置校验。
 *
 * 本函数只做：
 * - region/page_index 到页首地址解析；
 * - page_offset/length 页内范围检查；
 * - 数据指针检查；
 * - 起始编程地址计算。
 *
 * 本函数不检查目标是否为 0xFF，
 * 不解锁 FMC，不清状态位，不执行编程。
 */
static board_internal_flash_status_t board_internal_flash_page_program_preflight(
                                                board_internal_flash_region_t region,
                                                uint32_t page_index,
                                                uint32_t page_offset,
                                                const uint8_t *data,
                                                uint32_t length,
                                                uint32_t *program_address)
{
    board_internal_flash_status_t status;
    uint32_t page_address;

    if (program_address == 0)
    {
        return BOARD_INTERNAL_FLASH_STATUS_INVALID_ARGUMENT;
    }
    
    status = board_internal_flash_page_address_resolve(region, page_index, &page_address);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return status;
    }

    if (page_offset >= INTERNAL_FLASH_PAGE_SIZE)
    {
        return BOARD_INTERNAL_FLASH_STATUS_PAGE_OFFSET_OUT_OF_RANGE;
    }
    
    if (length == 0U)
    {
        return BOARD_INTERNAL_FLASH_STATUS_LENGTH_INVALID;
    }

    if (data == 0)
    {
        return BOARD_INTERNAL_FLASH_STATUS_INVALID_ARGUMENT;
    }

    if (length > (INTERNAL_FLASH_PAGE_SIZE - page_offset))
    {
        return BOARD_INTERNAL_FLASH_STATUS_CROSS_PAGE;
    }

    if (page_offset > (0xFFFFFFFFUL - page_address))
    {
        return BOARD_INTERNAL_FLASH_STATUS_LAYOUT_INVALID;
    }

    *program_address = page_address + page_offset;

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

static board_internal_flash_status_t board_internal_flash_program_target_erased_check(uint32_t program_address, uint32_t length)
{
    uint32_t offset;

    for (offset = 0U; offset < length; offset++)
    {
        if (*(volatile uint8_t *)(program_address + offset) != 0xFFU)
        {
            return BOARD_INTERNAL_FLASH_STATUS_TARGET_NOT_ERASED;
        }
    }
    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

static board_internal_flash_status_t board_internal_flash_program_unit(uint32_t address, uint32_t value, uint32_t program_size)
{
    fmc_state_enum fmc_state;
    board_internal_flash_status_t status;

    fmc_flag_clear(BOARD_INTERNAL_FLASH_FMC_CLEAR_FLAGS);

    FMC_CTL &= ~FMC_CTL_PSZ;
    FMC_CTL |= program_size;
    FMC_CTL |= FMC_CTL_PG;

    switch (program_size)
    {
        case CTL_PSZ_WORD:
            REG32(address) = value;
            break;

        case CTL_PSZ_HALF_WORD:
            REG16(address) = (uint16_t)value;
            break;

        case CTL_PSZ_BYTE:
            REG8(address) = (uint8_t)value;
            break;

        default:
            FMC_CTL &= ~FMC_CTL_PG;
            return BOARD_INTERNAL_FLASH_STATUS_FMC_UNEXPECTED;
    }

    fmc_state = board_internal_flash_fmc_wait_ready_watchdog(BOARD_INTERNAL_FLASH_PROGRAM_TIMEOUT_US, BOARD_INTERNAL_FLASH_WATCHDOG_FEED_INTERVAL_US);
    
    /*
     * 成功、超时、FMC 错误都必须清除 PG。
     */
    FMC_CTL &= ~FMC_CTL_PG;

    status = board_internal_flash_fmc_state_to_status(fmc_state);

    return status;
}

static board_internal_flash_status_t board_internal_flash_program_verify(uint32_t program_address, const uint8_t *data, uint32_t length)
{
    uint32_t offset;

    for (offset = 0U; offset < length; offset++)
    {
        if (*(volatile uint8_t *)(program_address + offset) != data[offset])
        {
            return BOARD_INTERNAL_FLASH_STATUS_PROGRAM_VERIFY_FAILED;
        }
    }

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

static board_internal_flash_status_t board_internal_flash_page_erase_verify(uint32_t page_address)
{
    uint32_t offset;
    uint32_t last_feed_us;
    uint32_t now_us;

    /*
     * 该函数只允许由成功完成 FMC 操作的路径调用。
     * 成功的 FMC 等待过程已经初始化 TIMER1。
     */
    if (s_internal_flash_timebase_initialized == 0U)
    {
        return BOARD_INTERNAL_FLASH_STATUS_FMC_TIMEOUT;
    }

    fwdgt_counter_reload();

    last_feed_us = board_internal_flash_timebase_now_us();

    for (offset = 0U; offset < INTERNAL_FLASH_PAGE_SIZE; offset++)
    {
        if (*(volatile uint8_t *)(page_address + offset) != 0xFFU)
        {
            return BOARD_INTERNAL_FLASH_STATUS_ERASE_VERIFY_FAILED;
        }

        now_us = board_internal_flash_timebase_now_us();

        if ((uint32_t)(now_us - last_feed_us) >= BOARD_INTERNAL_FLASH_WATCHDOG_FEED_INTERVAL_US)
        {
            fwdgt_counter_reload();
            last_feed_us = now_us;
        }
    }

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

board_internal_flash_status_t board_internal_flash_page_erase(board_internal_flash_region_t region, uint32_t page_index)
{
    board_internal_flash_status_t status;
    fmc_state_enum fmc_state;
    uint32_t page_address;
    uint32_t primask;

    /*
     * 第一道门：只允许 App/Backup/Staging 的合法页。
     * 失败时不会解锁 FMC，也不会修改 Flash。
     */
    status = board_internal_flash_page_address_resolve(region, page_index, &page_address);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return status;
    }

    /*
     * 当前 Boot 阶段没有需要在单页擦除期间继续运行的中断业务。
     * 保存并屏蔽中断，避免未来协议/定时器 ISR 访问正在处理的区域。
     * FMC 等待期间依赖 TIMER1 硬件计数，不依赖 SysTick 中断。
     */
    primask = __get_PRIMASK();
    __disable_irq();

    /*
     * 从这里开始，所有退出路径都必须经过 cleanup，
     * 确保 PE_EN、SER 被清除并重新锁定 FMC。
     */
    fmc_unlock();
    fmc_flag_clear(BOARD_INTERNAL_FLASH_FMC_CLEAR_FLAGS);

    /*
     * 擦除前等待 FMC 空闲。
     */
    fmc_state = board_internal_flash_fmc_wait_ready_watchdog(BOARD_INTERNAL_FLASH_PAGE_ERASE_TIMEOUT_US, BOARD_INTERNAL_FLASH_WATCHDOG_FEED_INTERVAL_US);

    status = board_internal_flash_fmc_state_to_status(fmc_state);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        goto cleanup;
    }

    /*
     * GD32F470 4 KiB page erase register sequence。
     * page_address 已由纯地址解析路径确认：
     * - 属于 App/Backup/Staging；
     * - 4 KiB 对齐；
     * - 完整页未越出逻辑分区。
     */
    FMC_PEKEY = UNLOCK_PE_KEY;

    FMC_PECFG = FMC_PE_EN | (page_address & FMC_PE_ADDR);

    /*
    * 页擦除时 SN 必须为 0。
    */
    FMC_CTL &= ~FMC_CTL_SN;
    FMC_CTL |= FMC_CTL_SER;
    FMC_CTL |= FMC_CTL_START;

    /*
     * 等待本次页擦除完成。
     * 期间使用 TIMER1 判断实际时间，并定期刷新 FWDGT。
     */
    fmc_state = board_internal_flash_fmc_wait_ready_watchdog(BOARD_INTERNAL_FLASH_PAGE_ERASE_TIMEOUT_US, BOARD_INTERNAL_FLASH_WATCHDOG_FEED_INTERVAL_US);

    status = board_internal_flash_fmc_state_to_status(fmc_state);

cleanup:
    /*
     * 无论成功、FMC 错误还是超时，都清理操作控制位。
     */
    FMC_PECFG &= ~FMC_PE_EN;
    FMC_CTL &= ~(FMC_CTL_SER | FMC_CTL_SN);

    fmc_lock();

    /*
     * 只有 FMC 操作本身成功，才执行整页 0xFF 校验。
     * FMC 错误或超时后的页状态按未知处理，不做读回成功判断。
     */
    if (status == BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        status = board_internal_flash_page_erase_verify(page_address);
    }

    __set_PRIMASK(primask);

    return status;
    
}

board_internal_flash_status_t board_internal_flash_page_program(board_internal_flash_region_t region, uint32_t page_index, uint32_t page_offset, const uint8_t *data, uint32_t length)
{
    board_internal_flash_status_t status;
    fmc_state_enum fmc_state;
    uint32_t program_address;
    uint32_t address;
    uint32_t offset;
    uint32_t remaining;
    uint32_t word_data;
    uint16_t halfword_data;
    uint32_t primask;

    /*
     * 第一道门：
     * region/page_index/page_offset/length/data 全部先校验。
     */
    status = board_internal_flash_page_program_preflight(region, page_index, page_offset, data, length, &program_address);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return status;
    }

    /*
     * 第二道门：
     * 编程目标必须已经被擦除为 0xFF。
     */
    status = board_internal_flash_program_target_erased_check(program_address, length);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return status;
    }

    /*
     * 保留进入函数前的中断状态。
     * 当前 Boot 阶段在 FMC 编程期间暂时屏蔽中断，
     * 超时判断依赖 TIMER1 硬件 CNT，而不是 SysTick ISR。
     */
    primask = __get_PRIMASK();
    __disable_irq();

    fmc_unlock();

    fmc_flag_clear(BOARD_INTERNAL_FLASH_FMC_CLEAR_FLAGS);

    /*
     * 编程前确认 FMC 已经空闲。
     */
    fmc_state = board_internal_flash_fmc_wait_ready_watchdog(BOARD_INTERNAL_FLASH_PROGRAM_TIMEOUT_US, BOARD_INTERNAL_FLASH_WATCHDOG_FEED_INTERVAL_US);

    status = board_internal_flash_fmc_state_to_status(fmc_state);

    if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        goto cleanup;
    }

    /*
     * 按最大合法粒度编程：
     * 4 字节对齐且剩余不少于 4 字节时使用 word；
     * 否则 2 字节对齐且剩余不少于 2 字节时使用 halfword；
     * 最后使用 byte。
     */
    offset = 0U;

    while (offset < length)
    {
        address = program_address + offset;
        remaining = length - offset;

        if (((address & 0x3U) == 0U) && (remaining >= 4U))
        {
            word_data = ((uint32_t)data[offset]) |
                        ((uint32_t)data[offset + 1U] << 8U) |
                        ((uint32_t)data[offset + 2U] << 16U) |
                        ((uint32_t)data[offset + 3U] << 24U);

            status = board_internal_flash_program_unit(address, word_data, CTL_PSZ_WORD);

            if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
            {
                goto cleanup;
            }

            offset += 4U;
        }
        else if (((address & 0x1U) == 0U) && (remaining >= 2U))
        {
            halfword_data = (uint16_t)data[offset] | ((uint16_t)data[offset + 1U] << 8U);

            status = board_internal_flash_program_unit(address, (uint32_t)halfword_data, CTL_PSZ_HALF_WORD);

            if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
            {
                goto cleanup;
            }

            offset += 2U;
        }
        else
        {
            status = board_internal_flash_program_unit(address, (uint32_t)data[offset], CTL_PSZ_BYTE);

            if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
            {
                goto cleanup;
            }

            offset += 1U;
        }
    }

cleanup:
    /*
     * 无论成功、FMC 错误还是超时，
     * 都清除编程控制位并锁定 FMC。
     */
    FMC_CTL &= ~(FMC_CTL_PG | FMC_CTL_PSZ);

    fmc_lock();

    /*
     * 只有所有编程单元都成功，才执行最终读回校验。
     */
    if (status == BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        status = board_internal_flash_program_verify(program_address, data, length);
    }

    __set_PRIMASK(primask);

    return status;
}
