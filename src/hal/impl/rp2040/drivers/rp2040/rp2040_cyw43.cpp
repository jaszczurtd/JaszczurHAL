#include "rp2040_cyw43.h"

extern "C" {
#include <cyw43.h>
#include <cyw43_stats.h>
}
#include <pico/cyw43_arch.h>

static bool cyw43_wl_gpio(uint8_t pin, uint *wl_gpio) {
  if (pin < 64u) {
    return false;
  }

  const uint gpio = (uint)(pin - 64u);
  if (gpio >= CYW43_WL_GPIO_COUNT) {
    return false;
  }

  *wl_gpio = gpio;
  return true;
}

extern "C" void hal_cyw43_pinMode(uint8_t pin, hal_cyw43_pin_mode_t mode) {
  (void)pin;
  (void)mode;
  // The CYW43 GPIO API exposes value control only; there is no GPIO direction
  // configuration equivalent to bank0 GPIO pinMode.
}

extern "C" void hal_cyw43_digitalWrite(uint8_t pin, bool high) {
  uint wl_gpio = 0u;
  if (!cyw43_wl_gpio(pin, &wl_gpio)) {
    return;
  }

  (void)cyw43_gpio_set(&cyw43_state, (int)wl_gpio, high);
}

extern "C" bool hal_cyw43_digitalRead(uint8_t pin) {
  uint wl_gpio = 0u;
  if (!cyw43_wl_gpio(pin, &wl_gpio)) {
    return false;
  }

  bool value = false;
  (void)cyw43_gpio_get(&cyw43_state, (int)wl_gpio, &value);
  return value;
}
