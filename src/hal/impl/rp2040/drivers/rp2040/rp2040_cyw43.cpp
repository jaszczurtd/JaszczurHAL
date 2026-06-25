#include "rp2040_cyw43.h"

extern "C" {
#include <cyw43.h>
#include <cyw43_stats.h>
}
#include <pico/cyw43_arch.h>

#if defined(EXTENDED_PIN_MODE)
typedef uint32_t hal_cyw43_core_pin_t;
#else
typedef uint8_t hal_cyw43_core_pin_t;
#endif

typedef enum {
  HAL_CYW43_CORE_LOW = 0,
  HAL_CYW43_CORE_HIGH = 1,
} hal_cyw43_core_pin_status_t;

typedef enum {
  HAL_CYW43_CORE_INPUT = 0,
  HAL_CYW43_CORE_OUTPUT = 1,
} hal_cyw43_core_pin_mode_t;

#if defined(PICO_CYW43_SUPPORTED)
extern bool __isPicoW;
#endif

extern "C" void __pinMode(hal_cyw43_core_pin_t pin,
                          hal_cyw43_core_pin_mode_t mode);
extern "C" void __digitalWrite(hal_cyw43_core_pin_t pin,
                               hal_cyw43_core_pin_status_t val);
extern "C" hal_cyw43_core_pin_status_t __digitalRead(hal_cyw43_core_pin_t pin);

static hal_cyw43_core_pin_t cyw43_core_pin(uint8_t pin) {
#if defined(PICO_CYW43_SUPPORTED)
  if (!__isPicoW && pin >= 64u) {
    return 25u;
  }
#endif
  return (hal_cyw43_core_pin_t)pin;
}

extern "C" void hal_cyw43_pinMode(uint8_t pin, hal_cyw43_pin_mode_t mode) {
  hal_cyw43_core_pin_t core_pin = cyw43_core_pin(pin);
  if (core_pin < 64u) {
    __pinMode(core_pin, mode == HAL_CYW43_PIN_OUTPUT ? HAL_CYW43_CORE_OUTPUT
                                                     : HAL_CYW43_CORE_INPUT);
  } else {
    // TBD - There is no GPIO direction control in the driver.
  }
}

extern "C" void hal_cyw43_digitalWrite(uint8_t pin, bool high) {
  hal_cyw43_core_pin_t core_pin = cyw43_core_pin(pin);
  if (core_pin < 64u) {
    __digitalWrite(core_pin, high ? HAL_CYW43_CORE_HIGH : HAL_CYW43_CORE_LOW);
  } else {
    cyw43_arch_gpio_put(core_pin - 64u, high ? 1 : 0);
  }
}

extern "C" bool hal_cyw43_digitalRead(uint8_t pin) {
  hal_cyw43_core_pin_t core_pin = cyw43_core_pin(pin);
  if (core_pin < 64u) {
    return __digitalRead(core_pin) == HAL_CYW43_CORE_HIGH;
  }
  return cyw43_arch_gpio_get(core_pin - 64u) != 0;
}
