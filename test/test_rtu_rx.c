/* Execute the actual BSP receiver with only registers/clock replaced.
 * RingBuffer, timing core, IRQ handler and frame queue are production code.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "board_dma_map.h"
#include "board_timebase.h"

static uint32_t host_status, host_data, host_now, host_mask;
static uint32_t host_dma, host_word, host_parity, host_delay;
static board_timebase_alarm_callback_t host_callback;
#undef USART_STAT0
#undef USART_DATA
#define USART_STAT0(x) host_status
#define USART_DATA(x) host_data
#define __get_PRIMASK() host_mask
#define __disable_irq() (host_mask = 1U)
#define __set_PRIMASK(x) (host_mask = (x))

/* AC5's retargeted stdio objects are irrelevant on the host. */
typedef struct __FILE host_FILE;
#define FILE host_FILE
#define fputc host_fputc
#include "../BSP/board_usart.c"
#undef FILE
#undef fputc

uint32_t board_timebase_now_us(void) { return host_now; }
int board_timebase_alarm_start(uint32_t delay, board_timebase_alarm_callback_t cb)
{ host_delay = delay; host_callback = cb; return 1; }
void board_timebase_alarm_cancel(void) { host_callback = NULL; }
uint32_t dma_transfer_number_get(uint32_t p, dma_channel_enum c)
{ (void)p; (void)c; return 512U; }
void usart_disable(uint32_t p) { (void)p; }
void usart_enable(uint32_t p) { (void)p; }
void usart_interrupt_enable(uint32_t p, usart_interrupt_enum e) { (void)p; (void)e; }
void usart_interrupt_disable(uint32_t p, usart_interrupt_enum e) { (void)p; (void)e; }
void usart_dma_receive_config(uint32_t p, uint32_t v) { (void)p; host_dma = v; }
void usart_word_length_set(uint32_t p, uint32_t v) { (void)p; host_word = v; }
void usart_parity_config(uint32_t p, uint32_t v) { (void)p; host_parity = v; }
void usart_baudrate_set(uint32_t p, uint32_t v) { (void)p; (void)v; }
FlagStatus usart_interrupt_flag_get(uint32_t p, usart_interrupt_flag_enum e)
{ (void)p; (void)e; return RESET; }
uint16_t usart_data_receive(uint32_t p)
{ (void)p; host_status = 0; return (uint16_t)host_data; }
void usart_data_transmit(uint32_t p, uint16_t d) { (void)p; (void)d; }

static void timeout(void)
{
    board_timebase_alarm_callback_t cb = host_callback;
    assert(cb);
    host_callback = NULL;
    host_now += host_delay;
    cb();
    assert(host_mask == 0);
}
static void byte(uint8_t value, uint32_t delta, uint32_t error)
{
    host_now += delta;
    host_data = value;
    host_status = USART_STAT0_RBNE | error;
    board_usart1_rs485_irq_handler();
    assert(host_mask == 0);
}
static void start(void)
{
    rt_ringbuffer_init(&s_usart1_rs485_rx_ringbuffer,
        s_usart1_rs485_rx_ringbuffer_pool, sizeof(s_usart1_rs485_rx_ringbuffer_pool));
    board_usart1_rs485_rtu_receive_enable(1);
    assert(host_dma == USART_RECEIVE_DMA_DISABLE);
    assert(host_word == USART_WL_9BIT && host_parity == USART_PM_EVEN);
    timeout();
}

int main(void)
{
    uint8_t out[256];
    uint16_t len;
    unsigned i;
    start();
    assert(host_delay == 1846);
    byte(0x11, 100, 0); byte(0x22, 96, 0); timeout();
    assert(board_usart1_rs485_try_receive_byte(out) == 0);
    assert(board_usart1_rs485_rtu_frame_receive(out, 1, &len) == 3);
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 1);
    assert(len == 2 && out[0] == 0x11 && out[1] == 0x22);

    /* Preserve a queued valid frame when the next frame has a bad gap. */
    byte(0x33, 100, 0); timeout();
    byte(0x44, 100, 0); byte(0x55, 900, 0); byte(0x66, 96, 0); timeout();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 1);
    assert(len == 1 && out[0] == 0x33);
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 4);
    assert(len == 0);
    byte(0x77, 100, 0); timeout();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 1);
    assert(len == 1 && out[0] == 0x77);

    /* Oversize must be consumed as an error, not block a 256-byte reader. */
    for (i = 0; i < 300; i++) byte((uint8_t)i, 96, 0);
    timeout();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 2);
    assert(len == 0);

    /* Error on the very first byte must not poison the following frame. */
    byte(0x88, 100, USART_STAT0_PERR); timeout();
    byte(0x99, 100, 0); timeout();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 1);
    assert(len == 1 && out[0] == 0x99);

    /* Queue full recovery and both directions of mode switching. */
    for (i = 0; i < 8; i++) { byte((uint8_t)i, 100, 0); timeout(); }
    byte(0xAB, 100, 0); timeout();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 1);
    assert(len == 1 && out[0] == 0xAB);
    board_usart1_rs485_rtu_receive_enable(0);
    assert(host_dma == USART_RECEIVE_DMA_ENABLE);
    assert(host_word == USART_WL_8BIT && host_parity == USART_PM_NONE);
    assert(!host_callback);
    assert(rt_ringbuffer_putchar(&s_usart1_rs485_rx_ringbuffer, 0xCC));
    assert(board_usart1_rs485_try_receive_byte(out) && out[0] == 0xCC);
    start();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 0);
    board_usart1_rs485_baudrate_set(9600);
    assert(s_usart1_rs485_rtu_t1_5_us == 1719);
    assert(s_usart1_rs485_rtu_t3_5_us == 4011 && host_delay == 5157);
    timeout();
    board_usart1_rs485_baudrate_set(19200);
    assert(s_usart1_rs485_rtu_t1_5_us == 860);
    assert(s_usart1_rs485_rtu_t3_5_us == 2006 && host_delay == 2579);
    timeout();
    board_usart1_rs485_baudrate_set(115200);
    assert(s_usart1_rs485_rtu_t1_5_us == 750 && host_delay == 1846);
    timeout();
    byte(0xA1, 100, 0);
    /* A completed byte pending at timer IRQ must join this frame. */
    host_now += 96;
    host_data = 0xA2;
    host_status = USART_STAT0_RBNE;
    board_usart1_rs485_rtu_alarm_callback();
    timeout();
    assert(board_usart1_rs485_rtu_frame_receive(out, sizeof(out), &len) == 1);
    assert(len == 2 && out[0] == 0xA1 && out[1] == 0xA2);
    puts("RTU BSP: frames, invalid gaps, overflow, UART errors, mode switch PASS");
    return 0;
}
