#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK || HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_SWSERIAL

#include "hal/serial/hal_swserial.h"

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin) {
  hal_swserial_t handle = NULL;
  (void)hal_swserial_create_ex(rx_pin, tx_pin, &handle);
  return handle;
}

bool hal_swserial_set_rx(hal_swserial_t handle, uint8_t rx_pin) {
  return hal_status_to_bool(hal_swserial_set_rx_ex(handle, rx_pin));
}

bool hal_swserial_set_tx(hal_swserial_t handle, uint8_t tx_pin) {
  return hal_status_to_bool(hal_swserial_set_tx_ex(handle, tx_pin));
}

int hal_swserial_read(hal_swserial_t handle) {
  uint8_t value = 0u;
  return hal_status_is_ok(hal_swserial_read_ex(handle, &value)) ? (int)value
                                                                : -1;
}

size_t hal_swserial_write(hal_swserial_t handle, const uint8_t *data,
                          size_t len) {
  size_t written = 0u;
  (void)hal_swserial_write_ex(handle, data, len, &written);
  return written;
}

size_t hal_swserial_println(hal_swserial_t handle, const char *text) {
  size_t written = 0u;
  (void)hal_swserial_println_ex(handle, text, &written);
  return written;
}

#endif
#endif
