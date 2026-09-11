#ifndef TEST_STORAGE_PERSISTENCE_BOARD_SPI_FLASH_H
#define TEST_STORAGE_PERSISTENCE_BOARD_SPI_FLASH_H

#include <stdint.h>

int board_spi_flash_read(uint32_t address, uint8_t *data, uint32_t length);
int board_spi_flash_page_program(uint32_t address,
                                 const uint8_t *data,
                                 uint32_t length);
int board_spi_flash_sector_erase(uint32_t address);
int board_spi_flash_read_jedec_id(uint8_t id[3]);

#endif /* TEST_STORAGE_PERSISTENCE_BOARD_SPI_FLASH_H */
