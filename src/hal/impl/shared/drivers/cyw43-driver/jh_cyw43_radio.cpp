#include "jh_cyw43_radio.h"
#include "../../../../hal_config.h"
#include "../../../../hal_target.h"
#include "../../jh_board_runtime.h"

namespace {

#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43

constexpr hal_board_capabilities_t kCyw43Capabilities =
    HAL_BOARD_CAP_CYW43 | (HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND
                               ? HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND
                               : 0u);

extern "C" jh_cyw43_radio_runtime_t *jh_cyw43_radio_backend_runtime(void);

jh_cyw43_radio_runtime_t *runtime(void) {
  jh_cyw43_radio_runtime_t *value = jh_cyw43_radio_backend_runtime();
  return value != nullptr && value->initialized ? value : nullptr;
}

hal_status_t publish_runtime_state(jh_cyw43_radio_runtime_t *value) {
  jh_cyw43_radio_runtime_snapshot_t snapshot{};
  if (value == nullptr) {
    return HAL_EINVAL;
  }
  const hal_status_t snapshot_status =
      jh_cyw43_radio_runtime_snapshot(value, &snapshot);
  if (snapshot_status != HAL_OK) {
    return snapshot_status;
  }
  if (snapshot.state == JH_CYW43_RADIO_STATE_READY) {
    return jh_board_runtime_set_available(kCyw43Capabilities);
  }
  if (snapshot.state == JH_CYW43_RADIO_STATE_FAILED) {
    return jh_board_runtime_set_failed(kCyw43Capabilities);
  }
  return snapshot.state == JH_CYW43_RADIO_STATE_OFF
             ? jh_board_runtime_set_inactive(kCyw43Capabilities)
             : HAL_OK;
}

#endif

} // namespace

extern "C" hal_status_t jh_cyw43_radio_acquire(jh_cyw43_radio_client_t client) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  const hal_status_t capability_status =
      hal_board_require_capabilities(kCyw43Capabilities);
  if (capability_status != HAL_OK && capability_status != HAL_EUNINIT) {
    return capability_status;
  }
  jh_cyw43_radio_runtime_t *value = runtime();
  if (value == nullptr) {
    return HAL_ECONFIG;
  }
  const hal_status_t status = jh_cyw43_radio_runtime_acquire(value, client);
  const hal_status_t publish_status = publish_runtime_state(value);
  if (status == HAL_OK && publish_status != HAL_OK) {
    (void)jh_cyw43_radio_runtime_release(value, client);
    (void)publish_runtime_state(value);
    return publish_status;
  }
  return status != HAL_OK ? status : publish_status;
#else
  (void)client;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t jh_cyw43_radio_release(jh_cyw43_radio_client_t client) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  if (value == nullptr) {
    return HAL_ECONFIG;
  }
  const hal_status_t status = jh_cyw43_radio_runtime_release(value, client);
  const hal_status_t publish_status = publish_runtime_state(value);
  return status != HAL_OK ? status : publish_status;
#else
  (void)client;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t
jh_cyw43_radio_set_invalidation_handler(jh_cyw43_radio_client_t client,
                                        jh_cyw43_radio_invalidation_fn handler,
                                        void *context) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr ? HAL_ECONFIG
                          : jh_cyw43_radio_runtime_set_invalidation_handler(
                                value, client, handler, context);
#else
  (void)client;
  (void)handler;
  (void)context;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t
jh_cyw43_radio_set_service_handler(jh_cyw43_radio_client_t client,
                                   jh_cyw43_radio_service_fn handler,
                                   void *context) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr ? HAL_ECONFIG
                          : jh_cyw43_radio_runtime_set_service_handler(
                                value, client, handler, context);
#else
  (void)client;
  (void)handler;
  (void)context;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t jh_cyw43_radio_service_clients(void) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr ? HAL_ECONFIG
                          : jh_cyw43_radio_runtime_service_clients(value);
#else
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t jh_cyw43_radio_restart(void) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  if (value == nullptr) {
    return HAL_ECONFIG;
  }
  const hal_status_t status = jh_cyw43_radio_runtime_restart(value);
  const hal_status_t publish_status = publish_runtime_state(value);
  return status != HAL_OK ? status : publish_status;
#else
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t jh_cyw43_radio_enter(jh_cyw43_radio_client_t client,
                                             bool require_ipv4) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr
             ? HAL_ECONFIG
             : jh_cyw43_radio_runtime_enter(value, client, require_ipv4);
#else
  (void)client;
  (void)require_ipv4;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t jh_cyw43_radio_leave(void) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr ? HAL_ECONFIG : jh_cyw43_radio_runtime_leave(value);
#else
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t jh_cyw43_radio_service(jh_cyw43_radio_client_t client) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr ? HAL_ECONFIG
                          : jh_cyw43_radio_runtime_service(value, client);
#else
  (void)client;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" hal_status_t
jh_cyw43_radio_snapshot(jh_cyw43_radio_runtime_snapshot_t *out_snapshot) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value == nullptr
             ? HAL_ECONFIG
             : jh_cyw43_radio_runtime_snapshot(value, out_snapshot);
#else
  (void)out_snapshot;
  return HAL_EUNSUPPORTED;
#endif
}

extern "C" bool
jh_cyw43_radio_generation_is_current(jh_cyw43_radio_client_t client,
                                     uint32_t generation) {
#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    HAL_BOARD_HAS_CYW43
  jh_cyw43_radio_runtime_t *value = runtime();
  return value != nullptr && jh_cyw43_radio_runtime_generation_is_current(
                                 value, client, generation);
#else
  (void)client;
  (void)generation;
  return false;
#endif
}
