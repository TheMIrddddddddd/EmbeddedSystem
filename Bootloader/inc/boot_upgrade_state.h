#ifndef BOOT_UPGRADE_STATE_H
#define BOOT_UPGRADE_STATE_H

#include "boot_upgrade_meta.h"

/* 清理在线接收状态，供 BEGIN/END 失败路径复用。 */
boot_upgrade_meta_write_status_t boot_upgrade_abort_receiving(void);

/* 进入一轮接收，source 可为在线或 TF 离线。 */
boot_upgrade_meta_write_status_t boot_upgrade_meta_enter_receiving(
    uint32_t pending_size,
    uint32_t pending_crc32,
    uint32_t pending_version,
    upgrade_source_t source);

#endif /* BOOT_UPGRADE_STATE_H */
