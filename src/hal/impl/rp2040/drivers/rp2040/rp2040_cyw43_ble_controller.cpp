#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP && defined(JH_BLUETOOTH_BTSTACK)

#include "hal/bluetooth/jh_ble_controller.h"

extern "C" const jh_ble_controller_t *jh_ble_controller_backend(void) {
  return jh_ble_controller_cyw43_instance();
}

#endif
