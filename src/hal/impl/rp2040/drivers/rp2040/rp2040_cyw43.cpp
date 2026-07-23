#include "rp2040_cyw43.h"

#include "../../../../hal_config.h"
#if defined(HAL_NETWORK_BACKEND_CYW43) && defined(HAL_ENABLE_WIFI)
#include "../../../../impl/shared/drivers/cyw43-driver/jh_cyw43_driver.h"
#include "../../../../impl/shared/network/jh_cyw43_provider.h"

static bool cyw43_wl_gpio(uint8_t pin, unsigned int *wl_gpio) {
  if (pin < 64u) {
    return false;
  }

  const unsigned int gpio = (unsigned int)(pin - 64u);
  if (gpio >= CYW43_WL_GPIO_COUNT) {
    return false;
  }

  *wl_gpio = gpio;
  return true;
}

static bool cyw43_access_begin(void) {
  return jh_cyw43_provider_stack_enter(false) == HAL_OK;
}

static void cyw43_access_end(void) { jh_cyw43_provider_stack_leave(); }

extern "C" void hal_cyw43_pinMode(uint8_t pin, hal_cyw43_pin_mode_t mode) {
  (void)pin;
  (void)mode;
  // The CYW43 GPIO API exposes value control only; there is no GPIO direction
  // configuration equivalent to bank0 GPIO pinMode.
}

extern "C" void hal_cyw43_digitalWrite(uint8_t pin, bool high) {
  unsigned int wl_gpio = 0u;
  if (!cyw43_wl_gpio(pin, &wl_gpio)) {
    return;
  }
  if (!cyw43_access_begin()) {
    return;
  }

  (void)cyw43_gpio_set(&cyw43_state, (int)wl_gpio, high);
  cyw43_access_end();
}

extern "C" bool hal_cyw43_digitalRead(uint8_t pin) {
  unsigned int wl_gpio = 0u;
  if (!cyw43_wl_gpio(pin, &wl_gpio)) {
    return false;
  }
  if (!cyw43_access_begin()) {
    return false;
  }

  bool value = false;
  (void)cyw43_gpio_get(&cyw43_state, (int)wl_gpio, &value);
  cyw43_access_end();
  return value;
}

#else

extern "C" void hal_cyw43_pinMode(uint8_t, hal_cyw43_pin_mode_t) {}
extern "C" void hal_cyw43_digitalWrite(uint8_t, bool) {}
extern "C" bool hal_cyw43_digitalRead(uint8_t) { return false; }

#endif
