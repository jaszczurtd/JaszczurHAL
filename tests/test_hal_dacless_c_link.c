#include "hal/audio/hal_dacless.h"

static void fill_block(void *context, uint16_t *buffer, uint16_t sample_count) {
  (void)context;
  for (uint16_t index = 0u; index < sample_count; ++index) {
    buffer[index] = index;
  }
}

int main(void) {
  hal_dacless_config_t config;
  hal_dacless_t audio = 0;
  hal_dacless_state_t state;
  if (hal_dacless_config_init(&config) != HAL_OK) {
    return 1;
  }
  config.use_dma = false;
  config.adc_input_count = 0u;
  if (hal_dacless_create(&config, &audio) != HAL_OK ||
      hal_dacless_set_block_callback(audio, fill_block, 0) != HAL_OK ||
      hal_dacless_begin(audio) != HAL_OK ||
      hal_dacless_get_state(audio, &state) != HAL_OK || !state.running) {
    (void)hal_dacless_destroy(audio);
    return 1;
  }
  return hal_dacless_destroy(audio) == HAL_OK ? 0 : 1;
}
