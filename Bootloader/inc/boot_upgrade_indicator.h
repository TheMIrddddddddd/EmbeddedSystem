#ifndef BOOT_UPGRADE_INDICATOR_H
#define BOOT_UPGRADE_INDICATOR_H

#include <stdint.h>

#include "upgrade_serialization.h"

typedef enum
{
    BOOT_UPGRADE_INDICATOR_STATUS_DISABLED = 0,
    BOOT_UPGRADE_INDICATOR_STATUS_OK,
    BOOT_UPGRADE_INDICATOR_STATUS_I2C_INIT_FAILED,
    BOOT_UPGRADE_INDICATOR_STATUS_OLED_INIT_FAILED,
    BOOT_UPGRADE_INDICATOR_STATUS_REFRESH_FAILED
} boot_upgrade_indicator_status_t;

/* OLED 不可用时只记录状态，不能阻断 Bootloader 升级流程。 */
void boot_upgrade_indicator_init(void);

/* percent 的有效范围为 0~100，超范围值会被钳位。 */
void boot_upgrade_indicator_set_progress(uint8_t percent);

/* 将 INSTALL 子阶段映射为 95%~100% 的进度。 */
void boot_upgrade_indicator_set_install_stage(install_stage_t stage);

/* 跳转 App 前清空 Bootloader 的显示内容。 */
void boot_upgrade_indicator_shutdown(void);

extern volatile boot_upgrade_indicator_status_t
    g_boot_upgrade_indicator_status;
extern volatile uint8_t g_boot_upgrade_indicator_oled_ready;
extern volatile uint8_t g_boot_upgrade_indicator_progress;

#endif /* BOOT_UPGRADE_INDICATOR_H */
