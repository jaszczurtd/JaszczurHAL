#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define ADC_TEMPERATURE_CHANNEL_NUM 4u

#ifdef __cplusplus
extern "C" {
#endif

/* The FIFO register is the read address of a DMA scan; the simulated DMA
 * recognises it and serves converter samples from there. */
typedef struct {
  uint32_t cs;
  uint32_t result;
  uint32_t fcs;
  uint32_t fifo;
  uint32_t div;
} adc_hw_t;

extern adc_hw_t jh_fake_adc_hw;
#define adc_hw (&jh_fake_adc_hw)

void adc_init(void);
void adc_gpio_init(uint gpio);
void adc_select_input(uint input);
uint16_t adc_read(void);
void adc_set_temp_sensor_enabled(bool enabled);
void adc_run(bool run);
void adc_fifo_drain(void);
void adc_set_clkdiv(float clkdiv);
void adc_set_round_robin(uint input_mask);
void adc_fifo_setup(bool en, bool dreq_en, uint16_t dreq_thresh,
                    bool err_in_fifo, bool byte_shift);

#ifdef __cplusplus
}
#endif
