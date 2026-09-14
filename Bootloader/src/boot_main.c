#include "gd32f4xx.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_fwdgt.h"

#include "systick.h"
#include "board_gpio.h"
#include "board_spi_flash.h"
#include "boot_upgrade_meta.h"
#include "boot_jump.h"
#include "boot_app_image.h"
#include "boot_upgrade_begin.h"
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

static void boot_upgrade_meta_bootstrap_from_app(void)
{
    upgrade_meta_t initial_meta;
    upgrade_meta_t selected_meta;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t selected_slot;
    boot_upgrade_meta_select_status_t select_status;
    boot_upgrade_meta_write_status_t write_status;

    /*
     * 只有以下条件同时满足，才允许初始化外部 Meta：
     *
     * 1. GD25Q40E JEDEC ID 已确认；
     * 2. 当前没有任何有效 Meta 槽；
     * 3. 当前 App manifest、App CRC、MSP、Reset_Handler 均有效。
     */
    if ((g_boot_meta_jedec_valid == 0U) ||
        (g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_NO_VALID_SLOT) ||
        (g_boot_app_image_status != BOOT_APP_IMAGE_STATUS_OK))
    {
        return;
    }

    /*
     * Slot A 初始 Meta。
     *
     * 这里逐字段赋值，不能使用 memcpy 结构体作为持久化格式。
     * upgrade_meta_encode() 会按照固定 72 字节 ABI 重新序列化。
     */
    initial_meta.magic = UPGRADE_META_MAGIC;
    initial_meta.meta_version = UPGRADE_META_VERSION;
    initial_meta.generation = 1U;

    initial_meta.state = FW_STATE_IDLE;
    initial_meta.install_stage = INSTALL_APP_VALID;
    initial_meta.failure_count = 0U;
    initial_meta.upgrade_source = UPGRADE_SOURCE_NONE;

    /*
     * 当前正在运行的 App 来自 App manifest。
     */
    initial_meta.active_size = g_boot_app_manifest.image_size;
    initial_meta.active_crc32 = g_boot_app_manifest.image_crc32;
    initial_meta.active_version = g_boot_app_manifest.image_version;

    /*
     * 初始状态下没有 Backup、Pending 和失败包。
     */
    initial_meta.backup_size = 0U;
    initial_meta.backup_crc32 = 0U;
    initial_meta.backup_version = 0U;

    initial_meta.pending_size = 0U;
    initial_meta.pending_crc32 = 0U;
    initial_meta.pending_version = 0U;

    initial_meta.failed_package_crc32 = 0U;
    initial_meta.failed_package_version = 0U;

    initial_meta.request = UPGRADE_META_REQUEST_NONE;

    /*
     * crc32 由 upgrade_meta_encode() 重新计算。
     */
    initial_meta.crc32 = 0U;
    initial_meta.commit_marker = UPGRADE_META_COMMIT_MARKER;

    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    /*
     * active_slot = NONE 时，写入目标固定为 Slot A。
     */
    write_status = boot_upgrade_meta_write_inactive(BOOT_UPGRADE_META_SLOT_NONE, &initial_meta, &written_slot);

    /*
     * 必须确认：
     * 1. 整个写入流程成功；
     * 2. 实际写入槽确实是 Slot A。
     */
    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) || (written_slot != BOOT_UPGRADE_META_SLOT_A))
    {
        /*
         * App 本身有效时，即使 Meta 重建失败，也允许继续启动 App。
         * 具体失败原因通过：
         *
         * g_boot_upgrade_meta_write_status
         *
         * 观察。
         */
        return;
    }

    /*
     * writer 已经完成整条记录读回和 decode 校验。
     * 先更新当前启动上下文。
     */
    g_boot_meta_selected = initial_meta;
    g_boot_meta_scan_slot = written_slot;
    g_boot_meta_scan_status = BOOT_UPGRADE_META_SELECT_OK_DEGRADED;

    /*
     * 再次扫描 A/B，确认启动上下文和 Flash 中实际记录一致。
     *
     * 此时预期：
     * Slot A = VALID
     * Slot B = EMPTY 或 INVALID_RECORD
     * 选择结果 = OK_DEGRADED
     */
    select_status = boot_upgrade_meta_select(&selected_meta, &selected_slot);

    if ((select_status == BOOT_UPGRADE_META_SELECT_OK) || (select_status == BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_boot_meta_selected = selected_meta;
        g_boot_meta_scan_slot = selected_slot;
        g_boot_meta_scan_status = select_status;
    }
}

