#include "../../../../hal_target.h"

#if HAL_TARGET_IS_RP
#include "../../../../hal_config.h"

#if defined(HAL_ENABLE_NETWORK_CORE) && defined(HAL_NETWORK_BACKEND_CYW43) &&  \
    HAL_BOARD_HAS_CYW43

#include "../../../../hal_serial.h"
#include "../../../../hal_sync.h"
#include "../../../../hal_system.h"
#include "../../../../impl/shared/drivers/cyw43-driver/jh_cyw43_driver.h"
#include "../../../../impl/shared/drivers/cyw43-driver/jh_cyw43_lwip.h"
#include "../../../../impl/shared/hal_mutex_once.h"
#include "rp2040_cyw43_gspi.h"
#include "rp2040_cyw43_platform.h"

#include <hardware/sync.h>
#include <pico/error.h>

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

namespace {

hal_mutex_t s_state_mutex;
hal_mutex_t s_stack_mutex;

void ensure_state_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_state_mutex);
}

void ensure_stack_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_stack_mutex);
}

void platform_state_lock(void *) {
  ensure_state_mutex();
  hal_mutex_lock(s_state_mutex);
}

void platform_state_unlock(void *) { hal_mutex_unlock(s_state_mutex); }

jh_network_context_owner_t platform_owner(void *) {
#if defined(HAL_ENABLE_FREERTOS)
  const TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return task == nullptr ? 1u
                         : reinterpret_cast<jh_network_context_owner_t>(task);
#else
  return (jh_network_context_owner_t)get_core_num() + 1u;
#endif
}

hal_status_t platform_stack_enter(void *) {
  ensure_stack_mutex();
  hal_mutex_lock(s_stack_mutex);
  if (!jh_cyw43_driver_is_ready()) {
    hal_mutex_unlock(s_stack_mutex);
    return HAL_EUNINIT;
  }
  return HAL_OK;
}

void platform_stack_leave(void *) { hal_mutex_unlock(s_stack_mutex); }

void platform_service(void *) { (void)jh_cyw43_lwip_service(); }

bool platform_ipv4_ready(void *) {
  jh_cyw43_lwip_snapshot_t snapshot{};
  return jh_cyw43_lwip_get_snapshot(&snapshot) == HAL_OK &&
         snapshot.dhcp_bound && snapshot.ipv4 != 0u;
}

const jh_network_service_port_t s_service_port = {
    nullptr,          platform_state_lock,  platform_state_unlock,
    platform_owner,   platform_stack_enter, platform_stack_leave,
    platform_service, platform_ipv4_ready,
};

} // namespace

hal_status_t jh_rp2040_cyw43_platform_status(int status) {
  if (status == 0) {
    return HAL_OK;
  }
  if (status == -CYW43_ETIMEDOUT || status == PICO_ERROR_TIMEOUT) {
    return HAL_ETIMEOUT;
  }
  if (status == -CYW43_EINVAL || status == PICO_ERROR_INVALID_ARG) {
    return HAL_EINVAL;
  }
  if (status == PICO_ERROR_BADAUTH) {
    return HAL_EAUTH;
  }
  if (status == PICO_ERROR_INSUFFICIENT_RESOURCES) {
    return HAL_ENOMEM;
  }
  if (status == PICO_ERROR_INVALID_STATE ||
      status == PICO_ERROR_PRECONDITION_NOT_MET) {
    return HAL_ESTATE;
  }
  if (status == PICO_ERROR_RESOURCE_IN_USE) {
    return HAL_EBUSY;
  }
  return status == -CYW43_EIO || status == PICO_ERROR_IO ||
                 status == PICO_ERROR_CONNECT_FAILED
             ? HAL_EIO
             : HAL_EHW;
}

hal_status_t jh_rp2040_cyw43_platform_init(uint32_t country_code) {
  if (country_code != (uint32_t)HAL_CYW43_COUNTRY_CODE) {
    return HAL_ECONFIG;
  }

  const jh_rp2040_cyw43_gspi_config_t config = {
      (uint8_t)HAL_CYW43_PIN_CHIP_SELECT,
      (uint8_t)HAL_CYW43_PIN_CLOCK,
      (uint8_t)HAL_CYW43_PIN_WL_ON,
      (uint8_t)HAL_CYW43_PIN_DATA,
      (uint32_t)HAL_CYW43_GSPI_TARGET_HZ,
      (uint32_t)HAL_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256,
      (size_t)HAL_CYW43_MAX_TRANSACTION_BYTES,
  };
#if defined(HAL_CW43_BASELINE_DIAGNOSTICS)
  hal_deb("CW43 init: gSPI transport");
#endif
  hal_status_t status = jh_rp2040_cyw43_gspi_init(&config);
#if defined(HAL_CW43_BASELINE_DIAGNOSTICS)
  hal_deb("CW43 init: gSPI transport status=%s", hal_status_to_string(status));
#endif
  if (status != HAL_OK) {
    return status;
  }

  jh_cyw43_driver_result_t result{};
#if defined(HAL_CW43_BASELINE_DIAGNOSTICS)
  hal_deb("CW43 init: driver start");
#endif
  status = jh_cyw43_driver_start(jh_rp2040_cyw43_gspi_transport(), &result);
#if defined(HAL_CW43_BASELINE_DIAGNOSTICS)
  hal_deb("CW43 init: driver status=%s stage=%s cyw43=%d",
          hal_status_to_string(status),
          jh_cyw43_driver_stage_string(result.stage), result.cyw43_error);
#endif
  if (status == HAL_OK) {
    /*
     * Preserve the established WiFi startup latency. The prior implementation
     * disabled CYW43 power saving after every successful start, whereas the
     * native driver otherwise retains CYW43_DEFAULT_PM (PM2,
     * 200 ms sleep-return window). PM2 noticeably delays inbound traffic
     * such as MQTT commands on an otherwise idle connection.
     */
    (void)cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
  }
  if (status != HAL_OK) {
    (void)jh_rp2040_cyw43_gspi_deinit();
  }
  return status;
}

void jh_rp2040_cyw43_platform_deinit(void) {
  if (jh_cyw43_driver_is_ready()) {
    (void)jh_cyw43_driver_stop();
  }
  if (jh_rp2040_cyw43_gspi_transport() != nullptr) {
    (void)jh_rp2040_cyw43_gspi_deinit();
  }
}

const jh_network_service_port_t *jh_rp2040_cyw43_platform_service_port(void) {
  return &s_service_port;
}

#endif
#endif
