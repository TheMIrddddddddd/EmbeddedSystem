#ifndef TEST_APP_PROTOCOL_BOARD_SPI_FLASH_H
#define TEST_APP_PROTOCOL_BOARD_SPI_FLASH_H

#include <stdint.h>

int board_spi_flash_read_jedec_id(uint8_t jedec_id[3]);

#endif