static uint8_t boot_meta_recover_receiving(void)
{
    upgrade_meta_t active_meta;
    upgrade_meta_t next_meta;
    upgrade_meta_t committed_meta;
    upgrade_meta_t selected_meta;

    boot_upgrade_meta_slot_t active_slot;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t selected_slot;

    boot_upgrade_meta_select_status_t select_status;
    boot_upgrade_meta_write_status_t write_status;

    /*
     * 只有当前存在有效 Meta 时，才能恢复 RECEIVING。
     */
    if ((g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return 0U;
    }

    if ((g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        return 0U;
    }

    active_slot = g_boot_meta_scan_slot;

    /*
     * g_boot_meta_selected 是启动时已经选中的有效 Meta。
     */
    active_meta = g_boot_meta_selected;
    next_meta = active_meta;

    /*
     * RECEIVING 表示升级接收没有完成。
     * 当前阶段不能继续使用 pending 内容，
     * 直接回到当前有效 App 的 IDLE 状态。
     */
    next_meta.state = FW_STATE_IDLE;
    next_meta.install_stage = INSTALL_APP_VALID;

    /*
     * 清除未完成的升级内容标记。
     */
    next_meta.pending_size = 0U;
    next_meta.pending_crc32 = 0U;
    next_meta.pending_version = 0U;
    next_meta.upgrade_source = UPGRADE_SOURCE_NONE;

    /*
     * generation、crc32、commit_marker
     * 由 boot_upgrade_meta_update() 统一处理。
     */
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );

    /*
     * 必须写入另一个槽。
     */
    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        return 0U;
    }

    /*
     * 先使用 update 接口读回的完整记录更新当前上下文。
     */
    g_boot_meta_selected = committed_meta;
    g_boot_meta_scan_slot = written_slot;
    g_boot_meta_scan_status = BOOT_UPGRADE_META_SELECT_OK;

    /*
     * 再次扫描 A/B，确认实际选择结果。
     */
    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return 0U;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;

    return 1U;
}

static boot_upgrade_meta_write_status_t boot_meta_enter_receiving(
    uint32_t pending_size,
    uint32_t pending_crc32,
    uint32_t pending_version,
    upgrade_source_t source)
{
    upgrade_meta_t active_meta;
    upgrade_meta_t next_meta;
    upgrade_meta_t committed_meta;
    upgrade_meta_t selected_meta;

    boot_upgrade_meta_slot_t active_slot;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t selected_slot;

    boot_upgrade_meta_write_status_t write_status;
    boot_upgrade_meta_select_status_t select_status;

    /*
     * pending_size 必须是合法 App 映像长度。
     */
    if ((pending_size == 0U) ||
        (pending_size > MAX_IMAGE_SIZE))
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    }

    /*
     * 当前只允许在线升级或 TF 离线升级来源。
     */
    if ((source != UPGRADE_SOURCE_ONLINE) &&
        (source != UPGRADE_SOURCE_TF_OFFLINE))
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    }

    /*
     * 进入 RECEIVING 必须从有效 Meta 开始。
     */
    if ((g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_SLOT;
    }

    if ((g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_SLOT;
    }

    /*
     * 只有 IDLE 状态允许开始一次新的接收。
     * 防止在 RECEIVING、INSTALLING 等状态重复 BEGIN。
     */
    if (g_boot_meta_selected.state != FW_STATE_IDLE)
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    }

    active_slot = g_boot_meta_scan_slot;

    /*
     * 复制当前有效 Meta。
     */
    active_meta = g_boot_meta_selected;
    next_meta = active_meta;

    /*
     * 设置新的接收状态。
     */
    next_meta.state = FW_STATE_RECEIVING;

    /*
     * 当前 App 仍然是有效活动镜像，
     * 所以 install_stage 保持为 APP_VALID。
     */
    next_meta.install_stage = INSTALL_APP_VALID;

    /*
     * 保存待接收映像信息。
     */
    next_meta.pending_size = pending_size;
    next_meta.pending_crc32 = pending_crc32;
    next_meta.pending_version = pending_version;
    next_meta.upgrade_source = source;

    /*
     * generation、crc32、commit_marker
     * 由 boot_upgrade_meta_update() 统一处理。
     */
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        return write_status;
    }

    /*
     * 再次扫描 A/B，确认新状态已经成为当前有效记录。
     */
    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;

    return BOOT_UPGRADE_META_WRITE_OK;
}

