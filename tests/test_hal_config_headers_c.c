#define HAL_DISABLE_ASSERTS

#include "hal/core/hal_assert.h"
#include "hal/core/hal_compat.h"
#include "hal/core/hal_runtime_config.h"

static const char test_progmem_text[] PROGMEM = "compat";

int main(void) {
  hal_config_t config = {0};
  const char *flash_text = F("flash");

  config.can_max_instances = hal_max(1, 2);
  HAL_ASSERT(0, "disabled assertion must be a no-op");

  return (hal_min(config.can_max_instances, 3) == 2 &&
          test_progmem_text[0] == 'c' && flash_text[0] == 'f')
             ? 0
             : 1;
}
