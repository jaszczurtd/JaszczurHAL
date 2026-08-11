#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK

#include "hal/usb/hal_usb.h"
#include "hal_mock.h"

#include <string.h>

namespace {

constexpr size_t kUsbMockBufferSize = 4096u;

bool s_initialized;
bool s_connected;
bool s_reset_requested;
uint8_t s_rx[kUsbMockBufferSize];
size_t s_rx_size;
uint8_t s_tx[kUsbMockBufferSize];
size_t s_tx_size;
hal_usb_bootloader_reset_hook_t s_reset_hook;
void *s_reset_hook_user;

} // namespace

hal_status_t hal_usb_init(void) {
  s_initialized = true;
  return HAL_OK;
}

hal_status_t hal_usb_deinit(void) {
  s_initialized = false;
  s_connected = false;
  return HAL_OK;
}

hal_status_t hal_usb_task(void) { return s_initialized ? HAL_OK : HAL_EUNINIT; }

hal_status_t hal_usb_cdc_is_connected(bool *out_connected) {
  if (out_connected == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_initialized) {
    *out_connected = false;
    return HAL_EUNINIT;
  }
  *out_connected = s_connected;
  return HAL_OK;
}

hal_status_t hal_usb_cdc_available(size_t *out_available) {
  if (out_available == nullptr) {
    return HAL_EINVAL;
  }
  if (!s_initialized) {
    *out_available = 0u;
    return HAL_EUNINIT;
  }
  *out_available = s_rx_size;
  return HAL_OK;
}

hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity,
                              size_t *out_read) {
  if (out_read == nullptr || (data == nullptr && capacity != 0u)) {
    return HAL_EINVAL;
  }
  *out_read = 0u;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  if (capacity == 0u || s_rx_size == 0u) {
    return HAL_EAGAIN;
  }

  const size_t count = capacity < s_rx_size ? capacity : s_rx_size;
  memcpy(data, s_rx, count);
  s_rx_size -= count;
  if (s_rx_size != 0u) {
    memmove(s_rx, s_rx + count, s_rx_size);
  }
  *out_read = count;
  return HAL_OK;
}

hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written) {
  (void)timeout_ms;
  if (out_written == nullptr || (data == nullptr && length != 0u)) {
    return HAL_EINVAL;
  }
  *out_written = 0u;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  if (!s_connected) {
    return HAL_EAGAIN;
  }
  if (length == 0u) {
    return HAL_OK;
  }

  const size_t free_bytes = sizeof(s_tx) - s_tx_size;
  const size_t count = length < free_bytes ? length : free_bytes;
  if (count != 0u) {
    memcpy(s_tx + s_tx_size, data, count);
    s_tx_size += count;
    *out_written = count;
  }
  return count == length ? HAL_OK : HAL_EOVERFLOW;
}

hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  return s_connected ? HAL_OK : HAL_EAGAIN;
}

hal_status_t hal_usb_reset_to_bootloader(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  s_reset_requested = true;
  if (s_reset_hook != nullptr) {
    s_reset_hook(s_reset_hook_user);
  }
  return HAL_OK;
}

hal_status_t
hal_usb_set_bootloader_reset_hook(hal_usb_bootloader_reset_hook_t hook,
                                  void *user) {
  s_reset_hook = hook;
  s_reset_hook_user = user;
  return HAL_OK;
}

void hal_mock_usb_reset(void) {
  s_initialized = false;
  s_connected = false;
  s_reset_requested = false;
  s_rx_size = 0u;
  s_tx_size = 0u;
  s_reset_hook = nullptr;
  s_reset_hook_user = nullptr;
}

void hal_mock_usb_set_connected(bool connected) { s_connected = connected; }

void hal_mock_usb_inject_rx(const uint8_t *data, size_t length) {
  if (data == nullptr) {
    return;
  }
  const size_t free_bytes = sizeof(s_rx) - s_rx_size;
  const size_t count = length < free_bytes ? length : free_bytes;
  memcpy(s_rx + s_rx_size, data, count);
  s_rx_size += count;
}

const uint8_t *hal_mock_usb_tx_data(void) { return s_tx; }

size_t hal_mock_usb_tx_size(void) { return s_tx_size; }

bool hal_mock_usb_reset_requested(void) { return s_reset_requested; }

#endif
