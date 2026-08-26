#include "jh_cyw43_radio_runtime.h"

#include <limits.h>
#include <stddef.h>

namespace {

bool valid_client(jh_cyw43_radio_client_t client) {
  return client >= JH_CYW43_RADIO_CLIENT_WIFI &&
         client < JH_CYW43_RADIO_CLIENT_COUNT;
}

bool valid_port(const jh_cyw43_radio_runtime_port_t *port) {
  return port != nullptr && port->service_port != nullptr &&
         port->start != nullptr && port->stop != nullptr;
}

const jh_network_service_port_t *
service_port(const jh_cyw43_radio_runtime_t *runtime) {
  return runtime->port->service_port;
}

bool has_references(const jh_cyw43_radio_runtime_t *runtime) {
  return runtime->references[JH_CYW43_RADIO_CLIENT_WIFI] != 0u ||
         runtime->references[JH_CYW43_RADIO_CLIENT_BLUETOOTH] != 0u;
}

void notify_active_clients(jh_cyw43_radio_runtime_t *runtime,
                           uint32_t generation) {
  for (unsigned index = 0u; index < JH_CYW43_RADIO_CLIENT_COUNT; ++index) {
    if (runtime->references[index] != 0u &&
        runtime->invalidation[index] != nullptr) {
      runtime->invalidation[index](runtime->invalidation_context[index],
                                   generation);
    }
  }
}

void notify_client(jh_cyw43_radio_runtime_t *runtime,
                   jh_cyw43_radio_client_t client, uint32_t generation) {
  if (runtime->invalidation[client] != nullptr) {
    runtime->invalidation[client](runtime->invalidation_context[client],
                                  generation);
  }
}

void set_state(jh_cyw43_radio_runtime_t *runtime,
               jh_cyw43_radio_state_t state) {
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  runtime->state = state;
  port->state_unlock(port->context);
}

hal_status_t stop_while_holding_stack(jh_cyw43_radio_runtime_t *runtime) {
  const jh_network_service_port_t *port = service_port(runtime);
  const hal_status_t enter_status = port->stack_enter(port->context);
  if (enter_status != HAL_OK) {
    return enter_status;
  }
  const hal_status_t stop_status = runtime->port->stop(runtime->port->context);
  port->stack_leave(port->context);
  return stop_status;
}

} // namespace

extern "C" hal_status_t
jh_cyw43_radio_runtime_init(jh_cyw43_radio_runtime_t *runtime,
                            const jh_cyw43_radio_runtime_port_t *port) {
  if (runtime == nullptr || !valid_port(port)) {
    return HAL_EINVAL;
  }
  if (runtime->initialized) {
    return runtime->port == port ? HAL_OK : HAL_EBUSY;
  }
  runtime->port = port;
  runtime->state = JH_CYW43_RADIO_STATE_OFF;
  for (unsigned index = 0u; index < JH_CYW43_RADIO_CLIENT_COUNT; ++index) {
    runtime->references[index] = 0u;
    runtime->invalidation[index] = nullptr;
    runtime->invalidation_context[index] = nullptr;
    runtime->client_service[index] = nullptr;
    runtime->client_service_context[index] = nullptr;
  }
  const hal_status_t status =
      jh_network_service_init(&runtime->service, port->service_port);
  if (status == HAL_OK) {
    runtime->initialized = true;
  }
  return status;
}

