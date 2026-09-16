#include "boot_upgrade_trial.h"

#include "gd32f4xx.h"
#include "gd32f4xx_fwdgt.h"
#include "gd32f4xx_pmu.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_rtc.h"

#include "boot_app_image.h"
#include "boot_upgrade_context.h"
#include "boot_upgrade_install.h"
#include "common_crash_marker.h"
#include "common_flash_layout.h"

#define BOOT_UPGRADE_TRIAL_FAILURE_LIMIT 3U

/*
 * CONFIRMED 提交成功后写入 RTC_BKP2；
 * 复位后的那次启动消费该标记，保持满进度显示。
 */
#define BOOT_UPGRADE_TRIAL_COMMIT_FLAG 0x00004D36UL

volatile boot_upgrade_trial_status_t g_boot_upgrade_trial_status =
    BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;
volatile common_reset_reason_t g_boot_upgrade_trial_reset_reason =
    RESET_REASON_UNKNOWN;
volatile uint32_t g_boot_upgrade_trial_crash_marker;
volatile uint8_t g_boot_upgrade_trial_crash_marker_valid;
volatile uint8_t g_boot_upgrade_trial_failure_count_before;
volatile uint8_t g_boot_upgrade_trial_failure_count_after;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_trial_meta_status =
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
volatile boot_upgrade_meta_slot_t g_boot_upgrade_trial_written_slot =
    BOOT_UPGRADE_META_SLOT_NONE;

static uint8_t s_boot_upgrade_trial_confirmed_attempted;
static boot_upgrade_trial_status_t s_boot_upgrade_trial_confirmed_last_status =
    BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;

static uint8_t boot_upgrade_trial_meta_context_valid(void)
{
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

    return 1U;
}

static void boot_upgrade_trial_crash_marker_read(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();
    rcu_periph_clock_enable(RCU_RTC);

    g_boot_upgrade_trial_crash_marker = RTC_BKP1;
    g_boot_upgrade_trial_crash_marker_valid =
        (RTC_BKP1 == COMMON_CRASH_MARKER_MAGIC) ? 1U : 0U;
}

static void boot_upgrade_trial_crash_marker_clear(void)
{
    RTC_BKP1 = 0U;
}

static void boot_upgrade_trial_backup_domain_prepare(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();
    rcu_periph_clock_enable(RCU_RTC);
}

void boot_upgrade_trial_commit_flag_set(void)
{
    boot_upgrade_trial_backup_domain_prepare();
    RTC_BKP2 = BOOT_UPGRADE_TRIAL_COMMIT_FLAG;
}

uint8_t boot_upgrade_trial_commit_flag_consume(void)
{
    uint8_t flag_present;

    boot_upgrade_trial_backup_domain_prepare();

    flag_present =
        (RTC_BKP2 == BOOT_UPGRADE_TRIAL_COMMIT_FLAG) ? 1U : 0U;

    if (flag_present != 0U)
    {
        RTC_BKP2 = 0U;
    }

    return flag_present;
}

static boot_upgrade_trial_status_t boot_upgrade_trial_meta_commit(
    const upgrade_meta_t *next_meta)
{
    upgrade_meta_t active_meta;
    upgrade_meta_t committed_meta;
    upgrade_meta_t selected_meta;
    boot_upgrade_meta_slot_t active_slot;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t selected_slot;
    boot_upgrade_meta_write_status_t write_status;
    boot_upgrade_meta_select_status_t select_status;

    g_boot_upgrade_trial_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_trial_written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    if ((next_meta == 0) ||
        (boot_upgrade_trial_meta_context_valid() == 0U))
    {
        return BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED;
    }

    active_slot = g_boot_meta_scan_slot;
    active_meta = g_boot_meta_selected;
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        next_meta,
        &committed_meta,
        &written_slot
    );
    g_boot_upgrade_trial_meta_status = write_status;
    g_boot_upgrade_trial_written_slot = written_slot;

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        return BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED;
    }

    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_boot_upgrade_trial_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        return BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;

    return BOOT_UPGRADE_TRIAL_STATUS_CONFIRMED_COMMITTED;
}

