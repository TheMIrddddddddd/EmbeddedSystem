#include "gd32f4xx.h"
#include "common_flash_layout.h"
#include "boot_jump.h"

typedef void (*app_func_t)(void);

void boot_jump_to_app(void)
{
    uint32_t msp = *(volatile uint32_t*)APP_BASE;
    uint32_t reset_addr = *(volatile uint32_t*)(APP_BASE + 4U);

    /* ⑥ 8 字节对齐下首次压栈不越界的最小栈顶, 8 字节对齐检查 */
    if ((msp < SRAM_BASE + 8U) || (msp > SRAM_TOP) || ((msp & 0x7U) != 0U))
    {
        return;
    }
    if ((reset_addr < APP_BASE) || (reset_addr >= APP_MANIFEST_ADDR) || (0U == (reset_addr & 1U)))
    {
        return; /* Reset_Handler 不在合法 App 区域或不是 Thumb 地址 */
    }

    /* 关闭全局中断 */
    __disable_irq();

    /* 停止并关闭 SysTick */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    /* 清除 SysTick 挂起状态 */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* 反初始化 Bootloader 使用的外设(当前只有 LED 的 GPIOD) */
    rcu_periph_clock_disable(RCU_GPIOD);

    /* 禁止并清除全部 NVIC 已使能中断 */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* 清除外设挂起标志:当前无外设中断,留空 */

    /* 设置 VTOR-设置 MSP-跳转复位向量 */
    SCB->VTOR = APP_BASE;

    __DSB();
    __ISB();

    __set_MSP(msp);
    ((app_func_t)reset_addr)();

    /* App 的 Reset_Handler 不应返回；若异常返回,驻留于此,
     * 防止在已切换的 App 栈上执行函数返回序列 */
    while (1)
    {
    }
    
}
