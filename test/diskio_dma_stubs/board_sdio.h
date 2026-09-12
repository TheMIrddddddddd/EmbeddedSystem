#ifndef TEST_DISKIO_DMA_BOARD_SDIO_H
#define TEST_DISKIO_DMA_BOARD_SDIO_H

#include <stdint.h>

#define BOARD_SDIO_BLOCK_SIZE 512U

typedef enum
{
    BOARD_SDIO_STATUS_OK = 0,
    BOARD_SDIO_STATUS_INVALID_ARGUMENT,
    BOARD_SDIO_STATUS_TIMEOUT,
    BOARD_SDIO_STATUS_COMMAND_ERROR,
    BOARD_SDIO_STATUS_DATA_ERROR,
    BOARD_SDIO_STATUS_NO_CARD,
    BOARD_SDIO_STATUS_BUSY,
    BOARD_SDIO_STATUS_NOT_READY
} board_sdio_status_t;

uint8_t board_sdio_card_present(void);
board_sdio_status_t board_sdio_read_block(
    uint32_t block_number,
    uint8_t *buffer);
board_sdio_status_t board_sdio_write_block(
    uint32_t block_number,
    const uint8_t *buffer);
board_sdio_status_t board_sdio_read_block_dma_polling(
    uint32_t block_number,
    uint8_t *buffer);
board_sdio_status_t board_sdio_write_block_dma_polling(
    uint32_t block_number,
    const uint8_t *buffer);
board_sdio_status_t board_sdio_wait_card_ready(
    uint16_t rca,
    uint32_t *response);

#endif
