#include "hal/security/jh_secure_random.h"

#include <stdint.h>

void jh_secure_zeroize(void *buffer, size_t length) {
  if (buffer == nullptr) {
    return;
  }
  volatile uint8_t *bytes = static_cast<volatile uint8_t *>(buffer);
  for (size_t index = 0u; index < length; ++index) {
    bytes[index] = 0u;
  }
}

bool jh_constant_time_compare(const void *left, const void *right,
                              size_t length) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  const uint8_t *left_bytes = static_cast<const uint8_t *>(left);
  const uint8_t *right_bytes = static_cast<const uint8_t *>(right);
  uint8_t difference = 0u;
  for (size_t index = 0u; index < length; ++index) {
    difference = static_cast<uint8_t>(
        difference |
        static_cast<uint8_t>(left_bytes[index] ^ right_bytes[index]));
  }
  return difference == 0u;
}

/* Targets override this with their platform source. */
__attribute__((weak)) hal_status_t jh_secure_random_bytes(void *buffer,
                                                          size_t length) {
  if (buffer == NULL || length == 0u) {
    return HAL_EINVAL;
  }
  jh_secure_zeroize(buffer, length);
  return HAL_EUNSUPPORTED;
}
