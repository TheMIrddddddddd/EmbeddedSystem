#include "gd32f4xx.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_fwdgt.h"

#include "systick.h"
#include "board_gpio.h"
#include "boot_jump.h"
#include "common_reset_contract.h"

#define BOOT_FWDGT_RELOAD      781U
#define BOOT_FWDGT_PRESCALER   FWDGT_PSC_DIV256
#define BOOT_WAIT_SECONDS      5U

volatile common_reset_reason_t g_boot_reset_reason;

static common_reset_reason_t boot_reset_reason_read(void)
{
    common_reset_reason_t reason;

    reason = RESET_REASON_UNKNOWN;

    if (SET == rcu_flag_get(RCU_FLAG_FWDGTRST)) {
        reason = RESET_REASON_IWDG;
    }
    else if (SET == rcu_flag_get(RCU_FLAG_SWRST)) {
        reason = RESET_REASON_SOFTWARE;
    }
    else if (SET == rcu_flag_get(RCU_FLAG_PORRST)) {
        reason = RESET_REASON_POWER_ON;
    }
    else if (SET == rcu_flag_get(RCU_FLAG_EPRST)) {
        reason = RESET_REASON_EXTERNAL;
    }
    else if (SET == rcu_flag_get(RCU_FLAG_LPRST)) {
        reason = RESET_REASON_LOW_POWER;
    }

    rcu_all_reset_flag_clear();

    return reason;
}

static uint8_t boot_fwdgt_init(void)
{
    if (fwdgt_config(BOOT_FWDGT_RELOAD, BOOT_FWDGT_PRESCALER) != SUCCESS) {
        return 0U;
    }

    fwdgt_counter_reload();

    return 1U;
}

static void boot_wait_and_indicate(void)
{
    uint32_t second;

    for (second = 0U; second < BOOT_WAIT_SECONDS;second++) {
        if ((second & 1U) == 0U) {
            board_led_on();
        }
        else {
            board_led_off();
        }

        fwdgt_counter_reload();
        delay_1ms(1000U);
    }

    board_led_off();
    fwdgt_counter_reload();
}

static void boot_error_loop(void)
{
    __disable_irq();

    for (;;) {
    }
}

int main(void)
{
    g_boot_reset_reason = boot_reset_reason_read();

    if (boot_fwdgt_init() == 0U) {
        boot_error_loop();
    }

    systick_config();
    board_led_init();
    __enable_irq();

    boot_wait_and_indicate();
    boot_jump_to_app();

    /* App 跳转失败时停留在 Bootloader。 */
    boot_error_loop();
}
