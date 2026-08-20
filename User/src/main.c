#include <stdio.h>
#include "gd32f4xx.h"
#include "systick.h"
#include "board_gpio.h"
#include "board_usart.h"

int main(void)
{

    uint8_t rx_byte;
    uint32_t rx_count = 0U;
    uint32_t next_report_count = 256U;

    /* SysTick 1ms 节拍 —— delay_1ms 依赖它,必须先调 */
    systick_config();

    board_led_init();
    board_usart0_init();
    board_usart1_rs485_init();

    static const uint8_t message[] = "RS485 TX OK\r\n";
    board_usart1_rs485_send_buffer(message, sizeof(message) - 1U);

   while(1) {

    while(board_usart0_try_receive_byte(&rx_byte) != 0)
    {
        board_usart0_send_byte(rx_byte);
    }

    while (board_usart1_rs485_try_receive_byte(&rx_byte) != 0U)
    {
        rx_count++;
    }

    if (rx_count >= next_report_count)
    {
        printf("USART1 RX count=%lu\r\n", (unsigned long)rx_count);
        next_report_count += 256U;
    }
  }
}
