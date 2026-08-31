#ifndef BOARD_I2C_H
#define BOARD_I2C_H

#include <stdint.h>

int board_i2c0_init(void);
int board_i2c0_write(uint8_t device_address, const uint8_t *data, uint16_t length);

#endif /* BOARD_I2C_H */
