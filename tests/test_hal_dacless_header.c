#include "hal/audio/hal_dacless.h"

_Static_assert(DACLESS_MAX_BLOCK_SIZE > 0u, "DACless block capacity");
_Static_assert(DACLESS_MAX_ADC_INPUTS > 0u, "DACless ADC capacity");

static uint16_t sample_callback(void *context) {
  (void)context;
  return 0u;
}

static void block_callback(void *context, uint16_t *buffer,
                           uint16_t sample_count) {
  (void)context;
  (void)buffer;
  (void)sample_count;
}

int main(void) {
  hal_dacless_t audio = 0;
  hal_dacless_config_t config = {0};
  hal_dacless_state_t state = {0};
  hal_dacless_sample_callback_t sample = sample_callback;
  hal_dacless_block_callback_t block = block_callback;
  (void)audio;
  (void)config;
  (void)state;
  (void)sample;
  (void)block;
  return 0;
}
