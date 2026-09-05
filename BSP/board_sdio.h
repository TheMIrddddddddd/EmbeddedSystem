#ifndef BOARD_SDIO_H
#define BOARD_SDIO_H

#include <stdint.h>

#define  BOARD_SDIO_BLOCK_SIZE      512U

#define BOARD_SDIO_DMA_IRQ_EVENT_FTF       (1UL << 0)
#define BOARD_SDIO_DMA_IRQ_EVENT_FEE       (1UL << 1)
#define BOARD_SDIO_DMA_IRQ_EVENT_SDE       (1UL << 2)
#define BOARD_SDIO_DMA_IRQ_EVENT_TAE       (1UL << 3)
#define BOARD_SDIO_DMA_IRQ_EVENT_FATAL     (1UL << 4)

#define BOARD_SDIO_IRQ_EVENT_DTEND         (1UL << 0)
#define BOARD_SDIO_IRQ_EVENT_DTBLKEND      (1UL << 1)
#define BOARD_SDIO_IRQ_EVENT_DTCRCERR      (1UL << 2)
#define BOARD_SDIO_IRQ_EVENT_DTTMOUT       (1UL << 3)
#define BOARD_SDIO_IRQ_EVENT_TXURE         (1UL << 4)
#define BOARD_SDIO_IRQ_EVENT_RXORE         (1UL << 5)
#define BOARD_SDIO_IRQ_EVENT_STBITE        (1UL << 6)

typedef void (*board_sdio_irq_callback_t)(uint32_t dma_events, uint32_t sdio_events);

typedef struct
{
    uint8_t card_present;
    uint8_t high_capacity;
    uint8_t bus_4bit;
    uint8_t card_state;
    uint16_t rca;

    uint32_t cmd0_status;
    uint32_t cmd8_status;
    uint32_t cmd8_response;
    uint32_t acmd41_status;
    uint32_t ocr;

    uint32_t cmd2_status;
    uint32_t cid[4];

    uint32_t cmd3_status;
    uint32_t cmd3_response;

    uint32_t cmd9_status;
    uint32_t csd[4];

    uint32_t cmd7_status;
    uint32_t cmd7_response;

    uint32_t cmd13_status;
    uint32_t cmd13_response;

    uint32_t acmd6_status;
    uint32_t acmd6_response;
} board_sdio_card_info_t;

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

int board_sdio_init(void);
int board_sdio_bus_init(void);
uint8_t board_sdio_card_present(void);
board_sdio_status_t board_sdio_card_init(board_sdio_card_info_t *info);

board_sdio_status_t board_sdio_command(uint32_t command, uint32_t argument, uint32_t response_type, uint32_t *response);
board_sdio_status_t board_sdio_acmd41(uint32_t *ocr);
board_sdio_status_t board_sdio_command_r2(uint32_t command, uint32_t argument, uint32_t response[4]);
board_sdio_status_t board_sdio_command_r6(uint32_t command, uint32_t argument, uint32_t *response);
board_sdio_status_t board_sdio_set_bus_width_4bit(uint16_t rca, uint32_t *response);
board_sdio_status_t board_sdio_read_block(uint32_t block_number, uint8_t *buffer);
board_sdio_status_t board_sdio_write_block(uint32_t block_number, const uint8_t *buffer);
board_sdio_status_t board_sdio_wait_card_ready(uint16_t rca, uint32_t *response);
board_sdio_status_t board_sdio_read_block_dma_polling(uint32_t block_number, uint8_t *buffer);
board_sdio_status_t board_sdio_write_block_dma_polling(uint32_t block_number, const uint8_t *buffer);

board_sdio_status_t board_sdio_dma_read_start(
    uint32_t block_number,
    uint8_t *buffer);

board_sdio_status_t board_sdio_dma_write_start(
    uint32_t block_number,
    const uint8_t *buffer);

board_sdio_status_t board_sdio_dma_transfer_finish(void);
void board_sdio_dma_transfer_abort(void);
uint8_t board_sdio_dma_transfer_busy(void);

void board_sdio_dma_irq_handler(void);
void board_sdio_irq_handler(void);
void board_sdio_irq_callback_register(board_sdio_irq_callback_t callback);
#endif /* BOARD_SDIO_H */
