#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_usb.h"

hal_status_t hal_usb_init(void) { return HAL_EUNSUPPORTED; }

hal_status_t hal_usb_deinit(void) { return HAL_EUNSUPPORTED; }

hal_status_t hal_usb_task(void) { return HAL_EUNSUPPORTED; }

hal_status_t hal_usb_cdc_is_connected(bool *out_connected) {
  if (out_connected == nullptr) {
    return HAL_EINVAL;
  }
  *out_connected = false;
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_usb_cdc_available(size_t *out_available) {
  if (out_available == nullptr) {
    return HAL_EINVAL;
  }
  *out_available = 0u;
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity,
                              size_t *out_read) {
  if (out_read == nullptr || (data == nullptr && capacity != 0u)) {
    return HAL_EINVAL;
  }
  *out_read = 0u;
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written) {
  (void)timeout_ms;
  if (out_written == nullptr || (data == nullptr && length != 0u)) {
    return HAL_EINVAL;
  }
  *out_written = 0u;
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms) {
  (void)timeout_ms;
  return HAL_EUNSUPPORTED;
}

hal_status_t hal_usb_reset_to_bootloader(void) { return HAL_EUNSUPPORTED; }

hal_status_t
hal_usb_set_bootloader_reset_hook(hal_usb_bootloader_reset_hook_t hook,
                                  void *user) {
  (void)hook;
  (void)user;
  return HAL_EUNSUPPORTED;
}

#endif
