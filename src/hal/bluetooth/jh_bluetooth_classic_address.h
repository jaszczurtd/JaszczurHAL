#pragma once

#include "hal/bluetooth/hal_bluetooth_classic.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool jh_bluetooth_classic_address_equal(
    const hal_bluetooth_classic_address_t *left,
    const hal_bluetooth_classic_address_t *right) {
  return left != NULL && right != NULL &&
         memcmp(left->bytes, right->bytes, HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN) ==
             0;
}

static inline bool jh_bluetooth_classic_address_is_zero(
    const hal_bluetooth_classic_address_t *address) {
  if (address == NULL) {
    return false;
  }
  uint8_t combined = 0u;
  for (size_t index = 0u; index < HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN; ++index) {
    combined |= address->bytes[index];
  }
  return combined == 0u;
}
