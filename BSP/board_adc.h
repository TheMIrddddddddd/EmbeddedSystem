#ifndef BOARD_ADC_H
#define BOARD_ADC_H

#include <stdint.h>

#define BOARD_ADC_EVENT_DATA_READY   (1UL << 0)
#define BOARD_ADC_EVENT_DMA_ERROR    (1UL << 1)

typedef struct
{
    uint16_t ch0;
    uint16_t ch1;
} board_adc_sample_t;

typedef void (*board_adc_irq_callback_t)(uint32_t events);

int board_adc_init(void);
int board_adc_start(void);
void board_adc_stop(void);
int board_adc_trigger(void);

int board_adc_sample_get(board_adc_sample_t *sample);

void board_adc_irq_handler(void);
void board_adc_irq_callback_register(board_adc_irq_callback_t callback);

uint32_t board_adc_dma_event_count_get(void);
uint32_t board_adc_dma_error_count_get(void);

#endif /* BOARD_ADC_H */
