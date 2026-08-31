#ifndef BOARD_OLED_H
#define BOARD_OLED_H

#include <stdint.h>

/*
 * SSD1306 0.91 inch OLED, 128 x 32 pixels.
 * The address is the 8-bit I2C write address used by the GD32 SPL API.
 */
#define BOARD_OLED_I2C_ADDRESS       0x78U
#define BOARD_OLED_WIDTH              128U
#define BOARD_OLED_HEIGHT             32U
#define BOARD_OLED_PAGE_COUNT         (BOARD_OLED_HEIGHT / 8U)
#define BOARD_OLED_FRAMEBUFFER_SIZE   (BOARD_OLED_WIDTH * BOARD_OLED_PAGE_COUNT)

typedef enum
{
    BOARD_OLED_STATUS_OK = 0,
    BOARD_OLED_STATUS_INVALID_ARGUMENT,
    BOARD_OLED_STATUS_I2C_ERROR
} board_oled_status_t;

/* Call board_i2c0_init() before using this driver. */
board_oled_status_t board_oled_init(void);

/* Low-level SSD1306 command/data transfers. */
board_oled_status_t board_oled_write_command(uint8_t command);
board_oled_status_t board_oled_write_data(const uint8_t *data, uint16_t length);

/* Basic display controls. */
board_oled_status_t board_oled_display_on(void);
board_oled_status_t board_oled_display_off(void);
board_oled_status_t board_oled_clear(void);

/*
 * Refresh a page-major 128 x 32 framebuffer.
 * The buffer size must be BOARD_OLED_FRAMEBUFFER_SIZE bytes:
 * page 0 bytes first, then page 1, page 2 and page 3.
 */
board_oled_status_t board_oled_refresh(const uint8_t *framebuffer,
                                       uint16_t length);

#endif /* BOARD_OLED_H */
