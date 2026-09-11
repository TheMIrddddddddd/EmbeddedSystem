#ifndef TEST_APP_CLI_BOARD_SPI_FLASH_H
#define TEST_APP_CLI_BOARD_SPI_FLASH_H

#include <stdint.h>

int board_spi_flash_read_jedec_id(uint8_t id[3]);

#endif /* TEST_APP_CLI_BOARD_SPI_FLASH_H */
