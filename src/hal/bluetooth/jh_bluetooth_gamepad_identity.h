#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  JH_BLUETOOTH_GAMEPAD_EXPECTED_VENDOR_ID = 0x2dc8u,
  JH_BLUETOOTH_GAMEPAD_EXPECTED_PRODUCT_ID = 0x3230u,
  JH_BLUETOOTH_GAMEPAD_EXPECTED_VERSION = 0x0100u,
  JH_BLUETOOTH_GAMEPAD_CLASS_OF_DEVICE_MAJOR_MASK = 0x1f00u,
  JH_BLUETOOTH_GAMEPAD_CLASS_OF_DEVICE_MAJOR_PERIPHERAL = 0x0500u,
};

#define JH_BLUETOOTH_GAMEPAD_EXPECTED_NAME "8BitDo Zero 2 gamepad"

static inline bool jh_bluetooth_gamepad_candidate_matches(
    uint32_t class_of_device, const uint8_t *name, size_t name_length) {
  static const char expected_name[] = JH_BLUETOOTH_GAMEPAD_EXPECTED_NAME;
  return (class_of_device & JH_BLUETOOTH_GAMEPAD_CLASS_OF_DEVICE_MAJOR_MASK) ==
             JH_BLUETOOTH_GAMEPAD_CLASS_OF_DEVICE_MAJOR_PERIPHERAL &&
         name != NULL && name_length == sizeof(expected_name) - 1u &&
         memcmp(name, expected_name, sizeof(expected_name) - 1u) == 0;
}

static inline bool jh_bluetooth_gamepad_product_matches(uint16_t vendor_id,
                                                        uint16_t product_id) {
  return vendor_id == JH_BLUETOOTH_GAMEPAD_EXPECTED_VENDOR_ID &&
         product_id == JH_BLUETOOTH_GAMEPAD_EXPECTED_PRODUCT_ID;
}

static inline bool jh_bluetooth_gamepad_pnp_matches(uint16_t vendor_id,
                                                    uint16_t product_id,
                                                    uint16_t version) {
  return jh_bluetooth_gamepad_product_matches(vendor_id, product_id) &&
         version == JH_BLUETOOTH_GAMEPAD_EXPECTED_VERSION;
}