static boot_upgrade_trial_status_t boot_upgrade_trial_failure_count_update(void)
{
    upgrade_meta_t next_meta;
    boot_upgrade_trial_status_t status;

    next_meta = g_boot_meta_selected;
    g_boot_upgrade_trial_failure_count_before =
        g_boot_meta_selected.failure_count;

    if (g_boot_upgrade_trial_failure_count_before < 0xFFU)
    {
        g_boot_upgrade_trial_failure_count_after =
            (uint8_t)(g_boot_upgrade_trial_failure_count_before + 1U);
    }
    else
    {
        g_boot_upgrade_trial_failure_count_after = 0xFFU;
    }

    next_meta.failure_count = g_boot_upgrade_trial_failure_count_after;

    if (g_boot_upgrade_trial_failure_count_after >=
        BOOT_UPGRADE_TRIAL_FAILURE_LIMIT)
    {
        next_meta.state = FW_STATE_ROLLBACK_REQUIRED;
        next_meta.install_stage = INSTALL_APP_VALID;
    }

    status = boot_upgrade_trial_meta_commit(&next_meta);

    if (status == BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED)
    {
        g_boot_upgrade_trial_status = status;
        return status;
    }

    if (g_boot_upgrade_trial_failure_count_after >=
        BOOT_UPGRADE_TRIAL_FAILURE_LIMIT)
    {
        g_boot_upgrade_trial_status =
            BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
        return BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
    }

    g_boot_upgrade_trial_status =
        BOOT_UPGRADE_TRIAL_STATUS_FAILURE_RECORDED;
    return BOOT_UPGRADE_TRIAL_STATUS_FAILURE_RECORDED;
}

static boot_upgrade_trial_status_t
boot_upgrade_trial_mark_rollback_required(void)
{
    upgrade_meta_t next_meta;
    boot_upgrade_trial_status_t status;

    if (g_boot_meta_selected.state == FW_STATE_ROLLBACK_REQUIRED)
    {
        g_boot_upgrade_trial_status =
            BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
        return BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
    }

    next_meta = g_boot_meta_selected;
    next_meta.state = FW_STATE_ROLLBACK_REQUIRED;
    next_meta.install_stage = INSTALL_APP_VALID;

    status = boot_upgrade_trial_meta_commit(&next_meta);

    if (status == BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED)
    {
        g_boot_upgrade_trial_status = status;
        return status;
    }

    g_boot_upgrade_trial_status =
        BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
    return BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
}

boot_upgrade_trial_status_t boot_upgrade_trial_startup_process(
    common_reset_reason_t reset_reason)
{
    g_boot_upgrade_trial_reset_reason = reset_reason;
    g_boot_upgrade_trial_status = BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;
    g_boot_upgrade_trial_crash_marker = 0U;
    g_boot_upgrade_trial_crash_marker_valid = 0U;
    g_boot_upgrade_trial_failure_count_before = 0U;
    g_boot_upgrade_trial_failure_count_after = 0U;
    g_boot_upgrade_trial_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_trial_written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    /* 先读取标记，等对应 Meta 成功落盘后再清除。 */
    boot_upgrade_trial_crash_marker_read();

    if (boot_upgrade_trial_meta_context_valid() == 0U)
    {
        boot_upgrade_trial_crash_marker_clear();
        return BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;
    }

    if (g_boot_meta_selected.state != FW_STATE_TRIAL_PENDING)
    {
        boot_upgrade_trial_crash_marker_clear();
        return BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;
    }

    g_boot_upgrade_trial_failure_count_before =
        g_boot_meta_selected.failure_count;

    if (g_boot_meta_selected.failure_count >=
        BOOT_UPGRADE_TRIAL_FAILURE_LIMIT)
    {
        upgrade_meta_t next_meta;

        next_meta = g_boot_meta_selected;
        next_meta.state = FW_STATE_ROLLBACK_REQUIRED;
        next_meta.install_stage = INSTALL_APP_VALID;

        if (boot_upgrade_trial_meta_commit(&next_meta) ==
            BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED)
        {
            g_boot_upgrade_trial_status =
                BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED;
            return BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED;
        }

        boot_upgrade_trial_crash_marker_clear();
        g_boot_upgrade_trial_status =
            BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
        return BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED;
    }

    if ((g_boot_upgrade_trial_crash_marker_valid != 0U) ||
        (reset_reason == RESET_REASON_IWDG))
    {
        boot_upgrade_trial_status_t status;

        status = boot_upgrade_trial_failure_count_update();
        if (status != BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED)
        {
            boot_upgrade_trial_crash_marker_clear();
        }
        return status;
    }

    g_boot_upgrade_trial_status = BOOT_UPGRADE_TRIAL_STATUS_APP_ALLOWED;
    return BOOT_UPGRADE_TRIAL_STATUS_APP_ALLOWED;
}

