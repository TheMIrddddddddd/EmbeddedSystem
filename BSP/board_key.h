#ifndef BOARD_KEY_H
#define BOARD_KEY_H

#include <stdint.h>

void board_key_init(void);
uint8_t board_key_read(uint8_t key_no);

#endif /* BOARD_KEY_H */
