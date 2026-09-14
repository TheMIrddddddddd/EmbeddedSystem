#include "gd32f4xx.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_fwdgt.h"

#include "systick.h"
#include "board_gpio.h"
#include "board_spi_flash.h"
#include "boot_upgrade_meta.h"
#include "boot_jump.h"
#include "boot_app_image.h"
#include "common_reset_contract.h"

#define BOOT_FWDGT_RELOAD      781U
#define BOOT_FWDGT_PRESCALER   FWDGT_PSC_DIV256
#define BOOT_WAIT_SECONDS      5U

volatile common_reset_reason_t g_boot_reset_reason;
volatile uint8_t g_boot_meta_jedec_id[3];
volatile uint8_t g_boot_meta_jedec_valid;

volatile upgrade_meta_t g_boot_meta_selected;
volatile boot_upgrade_meta_slot_t g_boot_meta_scan_slot;
volatile boot_upgrade_meta_select_status_t g_boot_meta_scan_status;

static void boot_upgrade_meta_scan_at_startup(void)
{
    uint8_t jedec_id[3];
    upgrade_meta_t selected_meta;
    boot_upgrade_meta_slot_t selected_slot;

    g_boot_meta_jedec_id[0] = 0U;
    g_boot_meta_jedec_id[1] = 0U;
    g_boot_meta_jedec_id[2] = 0U;
    g_boot_meta_jedec_valid = 0U;

    g_boot_meta_scan_slot = BOOT_UPGRADE_META_SLOT_NONE;
    g_boot_meta_scan_status = BOOT_UPGRADE_META_SELECT_EXTERNAL_IO_ERROR;

    if (board_spi_flash_init() == 0)
    {
        return;
    }

    if (board_spi_flash_read_jedec_id(jedec_id) == 0)
    {
        return;
    }

    g_boot_meta_jedec_id[0] = jedec_id[0];
    g_boot_meta_jedec_id[1] = jedec_id[1];
    g_boot_meta_jedec_id[2] = jedec_id[2];

    if ((jedec_id[0] != 0xC8U) ||
        (jedec_id[1] != 0x40U) ||
        (jedec_id[2] != 0x13U))
    {
        return;
    }

    g_boot_meta_jedec_valid = 1U;

    g_boot_meta_scan_status = boot_upgrade_meta_select(&selected_meta, &selected_slot);

    g_boot_meta_scan_slot = selected_slot;

    if ((g_boot_meta_scan_status == BOOT_UPGRADE_META_SELECT_OK) ||
        (g_boot_meta_scan_status == BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_boot_meta_selected = selected_meta;
    }
}

static boot_app_image_status_t boot_app_image_check_at_startup(void)
{
    boot_app_image_info_t app_info;

    return boot_app_image_validate(&app_info);
}

static void boot_spi_flash_wait_feed(void)
{
    fwdgt_counter_reload();
}

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

static void boot_safe_upgrade_loop(void)
{
    uint32_t heartbeat;

    heartbeat = 0U;

    /*
     * 当前阶段是安全升级模式的驻留骨架。
     * 后续在循环内部接入升级接收和 0x0501 BEGIN 服务。
     *
     * 这里必须保持中断开启，不能调用 __disable_irq()。
     */

    for (;;)
    {
        fwdgt_counter_reload();

        if ((heartbeat & 1U) == 0U)
        {
            board_led_on();
        }
        else
        {
            board_led_off();
        }

        heartbeat++;

        /*
         * 当前没有正式的升级服务函数，
         * 先保持 Bootloader 驻留并喂狗。
         */
        delay_1ms(500U);
    }

}

int main(void)
{
    g_boot_reset_reason = boot_reset_reason_read();

    if (boot_fwdgt_init() == 0U) {
        boot_error_loop();
    }

    board_spi_flash_set_wait_hook(boot_spi_flash_wait_feed);

    systick_config();
    board_led_init();
    __enable_irq();

    boot_upgrade_meta_scan_at_startup();
    if (boot_app_image_check_at_startup() != BOOT_APP_IMAGE_STATUS_OK)
    {
        boot_safe_upgrade_loop();
    }

    boot_wait_and_indicate();
    boot_jump_to_app();

    /* App 跳转失败时停留在 Bootloader。 */
    boot_error_loop();
}