static boot_upgrade_trial_status_t boot_upgrade_trial_confirmed_commit(void)
{
    boot_app_image_info_t app_info;
    upgrade_meta_t next_meta;
    boot_upgrade_trial_status_t status;

    if (boot_app_image_validate(&app_info) != BOOT_APP_IMAGE_STATUS_OK)
    {
        return boot_upgrade_trial_mark_rollback_required();
    }

    if ((g_boot_meta_selected.pending_size == 0U) ||
        (g_boot_meta_selected.pending_size > MAX_IMAGE_SIZE) ||
        (app_info.manifest.image_size != g_boot_meta_selected.pending_size) ||
        (app_info.manifest.image_crc32 != g_boot_meta_selected.pending_crc32) ||
        (app_info.manifest.image_version != g_boot_meta_selected.pending_version))
    {
        return boot_upgrade_trial_mark_rollback_required();
    }

    next_meta = g_boot_meta_selected;
    next_meta.active_size = next_meta.pending_size;
    next_meta.active_crc32 = next_meta.pending_crc32;
    next_meta.active_version = next_meta.pending_version;
    next_meta.pending_size = 0U;
    next_meta.pending_crc32 = 0U;
    next_meta.pending_version = 0U;
    next_meta.upgrade_source =
        (g_boot_meta_selected.upgrade_source == UPGRADE_SOURCE_TF_OFFLINE) ?
        UPGRADE_SOURCE_TF_OFFLINE : UPGRADE_SOURCE_NONE;
    next_meta.state = FW_STATE_IDLE;
    next_meta.install_stage = INSTALL_APP_VALID;
    next_meta.failure_count = 0U;
    next_meta.backup_size = 0U;
    next_meta.backup_crc32 = 0U;
    next_meta.backup_version = 0U;

    if ((next_meta.failed_package_crc32 == g_boot_meta_selected.pending_crc32) &&
        (next_meta.failed_package_version == g_boot_meta_selected.pending_version))
    {
        next_meta.failed_package_crc32 = 0U;
        next_meta.failed_package_version = 0U;
    }

    status = boot_upgrade_trial_meta_commit(&next_meta);

    if (status == BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED)
    {
        g_boot_upgrade_trial_status = status;
        return status;
    }

    g_boot_upgrade_trial_status =
        BOOT_UPGRADE_TRIAL_STATUS_CONFIRMED_COMMITTED;
    fwdgt_counter_reload();

    /*
     * 提交完成后重新启动，统一从 IDLE 路径启动已确认 App。
     * 复位前留下标记，让下一次启动继续显示 100% 进度。
     */
    boot_upgrade_trial_commit_flag_set();
    NVIC_SystemReset();

    return BOOT_UPGRADE_TRIAL_STATUS_CONFIRMED_COMMITTED;
}

boot_upgrade_trial_status_t boot_upgrade_trial_process(void)
{
    if ((g_boot_meta_selected.state == FW_STATE_TRIAL_PENDING) &&
        (g_boot_app_image_status != BOOT_APP_IMAGE_STATUS_OK))
    {
        return boot_upgrade_trial_mark_rollback_required();
    }

    if (g_boot_meta_selected.state != FW_STATE_CONFIRMED)
    {
        g_boot_upgrade_trial_status = BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;
        return BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION;
    }

    /* Meta 写失败时等下一次整机复位再重试，避免安全循环高频擦写 Slot。 */
    if (s_boot_upgrade_trial_confirmed_attempted != 0U)
    {
        g_boot_upgrade_trial_status =
            s_boot_upgrade_trial_confirmed_last_status;
        return s_boot_upgrade_trial_confirmed_last_status;
    }

    s_boot_upgrade_trial_confirmed_attempted = 1U;
    s_boot_upgrade_trial_confirmed_last_status =
        boot_upgrade_trial_confirmed_commit();

    return s_boot_upgrade_trial_confirmed_last_status;
}
