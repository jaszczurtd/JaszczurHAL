#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/security/jh_secure_random.h"

#include <pico/rand.h>
#include <stdint.h>
#include <string.h>

/* Pico SDK selects the generator appropriate to RP2040 or RP2350. */
extern "C" hal_status_t jh_secure_random_bytes(void *buffer, size_t length) {
  if (buffer == NULL || length == 0u) {
    return HAL_EINVAL;
  }
  uint8_t *bytes = static_cast<uint8_t *>(buffer);
  size_t offset = 0u;
  while (offset < length) {
    const uint32_t value = get_rand_32();
    const size_t remaining = length - offset;
    const size_t count = remaining < sizeof(value) ? remaining : sizeof(value);
    for (size_t index = 0u; index < count; ++index) {
      bytes[offset + index] = (uint8_t)(value >> (index * 8u));
    }
    offset += count;
  }
  return HAL_OK;
}

#endif
