#include "../../hal_target.h"

#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#if defined(HAL_ENABLE_TLS)

#include <pico/rand.h>
#include <stddef.h>
#include <stdint.h>

#include "../../hal_status.h"

/* Arduino-Pico's pinned BearSSL archive expects this carrier ABI hook, but the
 * ordinary Pico variant does not provide WiFiClientSecureBearSSL.cpp. Keep the
 * definition weak so a carrier-owned implementation wins on Pico W. The Pico
 * SDK implementation uses the platform random source appropriate to RP2040 or
 * RP2350; hal_tls additionally injects caller-validated entropy explicitly. */
extern "C" __attribute__((weak)) uint32_t __picoRand(void) {
  return get_rand_32();
}

extern "C" hal_status_t hal_tls_default_entropy(void *, void *buffer,
                                                size_t length) {
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
#endif
