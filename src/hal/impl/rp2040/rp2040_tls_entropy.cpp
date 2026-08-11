#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_TLS)

#include <pico/rand.h>
#include <stdint.h>

/* The RP2040 BearSSL ABI expects this random hook. hal_tls additionally
 * injects caller-validated entropy explicitly; the shared platform source
 * lives in rp2040_secure_random.cpp. */
extern "C" __attribute__((weak)) uint32_t __picoRand(void) {
  return get_rand_32();
}

#endif
#endif
