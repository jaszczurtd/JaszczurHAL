/*
 * based on WireGuard implementation for ESP32 Arduino by Kenta Ida
 * (fuga@fugafuga.org) SPDX-License-Identifier: BSD-3-Clause RP2040 port by
 * Marcin Kielesinski (jaszczurtd@tlen.pl)
 */

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_WIREGUARD)

#include "hal/network/jh_lwip_extension.h"
#include "hal/serial/hal_serial.h"
#include "wireguard-platform.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool is_platform_initialized = false;

void wireguard_platform_init(void) {
  if (is_platform_initialized)
    return;

  const hal_status_t status =
      jh_lwip_extension_validate(jh_lwip_extension_platform_port());
  if (status != HAL_OK) {
    hal_derr("WireGuard: invalid lwIP extension port: %s",
             hal_status_to_string(status));
    return;
  }
  is_platform_initialized = true;
}

void wireguard_random_bytes(void *bytes, size_t size) {
  const hal_status_t status = jh_lwip_extension_random_bytes(
      jh_lwip_extension_platform_port(), bytes, size);
  if (status != HAL_OK) {
    if (bytes != NULL && size > 0u) {
      memset(bytes, 0, size);
    }
    hal_derr("WireGuard: entropy source failed: %s",
             hal_status_to_string(status));
  }
}

uint32_t wireguard_sys_now(void) {
  uint32_t milliseconds = 0u;
  const hal_status_t status = jh_lwip_extension_monotonic_ms(
      jh_lwip_extension_platform_port(), &milliseconds);
  if (status != HAL_OK) {
    hal_derr("WireGuard: monotonic clock failed: %s",
             hal_status_to_string(status));
  }
  return milliseconds;
}

void wireguard_tai64n_now(uint8_t *output) {
  const hal_status_t status =
      jh_lwip_extension_tai64n_now(jh_lwip_extension_platform_port(), output);
  if (status != HAL_OK) {
    if (output != NULL) {
      memset(output, 0, JH_LWIP_EXTENSION_TAI64N_SIZE);
    }
    hal_derr("WireGuard: wall clock failed: %s", hal_status_to_string(status));
  }
}

bool wireguard_is_under_load(void) { return false; }

#endif /* HAL_ENABLE_WIREGUARD */