boot_upgrade_meta_write_status_t boot_upgrade_begin_accept(
    const uint8_t *header_raw,
    uint32_t header_length)
{
    firmware_header_t header;
    fw_format_status_t header_status;

    if ((header_raw == 0) ||
        (header_length != FIRMWARE_HEADER_SIZE))
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    }

    header_status = firmware_header_decode(header_raw, header_length, &header);

    if (header_status != FW_FORMAT_STATUS_OK)
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    }

    return boot_meta_enter_receiving(header.image_size, header.image_crc32, header.firmware_version, UPGRADE_SOURCE_ONLINE);
}

static uint8_t boot_meta_startup_state_dispatch(void)
{
    /*
     * 外部 Meta 不可用或没有有效槽时，
     * 只要 App 已经通过 manifest/CRC/向量表校验，
     * 当前阶段允许降级启动 App。
     *
     * 后续可以在这里增加 Backup manifest 恢复和
     * 外部 Meta 重建失败记录。
     */
    if ((g_boot_meta_scan_status == BOOT_UPGRADE_META_SELECT_EXTERNAL_IO_ERROR) ||
        (g_boot_meta_scan_status == BOOT_UPGRADE_META_SELECT_NO_VALID_SLOT))
    {
        return 1U;
    }

    /*
     * 只有有效选择结果才进入状态分派。
     */
    if ((g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return 0U;
    }

    switch (g_boot_meta_selected.state)
    {
        case FW_STATE_IDLE:
            /*
             * 当前版本已经实现：
             * App manifest 校验 + 允许跳转。
             */
            return 1U;

        case FW_STATE_RECEIVING:
            /*
            * 接收过程中掉电：
            * 丢弃未完成的 pending，
            * 恢复当前有效 App。
            */
            return boot_meta_recover_receiving();

        case FW_STATE_STAGED_VALID:
            /*
             * 尚未接入 ONLINE INSTALL/ABORT 和
             * TF 自动安装分派。
             */
            return 0U;

        case FW_STATE_INSTALLING:
            /*
             * 尚未接入 BACKUP_START、
             * BACKUP_VALID、APP_ERASING、
             * APP_PROGRAMMING、APP_VALID 恢复矩阵。
             */
            return 0U;

        case FW_STATE_TRIAL_PENDING:
            /*
             * 尚未接入 App 启动确认和失败计数。
             */
            return 0U;

        case FW_STATE_CONFIRMED:
            /*
             * 尚未接入 CONFIRMED → IDLE 的完整提交路径。
             */
            return 0U;

        case FW_STATE_ROLLBACK_REQUIRED:
            /*
             * 尚未接入 Backup 回滚。
             */
            return 0U;

        case FW_STATE_ROLLED_BACK:
            /*
             * 尚未接入回滚完成后的清理和重新启动。
             */
            return 0U;

        default:
            /*
             * 未知状态不能直接跳转 App。
             */
            return 0U;
    }
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
    /*
    * 只有有效 App 且没有有效外部 Meta 时，
    * 才根据 App manifest 初始化 Slot A。
    */
    boot_upgrade_meta_bootstrap_from_app();

    /*
    * 根据当前 Meta 状态决定是否允许进入 App。
    */
    if (boot_meta_startup_state_dispatch() == 0U)
    {
        boot_safe_upgrade_loop();
    }

    boot_wait_and_indicate();
    boot_jump_to_app();

    /* App 跳转失败时停留在 Bootloader。 */
    boot_error_loop();
}
