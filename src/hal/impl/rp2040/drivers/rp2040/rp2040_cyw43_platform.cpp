#include "../../../../hal_target.h"

#if HAL_TARGET_IS_RP2040
#include "../../../../hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_CYW43)

#include "../../../../impl/shared/hal_mutex_once.h"
#include "../../../../impl/shared/network/jh_cyw43_config.h"
#include "rp2040_cyw43_platform.h"

extern "C" {
#include <cyw43.h>
}

#include <hardware/gpio.h>
#include <lwip/sys.h>
#include <lwip/timeouts.h>
#include <pico/cyw43_arch.h>
#include <pico/cyw43_driver.h>
#include <pico/error.h>
#include <pico/platform.h>

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

static hal_mutex_t s_platform_mutex = NULL;

static void platform_lock(void *) {
  (void)jh_hal_mutex_create_once(&s_platform_mutex);
  hal_mutex_lock(s_platform_mutex);
}

static void platform_unlock(void *) { hal_mutex_unlock(s_platform_mutex); }

static jh_network_context_owner_t platform_owner(void *) {
#if defined(HAL_ENABLE_FREERTOS)
  return reinterpret_cast<jh_network_context_owner_t>(
      xTaskGetCurrentTaskHandle());
#else
  return (jh_network_context_owner_t)get_core_num() + 1u;
#endif
}

static hal_status_t platform_stack_enter(void *) {
  cyw43_arch_lwip_begin();
  return HAL_OK;
}

static void platform_stack_leave(void *) { cyw43_arch_lwip_end(); }

static void platform_service(void *) {
  cyw43_arch_poll();
  sys_check_timeouts();
}

static bool platform_ipv4_ready(void *) {
  return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

static const jh_network_service_port_t s_service_port = {
    nullptr,          platform_lock,        platform_unlock,
    platform_owner,   platform_stack_enter, platform_stack_leave,
    platform_service, platform_ipv4_ready,
};

hal_status_t jh_rp2040_cyw43_platform_status(int status) {
  switch (status) {
  case PICO_OK:
    return HAL_OK;
  case PICO_ERROR_TIMEOUT:
    return HAL_ETIMEOUT;
  case PICO_ERROR_INVALID_ARG:
    return HAL_EINVAL;
  case PICO_ERROR_BADAUTH:
    return HAL_EAUTH;
  case PICO_ERROR_INSUFFICIENT_RESOURCES:
    return HAL_ENOMEM;
  case PICO_ERROR_INVALID_STATE:
  case PICO_ERROR_PRECONDITION_NOT_MET:
    return HAL_ESTATE;
  case PICO_ERROR_RESOURCE_IN_USE:
    return HAL_EBUSY;
  case PICO_ERROR_IO:
  case PICO_ERROR_CONNECT_FAILED:
    return HAL_EIO;
  default:
    return HAL_EHW;
  }
}

hal_status_t jh_rp2040_cyw43_platform_init(uint32_t country_code) {
  const jh_cyw43_bus_config_t config = {
      HAL_CYW43_PIN_WL_ON,
      HAL_CYW43_PIN_DATA,
      HAL_CYW43_PIN_DATA,
      HAL_CYW43_PIN_DATA,
      HAL_CYW43_PIN_CLOCK,
      HAL_CYW43_PIN_CHIP_SELECT,
      NUM_BANK0_GPIOS,
      HAL_CYW43_PIO_CLOCK_DIV_INT,
      HAL_CYW43_PIO_CLOCK_DIV_FRAC8,
  };
  hal_status_t status = jh_cyw43_bus_config_validate(&config);
  if (status != HAL_OK) {
    return status;
  }
  uint pins[CYW43_PIN_INDEX_WL_COUNT] = {
      config.pin_wl_on,     config.pin_data_out, config.pin_data_in,
      config.pin_host_wake, config.pin_clock,    config.pin_chip_select,
  };
  int platform_status = cyw43_set_pins_wl(pins);
  if (platform_status != PICO_OK) {
    return jh_rp2040_cyw43_platform_status(platform_status);
  }
  cyw43_set_pio_clkdiv_int_frac8(config.pio_clock_div_int,
                                 config.pio_clock_div_frac8);
  platform_status = cyw43_arch_init_with_country(country_code);
  if (platform_status != PICO_OK) {
    return jh_rp2040_cyw43_platform_status(platform_status);
  }
  cyw43_arch_enable_sta_mode();
  return HAL_OK;
}

void jh_rp2040_cyw43_platform_deinit(void) {
  cyw43_arch_deinit();
  cyw43_ll_deinit(&cyw43_state.cyw43_ll);
  gpio_init(HAL_CYW43_PIN_WL_ON);
  gpio_set_dir(HAL_CYW43_PIN_WL_ON, GPIO_OUT);
  gpio_put(HAL_CYW43_PIN_WL_ON, false);
  sleep_ms(100u);
}

const jh_network_service_port_t *jh_rp2040_cyw43_platform_service_port(void) {
  return &s_service_port;
}

#endif
#endif
