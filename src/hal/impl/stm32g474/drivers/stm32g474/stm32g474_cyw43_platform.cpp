#include "../../../../hal_target.h"

#if HAL_TARGET_IS_STM32G474
#include "../../../../hal_config.h"

#if (defined(HAL_ENABLE_NETWORK_CORE) || defined(JH_BLUETOOTH_BTSTACK)) &&     \
    defined(HAL_NETWORK_BACKEND_CYW43) && defined(HAL_CYW43_BUS_STM32_GSPI) && \
    defined(HAL_CYW43_STACK_LWIP)

#include "../../../../hal_sync.h"
#include "../../../shared/drivers/cyw43-driver/jh_cyw43_driver.h"
#include "../../../shared/drivers/cyw43-driver/jh_cyw43_lwip.h"
#include "../../../shared/hal_mutex_once.h"
#include "stm32g474_cyw43_gspi.h"
#include "stm32g474_cyw43_platform.h"

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

namespace {

hal_mutex_t s_state_mutex;
hal_mutex_t s_stack_mutex;
jh_cyw43_radio_runtime_t s_radio_runtime{};

void ensure_mutexes(void) {
  (void)jh_hal_mutex_create_once(&s_state_mutex);
  (void)jh_hal_mutex_create_once(&s_stack_mutex);
}

void state_lock(void *) {
  ensure_mutexes();
  hal_mutex_lock(s_state_mutex);
}

void state_unlock(void *) { hal_mutex_unlock(s_state_mutex); }

jh_network_context_owner_t current_owner(void *) {
#if defined(HAL_ENABLE_FREERTOS)
  const TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return task == nullptr ? 1u
                         : reinterpret_cast<jh_network_context_owner_t>(task);
#else
  return 1u;
#endif
}

hal_status_t stack_enter(void *) {
  ensure_mutexes();
  hal_mutex_lock(s_stack_mutex);
  if (!jh_cyw43_driver_is_ready()) {
    hal_mutex_unlock(s_stack_mutex);
    return HAL_EUNINIT;
  }
  return HAL_OK;
}

void stack_leave(void *) { hal_mutex_unlock(s_stack_mutex); }

hal_status_t service(void *) { return jh_cyw43_lwip_service(); }

bool ipv4_ready(void *) {
  jh_cyw43_lwip_snapshot_t snapshot{};
  return jh_cyw43_lwip_get_snapshot(&snapshot) == HAL_OK &&
         snapshot.dhcp_bound && snapshot.ipv4 != 0u;
}

const jh_network_service_port_t s_service_port = {
    nullptr,     state_lock,  state_unlock, current_owner,
    stack_enter, stack_leave, service,      ipv4_ready,
};

hal_status_t radio_start(void *) {
  const jh_stm32g474_cyw43_gspi_config_t config = {
      (uint8_t)HAL_CYW43_PIN_CHIP_SELECT,
      (uint8_t)HAL_CYW43_PIN_CLOCK,
      (uint8_t)HAL_CYW43_PIN_WL_ON,
      (uint8_t)HAL_CYW43_PIN_DATA,
      (size_t)HAL_CYW43_MAX_TRANSACTION_BYTES,
  };
  hal_status_t status = jh_stm32g474_cyw43_gspi_init(&config);
  if (status != HAL_OK) {
    return status;
  }
  jh_cyw43_driver_result_t result{};
  status = jh_cyw43_driver_start(jh_stm32g474_cyw43_gspi_transport(), &result);
  if (status != HAL_OK) {
    (void)jh_stm32g474_cyw43_gspi_deinit();
  }
  return status;
}

hal_status_t radio_stop(void *) {
  hal_status_t status = HAL_OK;
  if (jh_cyw43_driver_is_ready()) {
    status = jh_cyw43_driver_stop();
  }
  if (jh_stm32g474_cyw43_gspi_transport() != nullptr) {
    const hal_status_t transport_status = jh_stm32g474_cyw43_gspi_deinit();
    if (status == HAL_OK) {
      status = transport_status;
    }
  }
  return status;
}

const jh_cyw43_radio_runtime_port_t s_radio_port = {
    nullptr,
    &s_service_port,
    radio_start,
    radio_stop,
};

} // namespace

extern "C" jh_cyw43_radio_runtime_t *jh_cyw43_radio_backend_runtime(void) {
  ensure_mutexes();
  hal_mutex_lock(s_state_mutex);
  hal_status_t status = HAL_OK;
  if (!s_radio_runtime.initialized) {
    status = jh_cyw43_radio_runtime_init(&s_radio_runtime, &s_radio_port);
  }
  hal_mutex_unlock(s_state_mutex);
  return status == HAL_OK ? &s_radio_runtime : nullptr;
}

#endif
#endif
