#ifndef BOOT_UPGRADE_CONTEXT_H
#define BOOT_UPGRADE_CONTEXT_H

#include "boot_upgrade_meta.h"

/* Boot 启动阶段选中的外部 Meta 上下文。 */
extern volatile upgrade_meta_t g_boot_meta_selected;
extern volatile boot_upgrade_meta_slot_t g_boot_meta_scan_slot;
extern volatile boot_upgrade_meta_select_status_t g_boot_meta_scan_status;

#endif /* BOOT_UPGRADE_CONTEXT_H */
