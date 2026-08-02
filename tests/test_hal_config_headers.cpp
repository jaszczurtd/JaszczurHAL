#include "hal/hal_assert.h"
#include "hal/hal_compat.h"
#include "hal/hal_runtime_config.h"

#include <cstdlib>

static const char test_progmem_text[] PROGMEM = "compat";

extern "C" HAL_NORETURN void hal_assert_fail(const char *) { std::abort(); }

int main() {
  hal_config_t config{};
  const char *flash_text = F("flash");

  config.pwm_freq_max_channels = hal_min(4, 8);
  HAL_ASSERT(config.pwm_freq_max_channels == 4, "unexpected minimum");

  return (hal_max(config.pwm_freq_max_channels, 6) == 6 &&
          test_progmem_text[0] == 'c' && flash_text[0] == 'f')
             ? 0
             : 1;
}
