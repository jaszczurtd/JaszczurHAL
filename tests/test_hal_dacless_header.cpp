#include "hal/audio/hal_dacless.h"

#include <type_traits>

static_assert(std::is_class_v<DAClessAudio>);
static_assert(std::is_pointer_v<hal_dacless_t>);
static_assert(std::is_same_v<decltype(&hal_dacless_begin),
                             hal_status_t (*)(hal_dacless_t)>);
static_assert(
    std::is_same_v<decltype(&hal_dacless_set_block_callback),
                   hal_status_t (*)(hal_dacless_t, hal_dacless_block_callback_t,
                                    void *)>);

int main(void) {
  DAClessConfig legacy_config;
  hal_dacless_config_t config = {};
  (void)legacy_config;
  (void)config;
  return 0;
}
