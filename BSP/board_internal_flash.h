#ifndef BOARD_INTERNAL_FLASH_H
#define BOARD_INTERNAL_FLASH_H

#include <stdint.h>

/*
 * page_index 是所选逻辑区域内的 0-based 页索引。
 * 只允许操作 App、Backup、Staging。
 * Bootloader、Legacy Meta 保留区和内部 Flash 尾部保留区不属于本接口。
 */
typedef enum
{
    BOARD_INTERNAL_FLASH_REGION_APP = 0,
    BOARD_INTERNAL_FLASH_REGION_BACKUP,
    BOARD_INTERNAL_FLASH_REGION_STAGING,
    BOARD_INTERNAL_FLASH_REGION_COUNT
} board_internal_flash_region_t;

/*
 * 错误码属于 BSP 内部 Flash 接口层。
 * 不直接复用 FMC 的 fmc_state_enum，也不与后续协议错误码混用。
 */
typedef enum
{
    BOARD_INTERNAL_FLASH_STATUS_OK = 0,

    /* 参数和范围错误：不会解锁 FMC，也不会修改 Flash */
    BOARD_INTERNAL_FLASH_STATUS_INVALID_ARGUMENT = 1,
    BOARD_INTERNAL_FLASH_STATUS_INVALID_REGION = 2,
    BOARD_INTERNAL_FLASH_STATUS_PAGE_INDEX_OUT_OF_RANGE = 3,
    BOARD_INTERNAL_FLASH_STATUS_PAGE_OFFSET_OUT_OF_RANGE = 4,
    BOARD_INTERNAL_FLASH_STATUS_LENGTH_INVALID = 5,
    BOARD_INTERNAL_FLASH_STATUS_CROSS_PAGE = 6,
    BOARD_INTERNAL_FLASH_STATUS_TARGET_NOT_ERASED = 7,

    /* FMC 硬件状态错误 */
    BOARD_INTERNAL_FLASH_STATUS_FMC_RDDERR = 8,
    BOARD_INTERNAL_FLASH_STATUS_FMC_PGSERR = 9,
    BOARD_INTERNAL_FLASH_STATUS_FMC_PGMERR = 10,
    BOARD_INTERNAL_FLASH_STATUS_FMC_WPERR = 11,
    BOARD_INTERNAL_FLASH_STATUS_FMC_OPERR = 12,
    BOARD_INTERNAL_FLASH_STATUS_FMC_TIMEOUT = 13,
    BOARD_INTERNAL_FLASH_STATUS_FMC_UNEXPECTED = 14,

    /* 操作完成后的读回校验错误 */
    BOARD_INTERNAL_FLASH_STATUS_ERASE_VERIFY_FAILED = 15,
    BOARD_INTERNAL_FLASH_STATUS_PROGRAM_VERIFY_FAILED = 16,
    BOARD_INTERNAL_FLASH_STATUS_LAYOUT_INVALID = 17
} board_internal_flash_status_t;

/*
 * 擦除所选逻辑区域中的一个 4 KiB 页。
 *
 * page_index:
 *     APP:     0..31
 *     BACKUP:  0..31
 *     STAGING: 0..31
 *
 * 一次调用只允许擦除一个页，不允许内部扩展成物理 Sector 擦除。
 */
board_internal_flash_status_t board_internal_flash_page_erase(board_internal_flash_region_t region, uint32_t page_index);

/*
 * 在一个 4 KiB 页内部编程一段数据。
 *
 * page_offset + length 必须不超过一个页边界。
 * 本接口不负责擦除，目标字节必须已经是 0xFF。
 * page_offset 可以不是 4 字节对齐；实现按 word/halfword/byte
 * 选择合法编程粒度。
 *
 * data 必须指向调用期间保持有效的 RAM 数据，不得指向正在编程的
 * 内部 Flash 区域。
 *
 * 本接口不负责镜像 CRC，也不负责 manifest 生命周期。
 * manifest 只能在镜像正文校验成功后由上层最后写入。
 */
board_internal_flash_status_t board_internal_flash_page_program(board_internal_flash_region_t region, uint32_t page_index, uint32_t page_offset, const uint8_t *data, uint32_t length);

#endif /* BOARD_INTERNAL_FLASH_H */
