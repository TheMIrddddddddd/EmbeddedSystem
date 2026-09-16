#ifndef BOOT_UPGRADE_STAGING_H
#define BOOT_UPGRADE_STAGING_H

#include <stdint.h>

#include "board_internal_flash.h"

/*
 * 准备一次新的在线接收：
 * 逐页擦除整个 Staging 分区，并由内部 Flash BSP 完成整页读回校验。
 */
board_internal_flash_status_t boot_upgrade_staging_prepare(void);

extern volatile uint32_t g_boot_upgrade_staging_prepare_page_index;
extern volatile uint32_t g_boot_upgrade_staging_prepare_completed_pages;
extern volatile board_internal_flash_status_t g_boot_upgrade_staging_prepare_status;
extern volatile uint32_t g_boot_upgrade_staging_received_length;
extern volatile uint16_t g_boot_upgrade_staging_expected_sequence;

#endif /* BOOT_UPGRADE_STAGING_H */
