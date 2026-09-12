#include "ff.h"
#include "diskio.h"
#include "board_sdio.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DISKIO_SDIO_PDRV        0U

static DSTATUS s_disk_sdio_status = STA_NOINIT;
static uint16_t s_disk_sdio_rca = 0;

/*
 * FatFs 传入的扇区缓冲区不保证 4 字节对齐，而 SDIO DMA 使用 32 位传输。
 * StorageTask 是当前 FatFs 唯一所有者，因此一个静态 bounce buffer 足够，
 * 且不会引入动态内存或跨任务共享。
 */
#if defined(__CC_ARM)

__align(4)
static uint8_t s_disk_sdio_dma_bounce_buffer[BOARD_SDIO_BLOCK_SIZE];

#elif defined(__GNUC__)

static uint8_t s_disk_sdio_dma_bounce_buffer[BOARD_SDIO_BLOCK_SIZE]
    __attribute__((aligned(4)));

#else

static uint8_t s_disk_sdio_dma_bounce_buffer[BOARD_SDIO_BLOCK_SIZE];

#endif

static uint8_t diskio_sdio_buffer_aligned(const void *buffer)
{
    return ((((uintptr_t)buffer) & 0x03U) == 0U) ? 1U : 0U;
}

/* 将 BSP 的 SDIO 状态转换成 FatFs 的 DRESULT */
static DRESULT diskio_sdio_status_to_result(board_sdio_status_t status)
{
    switch (status)
    {
        case BOARD_SDIO_STATUS_OK:
            return RES_OK;
        
        case BOARD_SDIO_STATUS_INVALID_ARGUMENT:
            return RES_PARERR;

        case BOARD_SDIO_STATUS_NO_CARD:
        case BOARD_SDIO_STATUS_NOT_READY:
        case BOARD_SDIO_STATUS_BUSY:
            return RES_NOTRDY;
        
        case BOARD_SDIO_STATUS_TIMEOUT:
        case BOARD_SDIO_STATUS_COMMAND_ERROR:
        case BOARD_SDIO_STATUS_DATA_ERROR:
        default:
            return RES_ERROR;
    }
}

/* StorageTask 完成卡初始化后调用 */
void diskio_sdio_set_ready(uint16_t rca)
{
    if (rca == 0)
    {
        s_disk_sdio_rca = 0U;
        s_disk_sdio_status = STA_NOINIT;
        return;
    }
    s_disk_sdio_rca = rca;
    s_disk_sdio_status = 0U;
}

/* 无卡或初始化失败时调用 */
void diskio_sdio_set_not_ready(void)
{
    s_disk_sdio_rca = 0U;
    s_disk_sdio_status = STA_NOINIT;
}

/* 检查当前 TF 卡是否可以访问 */
static DRESULT diskio_sdio_ready_check(void)
{
    DSTATUS status;

    status = disk_status(DISKIO_SDIO_PDRV);

    if ((status & STA_NODISK) != 0U)
    {
        return RES_NOTRDY;
    }
    
    if ((status & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }
    return RES_OK;
}

/* 查询物理盘状态 */
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != DISKIO_SDIO_PDRV)
    {
        return STA_NOINIT;
    }

    if (board_sdio_card_present() == 0U)
    {
        s_disk_sdio_rca = 0U;
        s_disk_sdio_status = STA_NODISK | STA_NOINIT;
        return s_disk_sdio_status;
    }
    
    return s_disk_sdio_status;
}

/* 初始化物理盘 */
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != DISKIO_SDIO_PDRV)
    {
        return STA_NOINIT;
    }
    if (board_sdio_card_present() == 0U)
    {
        s_disk_sdio_rca = 0U;
        s_disk_sdio_status = STA_NOINIT | STA_NODISK;
        return s_disk_sdio_status;
    }
    if (s_disk_sdio_rca == 0U)
    {
        s_disk_sdio_status = STA_NOINIT;
        return s_disk_sdio_status;
    }
    s_disk_sdio_status = 0U;
    return s_disk_sdio_status;
}

