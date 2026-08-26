#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474 && defined(JH_BLUETOOTH_BTSTACK)

#include "hal/bluetooth/jh_bluetooth_controller.h"

extern "C" const jh_bluetooth_controller_t *
jh_bluetooth_controller_backend(void) {
  return jh_bluetooth_controller_cyw43_instance();
}

#endif
