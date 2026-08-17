#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <stdint.h>
#include "board_config.h"
#include "gd32f4xx_gpio.h"

void board_led_init(void);
void board_led_on(void);
void board_led_off(void);
void board_led_set(uint8_t led_no, uint8_t on);

#endif // !BOARD_GPIO_H
