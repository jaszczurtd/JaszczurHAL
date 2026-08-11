#include "rp2040_cyw43.h"

#include "hal/core/hal_config.h"
#if defined(HAL_NETWORK_BACKEND_CYW43) && defined(HAL_ENABLE_WIFI) &&          \
    HAL_BOARD_HAS_CYW43
#include "hal/network/cyw43/jh_cyw43_driver.h"
#include "hal/network/jh_cyw43_provider.h"

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

#elif defined(JH_RP_CYW43_LED_ONLY)

#include "hal/network/cyw43/jh_cyw43_driver.h"
#include "hal/system/hal_system.h"
#include "rp2040_cyw43_gspi.h"

static bool s_cyw43_led_init_attempted = false;
static bool s_cyw43_led_ready = false;

static bool cyw43_led_gpio(uint8_t pin, unsigned int *wl_gpio) {
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

static bool cyw43_led_ensure_ready(void) {
  if (!s_cyw43_led_init_attempted) {
    s_cyw43_led_init_attempted = true;
    const jh_rp2040_cyw43_gspi_config_t config = {
        (uint8_t)HAL_CYW43_PIN_CHIP_SELECT,
        (uint8_t)HAL_CYW43_PIN_CLOCK,
        (uint8_t)HAL_CYW43_PIN_WL_ON,
        (uint8_t)HAL_CYW43_PIN_DATA,
        (uint32_t)HAL_CYW43_GSPI_TARGET_HZ,
        (uint32_t)HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256,
        (size_t)HAL_CYW43_MAX_TRANSACTION_BYTES,
    };
    if (jh_rp2040_cyw43_gspi_init(&config) != HAL_OK) {
      return false;
    }

    jh_cyw43_driver_result_t result{};
    s_cyw43_led_ready = jh_cyw43_driver_start(jh_rp2040_cyw43_gspi_transport(),
                                              &result) == HAL_OK;
    if (!s_cyw43_led_ready) {
      (void)jh_rp2040_cyw43_gspi_deinit();
    }
  }
  return s_cyw43_led_ready;
}

static cyw43_ll_t *cyw43_led_low_level(void) {
  return cyw43_led_ensure_ready() ? jh_cyw43_driver_low_level() : nullptr;
}

extern "C" void hal_cyw43_pinMode(uint8_t pin, hal_cyw43_pin_mode_t mode) {
  unsigned int wl_gpio = 0u;
  cyw43_ll_t *driver = cyw43_led_low_level();
  if (mode == HAL_CYW43_PIN_OUTPUT && cyw43_led_gpio(pin, &wl_gpio) &&
      driver != nullptr) {
    (void)cyw43_ll_gpio_set(driver, (int)wl_gpio, false);
  }
}

extern "C" void hal_cyw43_digitalWrite(uint8_t pin, bool high) {
  unsigned int wl_gpio = 0u;
  cyw43_ll_t *driver = cyw43_led_low_level();
  if (cyw43_led_gpio(pin, &wl_gpio) && driver != nullptr) {
    (void)cyw43_ll_gpio_set(driver, (int)wl_gpio, high);
  }
}

extern "C" bool hal_cyw43_digitalRead(uint8_t pin) {
  unsigned int wl_gpio = 0u;
  bool value = false;
  cyw43_ll_t *driver = cyw43_led_low_level();
  return cyw43_led_gpio(pin, &wl_gpio) && driver != nullptr &&
         cyw43_ll_gpio_get(driver, (int)wl_gpio, &value) == 0 && value;
}

#else

extern "C" void hal_cyw43_pinMode(uint8_t, hal_cyw43_pin_mode_t) {}
extern "C" void hal_cyw43_digitalWrite(uint8_t, bool) {}
extern "C" bool hal_cyw43_digitalRead(uint8_t) { return false; }

#endif
