#include "jh_secure_random.h"

#include <string.h>

/* Targets override this with their platform source. */
__attribute__((weak)) hal_status_t jh_secure_random_bytes(void *buffer,
                                                          size_t length) {
  if (buffer == NULL || length == 0u) {
    return HAL_EINVAL;
  }
  memset(buffer, 0, length);
  return HAL_EUNSUPPORTED;
}
