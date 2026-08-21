#ifndef COMMON_FLASH_LAYOUT_H
#define COMMON_FLASH_LAYOUT_H

#define INTERNAL_FLASH_BASE         0x08000000UL
#define INTERNAL_FLASH_SIZE         0x00080000UL

#define BOOT_BASE                   0x08000000UL
#define BOOT_SIZE                   0x00010000UL

#define META_SLOT_A_BASE            0x08010000UL
#define META_SLOT_B_BASE            0x08011000UL
#define META_SLOT_SIZE              0x00001000UL

#define APP_BASE                    0x08012000UL
#define APP_SIZE                    0x00020000UL

#define APP_MANIFEST_ADDR           0x08031FC0UL

#define BACKUP_BASE                 0x08032000UL
#define BACKUP_SIZE                 0x00020000UL

#define STAGING_BASE                0x08052000UL
#define STAGING_SIZE                0x00020000UL

#define MANIFEST_RESERVED_SIZE      64UL
#define MAX_IMAGE_SIZE              (128UL * 1024UL - MANIFEST_RESERVED_SIZE)
#define BACKUP_MANIFEST_ADDR        0x08051FC0UL
#define STAGING_MANIFEST_ADDR       0x08071FC0UL

/* 芯片 SRAM 范围上界(SRAM_BASE 由 gd32f4xx.h 提供,此处仅定义库没有的上界) */
#define SRAM_TOP                   0x20030000UL

#endif /* COMMON_FLASH_LAYOUT_H */