extern "C" hal_status_t jh_cyw43_radio_runtime_set_service_handler(
    jh_cyw43_radio_runtime_t *runtime, jh_cyw43_radio_client_t client,
    jh_cyw43_radio_service_fn handler, void *context) {
  if (runtime == nullptr || !runtime->initialized || !valid_client(client)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  if (runtime->references[client] != 0u) {
    port->state_unlock(port->context);
    return HAL_EBUSY;
  }
  runtime->client_service[client] = handler;
  runtime->client_service_context[client] = context;
  port->state_unlock(port->context);
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_service_clients(jh_cyw43_radio_runtime_t *runtime) {
  if (runtime == nullptr || !runtime->initialized) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  jh_cyw43_radio_service_fn handlers[JH_CYW43_RADIO_CLIENT_COUNT]{};
  void *contexts[JH_CYW43_RADIO_CLIENT_COUNT]{};
  port->state_lock(port->context);
  for (unsigned index = 0u; index < JH_CYW43_RADIO_CLIENT_COUNT; ++index) {
    if (runtime->state == JH_CYW43_RADIO_STATE_READY &&
        runtime->references[index] != 0u) {
      handlers[index] = runtime->client_service[index];
      contexts[index] = runtime->client_service_context[index];
    }
  }
  port->state_unlock(port->context);

  for (unsigned index = 0u; index < JH_CYW43_RADIO_CLIENT_COUNT; ++index) {
    if (handlers[index] != nullptr) {
      const hal_status_t status = handlers[index](contexts[index]);
      if (status != HAL_OK) {
        return status;
      }
    }
  }
  return HAL_OK;
}

extern "C" hal_status_t jh_cyw43_radio_runtime_set_invalidation_handler(
    jh_cyw43_radio_runtime_t *runtime, jh_cyw43_radio_client_t client,
    jh_cyw43_radio_invalidation_fn handler, void *context) {
  if (runtime == nullptr || !runtime->initialized || !valid_client(client)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  if (runtime->references[client] != 0u) {
    port->state_unlock(port->context);
    return HAL_EBUSY;
  }
  runtime->invalidation[client] = handler;
  runtime->invalidation_context[client] = context;
  port->state_unlock(port->context);
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_acquire(jh_cyw43_radio_runtime_t *runtime,
                               jh_cyw43_radio_client_t client) {
  if (runtime == nullptr || !runtime->initialized || !valid_client(client)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  if (runtime->state == JH_CYW43_RADIO_STATE_FAILED) {
    port->state_unlock(port->context);
    return HAL_EHW;
  }
  if (runtime->state == JH_CYW43_RADIO_STATE_READY) {
    if (runtime->references[client] == UINT16_MAX) {
      port->state_unlock(port->context);
      return HAL_EOVERFLOW;
    }
    ++runtime->references[client];
    port->state_unlock(port->context);
    return HAL_OK;
  }
  if (runtime->state != JH_CYW43_RADIO_STATE_OFF) {
    port->state_unlock(port->context);
    return HAL_EBUSY;
  }
  runtime->state = JH_CYW43_RADIO_STATE_STARTING;
  port->state_unlock(port->context);

  hal_status_t status = runtime->port->start(runtime->port->context);
  if (status == HAL_OK) {
    status = jh_network_service_start(&runtime->service);
  }
  if (status != HAL_OK) {
    if (runtime->service.running) {
      (void)jh_network_service_stop(&runtime->service);
    }
    (void)runtime->port->stop(runtime->port->context);
    set_state(runtime, JH_CYW43_RADIO_STATE_FAILED);
    return status;
  }

  port->state_lock(port->context);
  runtime->references[client] = 1u;
  runtime->state = JH_CYW43_RADIO_STATE_READY;
  port->state_unlock(port->context);
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_release(jh_cyw43_radio_runtime_t *runtime,
                               jh_cyw43_radio_client_t client) {
  if (runtime == nullptr || !runtime->initialized || !valid_client(client)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  if (runtime->state != JH_CYW43_RADIO_STATE_READY ||
      runtime->references[client] == 0u) {
    port->state_unlock(port->context);
    return runtime->state == JH_CYW43_RADIO_STATE_FAILED ? HAL_EHW : HAL_ESTATE;
  }
  if (runtime->references[client] > 1u ||
      (runtime->references[client] == 1u &&
       runtime->references[client == JH_CYW43_RADIO_CLIENT_WIFI
                               ? JH_CYW43_RADIO_CLIENT_BLUETOOTH
                               : JH_CYW43_RADIO_CLIENT_WIFI] != 0u)) {
    --runtime->references[client];
    const bool became_inactive = runtime->references[client] == 0u;
    const uint32_t generation = runtime->service.generation;
    const jh_cyw43_radio_invalidation_fn invalidation =
        runtime->invalidation[client];
    void *const invalidation_context = runtime->invalidation_context[client];
    port->state_unlock(port->context);
    if (became_inactive && invalidation != nullptr) {
      invalidation(invalidation_context, generation);
    }
    return HAL_OK;
  }
  runtime->state = JH_CYW43_RADIO_STATE_STOPPING;
  port->state_unlock(port->context);

  hal_status_t status = jh_network_service_try_stop(&runtime->service);
  if (status != HAL_OK) {
    set_state(runtime, JH_CYW43_RADIO_STATE_READY);
    return status;
  }
  notify_client(runtime, client, runtime->service.generation);
  const hal_status_t stop_status = stop_while_holding_stack(runtime);
  port->state_lock(port->context);
  runtime->references[client] = 0u;
  runtime->state = stop_status == HAL_OK ? JH_CYW43_RADIO_STATE_OFF
                                         : JH_CYW43_RADIO_STATE_FAILED;
  port->state_unlock(port->context);
  return stop_status;
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_restart(jh_cyw43_radio_runtime_t *runtime) {
  if (runtime == nullptr || !runtime->initialized) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  const bool failed = runtime->state == JH_CYW43_RADIO_STATE_FAILED;
  const bool can_restart =
      runtime->state == JH_CYW43_RADIO_STATE_READY && has_references(runtime);
  if (can_restart) {
    runtime->state = JH_CYW43_RADIO_STATE_STOPPING;
  }
  port->state_unlock(port->context);
  if (!can_restart) {
    return failed ? HAL_EHW : HAL_ESTATE;
  }

  hal_status_t status = jh_network_service_try_stop(&runtime->service);
  if (status != HAL_OK) {
    set_state(runtime, JH_CYW43_RADIO_STATE_READY);
    return status;
  }
  notify_active_clients(runtime, runtime->service.generation);
  status = stop_while_holding_stack(runtime);
  if (status != HAL_OK) {
    set_state(runtime, JH_CYW43_RADIO_STATE_FAILED);
    return status;
  }

  set_state(runtime, JH_CYW43_RADIO_STATE_STARTING);
  status = runtime->port->start(runtime->port->context);
  if (status == HAL_OK) {
    status = jh_network_service_start(&runtime->service);
  }
  if (status != HAL_OK) {
    (void)runtime->port->stop(runtime->port->context);
  }
  set_state(runtime, status == HAL_OK ? JH_CYW43_RADIO_STATE_READY
                                      : JH_CYW43_RADIO_STATE_FAILED);
  return status;
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_enter(jh_cyw43_radio_runtime_t *runtime,
                             jh_cyw43_radio_client_t client,
                             bool require_ipv4) {
  if (runtime == nullptr || !runtime->initialized || !valid_client(client)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  const bool failed = runtime->state == JH_CYW43_RADIO_STATE_FAILED;
  const bool active = runtime->state == JH_CYW43_RADIO_STATE_READY &&
                      runtime->references[client] != 0u;
  port->state_unlock(port->context);
  if (failed) {
    return HAL_EHW;
  }
  return active ? jh_network_service_enter(&runtime->service, require_ipv4)
                : HAL_EUNINIT;
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_leave(jh_cyw43_radio_runtime_t *runtime) {
  return runtime == nullptr || !runtime->initialized
             ? HAL_EINVAL
             : jh_network_service_leave(&runtime->service);
}

extern "C" hal_status_t
jh_cyw43_radio_runtime_service(jh_cyw43_radio_runtime_t *runtime,
                               jh_cyw43_radio_client_t client) {
  const hal_status_t status =
      jh_cyw43_radio_runtime_enter(runtime, client, false);
  if (status != HAL_OK) {
    return status;
  }
  return jh_cyw43_radio_runtime_leave(runtime);
}

extern "C" hal_status_t jh_cyw43_radio_runtime_snapshot(
    jh_cyw43_radio_runtime_t *runtime,
    jh_cyw43_radio_runtime_snapshot_t *out_snapshot) {
  if (runtime == nullptr || !runtime->initialized || out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  out_snapshot->state = runtime->state;
  out_snapshot->generation = runtime->service.generation;
  out_snapshot->wifi_references =
      runtime->references[JH_CYW43_RADIO_CLIENT_WIFI];
  out_snapshot->bluetooth_references =
      runtime->references[JH_CYW43_RADIO_CLIENT_BLUETOOTH];
  port->state_unlock(port->context);
  return HAL_OK;
}

extern "C" bool
jh_cyw43_radio_runtime_generation_is_current(jh_cyw43_radio_runtime_t *runtime,
                                             jh_cyw43_radio_client_t client,
                                             uint32_t generation) {
  if (runtime == nullptr || !runtime->initialized || !valid_client(client) ||
      generation == 0u) {
    return false;
  }
  const jh_network_service_port_t *port = service_port(runtime);
  port->state_lock(port->context);
  const bool current = runtime->state == JH_CYW43_RADIO_STATE_READY &&
                       runtime->references[client] != 0u &&
                       runtime->service.generation == generation;
  port->state_unlock(port->context);
  return current;
}