/* 读取一个或多个扇区 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    UINT index;
    DRESULT result;
    board_sdio_status_t status;
    BYTE *target;
    uint8_t *dma_buffer;
    uint8_t use_bounce;

    if ((pdrv != DISKIO_SDIO_PDRV) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    
    if ((DWORD)(sector + (DWORD)count - 1U) < sector)
    {
        return RES_PARERR;
    }
    
    result = diskio_sdio_ready_check();

    if (result != RES_OK)
    {
        return result;
    }
    
    for (index = 0U; index < count; index++)
    {
        target = buff + ((uint32_t)index * BOARD_SDIO_BLOCK_SIZE);
        use_bounce = (diskio_sdio_buffer_aligned(target) == 0U) ? 1U : 0U;
        dma_buffer = (use_bounce != 0U) ?
            s_disk_sdio_dma_bounce_buffer : target;

        status = board_sdio_read_block_dma_polling(
            sector + (DWORD)index,
            dma_buffer);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            if (status == BOARD_SDIO_STATUS_NO_CARD)
            {
                diskio_sdio_set_not_ready();
            }
            return diskio_sdio_status_to_result(status);
        }

        if (use_bounce != 0U)
        {
            (void)memcpy(
                target,
                s_disk_sdio_dma_bounce_buffer,
                BOARD_SDIO_BLOCK_SIZE);
        }

    }
    return RES_OK;
}

#if FF_FS_READONLY == 0

/* 写入一个或多个扇区 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    UINT index;
    DRESULT result;
    board_sdio_status_t status;
    const BYTE *source;
    const uint8_t *dma_buffer;
    uint8_t use_bounce;

    if ((pdrv != DISKIO_SDIO_PDRV) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    
    if ((DWORD)(sector + (DWORD)count - 1U) < sector)
    {
        return RES_PARERR;
    }

    result = diskio_sdio_ready_check();

    if (result != RES_OK)
    {
        return result;
    }
    
    for (index = 0U; index < count; index++)
    {
        source = buff + ((uint32_t)index * BOARD_SDIO_BLOCK_SIZE);
        use_bounce = (diskio_sdio_buffer_aligned(source) == 0U) ? 1U : 0U;

        if (use_bounce != 0U)
        {
            (void)memcpy(
                s_disk_sdio_dma_bounce_buffer,
                source,
                BOARD_SDIO_BLOCK_SIZE);
        }

        dma_buffer = (use_bounce != 0U) ?
            s_disk_sdio_dma_bounce_buffer : source;

        status = board_sdio_write_block_dma_polling(
            sector + (DWORD)index,
            dma_buffer);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            if (status == BOARD_SDIO_STATUS_NO_CARD)
            {
                diskio_sdio_set_not_ready();
            }
            return diskio_sdio_status_to_result(status);
        }

        status = board_sdio_wait_card_ready(s_disk_sdio_rca, NULL);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            return diskio_sdio_status_to_result(status);
        }

    }
    return RES_OK;
}

#endif

/* 设备控制 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT result;
    board_sdio_status_t status;

    if (pdrv != DISKIO_SDIO_PDRV)
    {
        return RES_PARERR;
    }

    if ((cmd != CTRL_SYNC) && (buff == NULL))
    {
        return RES_PARERR;
    }
    
    result = diskio_sdio_ready_check();

    if (result != RES_OK)
    {
        return result;
    }

    switch (cmd)
    {
        case CTRL_SYNC:
            status = board_sdio_wait_card_ready(s_disk_sdio_rca, NULL);
            return diskio_sdio_status_to_result(status);
        
        case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)BOARD_SDIO_BLOCK_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1U;
            return RES_OK;

        case GET_SECTOR_COUNT:
            return RES_PARERR;

        default:
            return RES_PARERR;
    }
    
}







