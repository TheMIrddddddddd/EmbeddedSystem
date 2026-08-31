#include "protocol_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_queues.h"
#include "board_usart.h"

#define PROTOCOL_TASK_PRIORITY          5U
#define PROTOCOL_TASK_STACK_DEPTH       256U

#define PROTOCOL_RS485_TEST_BUFFER_SIZE 64U

static StaticTask_t s_protocol_task_tcb;
static StackType_t  s_protocol_task_stack[PROTOCOL_TASK_STACK_DEPTH];
static volatile uint32_t s_protocol_task_heartbeat;
static volatile uint8_t s_rs485_last_rx_byte;
static volatile uint16_t s_rs485_last_rx_length;
static volatile uint32_t s_rs485_rx_total_count;

static void protocol_task(void *argument)
{
    (void)argument;

    uint8_t rx_buffer[PROTOCOL_RS485_TEST_BUFFER_SIZE];
    uint16_t rx_length;
    uint8_t rx_byte;

    static const uint8_t message[] = "RS485 READY\r\n";
    board_usart1_rs485_send_buffer(message, sizeof(message) - 1);

    for(;;)
    {
        s_protocol_task_heartbeat++;

        rx_length = 0U;

        while ((rx_length < PROTOCOL_RS485_TEST_BUFFER_SIZE) && (board_usart1_rs485_try_receive_byte(&rx_byte) != 0U))
        {
            rx_buffer[rx_length] = rx_byte;
            rx_length++;

            s_rs485_last_rx_byte = rx_byte;
            s_rs485_rx_total_count++;
        }

        if (rx_length > 0U)
        {
            s_rs485_last_rx_length = rx_length;

            board_usart1_rs485_send_buffer(rx_buffer, rx_length);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int protocol_task_create(void)
{
    TaskHandle_t protocol_task_handle;

    protocol_task_handle = xTaskCreateStatic(
        protocol_task,
        "Protocol",
        PROTOCOL_TASK_STACK_DEPTH,
        NULL,
        PROTOCOL_TASK_PRIORITY,
        s_protocol_task_stack,
        &s_protocol_task_tcb
    );

    if (protocol_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}

uint32_t protocol_task_get_heartbeat(void)
{
    return s_protocol_task_heartbeat;
}
