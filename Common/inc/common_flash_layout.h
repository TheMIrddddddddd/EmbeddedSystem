#ifndef COMMON_FLASH_LAYOUT_H
#define COMMON_FLASH_LAYOUT_H

#define INTERNAL_FLASH_BASE                             0x08000000UL
#define INTERNAL_FLASH_SIZE                             0x00080000UL

#define BOOT_BASE                                       0x08000000UL
#define BOOT_SIZE                                       0x00010000UL

#define INTERNAL_FLASH_PAGE_SIZE                        0x00001000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_BASE        0x08010000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_SIZE        0x00002000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_SLOT_SIZE   0x00001000UL

#define INTERNAL_FLASH_LEGACY_META_RESERVED_A_BASE      0x08010000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_B_BASE      0x08011000UL

#define GD25Q40E_CAPACITY                               0x00080000UL
#define GD25Q40E_SECTOR_SIZE                            0x00001000UL

#define GD25Q40E_BUSINESS_KV_SLOT_A_OFFSET              0x00000000UL
#define GD25Q40E_BUSINESS_KV_SLOT_B_OFFSET              0x00001000UL

#define GD25Q40E_UPGRADE_META_SLOT_A_OFFSET             0x00002000UL
#define GD25Q40E_UPGRADE_META_SLOT_B_OFFSET             0x00003000UL
#define GD25Q40E_UPGRADE_META_SLOT_SIZE                 0x00001000UL
#define GD25Q40E_DIAGNOSTIC_SECTOR_OFFSET               0x0007F000UL

#define APP_BASE                                        0x08012000UL
#define APP_SIZE                                        0x00020000UL
#define APP_END                                         (APP_BASE + APP_SIZE - 1UL)

#define BACKUP_BASE                                     0x08032000UL
#define BACKUP_SIZE                                     0x00020000UL

#define STAGING_BASE                                    0x08052000UL
#define STAGING_SIZE                                    0x00020000UL

#define MANIFEST_RESERVED_SIZE                          64UL
#define APP_MANIFEST_ADDR                               (APP_BASE + APP_SIZE - MANIFEST_RESERVED_SIZE)
#define MAX_IMAGE_SIZE                                  (APP_SIZE - MANIFEST_RESERVED_SIZE)
#define BACKUP_MANIFEST_ADDR                            (BACKUP_BASE + BACKUP_SIZE - MANIFEST_RESERVED_SIZE)
#define STAGING_MANIFEST_ADDR                           (STAGING_BASE + STAGING_SIZE - MANIFEST_RESERVED_SIZE)

/* 芯片 SRAM 范围上界(SRAM_BASE 由 gd32f4xx.h 提供,此处仅定义库没有的上界) */
#define SRAM_TOP                                        0x20030000UL

#endif /* COMMON_FLASH_LAYOUT_H */
