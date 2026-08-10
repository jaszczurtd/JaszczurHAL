#include "../../hal_target.h"

#if HAL_TARGET_IS_MOCK

#include "../shared/jh_secure_random.h"
#include "hal_mock.h"

#include <stdint.h>

namespace {

/* The host has no entropy source, so tests supply one explicitly. */
hal_status_t s_status = HAL_EUNSUPPORTED;
uint64_t s_state = 0x9E3779B97F4A7C15ull;

uint8_t next_byte(void) {
  s_state ^= s_state >> 12;
  s_state ^= s_state << 25;
  s_state ^= s_state >> 27;
  return (uint8_t)((s_state * 0x2545F4914F6CDD1Dull) >> 56);
}

} // namespace

extern "C" hal_status_t jh_secure_random_bytes(void *buffer, size_t length) {
  if (buffer == NULL || length == 0u) {
    return HAL_EINVAL;
  }
  if (s_status != HAL_OK) {
    jh_secure_zeroize(buffer, length);
    return s_status;
  }
  uint8_t *bytes = static_cast<uint8_t *>(buffer);
  for (size_t index = 0u; index < length; ++index) {
    bytes[index] = next_byte();
  }
  return HAL_OK;
}

void hal_mock_secure_random_reset(void) {
  s_status = HAL_EUNSUPPORTED;
  s_state = 0x9E3779B97F4A7C15ull;
}

void hal_mock_secure_random_set_status(hal_status_t status) {
  s_status = status;
}

void hal_mock_secure_random_set_seed(uint64_t seed) {
  s_state = seed != 0u ? seed : 0x9E3779B97F4A7C15ull;
}

#endif
