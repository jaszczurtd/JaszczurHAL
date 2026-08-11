#include "jh_network_service.h"

static bool port_is_valid(const jh_network_service_port_t *port) {
  return port != nullptr && port->state_lock != nullptr &&
         port->state_unlock != nullptr && port->current_owner != nullptr &&
         port->stack_enter != nullptr && port->stack_leave != nullptr &&
         port->service != nullptr && port->ipv4_ready != nullptr;
}

static uint32_t next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

hal_status_t jh_network_service_init(jh_network_service_t *service,
                                     const jh_network_service_port_t *port) {
  if (service == nullptr || !port_is_valid(port)) {
    return HAL_EINVAL;
  }
  service->port = port;
  service->generation = 0u;
  service->owner = 0u;
  service->depth = 0u;
  service->pending_operations = 0u;
  service->running = false;
  service->stopping = false;
  return HAL_OK;
}

hal_status_t jh_network_service_start(jh_network_service_t *service) {
  if (service == nullptr || !port_is_valid(service->port)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service->port;
  port->state_lock(port->context);
  if (service->depth != 0u || (service->running && !service->stopping)) {
    port->state_unlock(port->context);
    return HAL_EBUSY;
  }
  service->generation = next_generation(service->generation);
  service->owner = 0u;
  service->pending_operations = 0u;
  service->running = true;
  service->stopping = false;
  port->state_unlock(port->context);
  return HAL_OK;
}

static hal_status_t stop_service(jh_network_service_t *service,
                                 bool preserve_when_busy) {
  if (service == nullptr || !port_is_valid(service->port)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service->port;
  port->state_lock(port->context);
  const bool context_active = service->depth != 0u;
  if (context_active && preserve_when_busy) {
    port->state_unlock(port->context);
    return HAL_EBUSY;
  }
  service->running = false;
  service->stopping = true;
  service->generation = next_generation(service->generation);
  service->pending_operations = 0u;
  port->state_unlock(port->context);
  return context_active ? HAL_EBUSY : HAL_OK;
}

hal_status_t jh_network_service_stop(jh_network_service_t *service) {
  return stop_service(service, false);
}

hal_status_t jh_network_service_try_stop(jh_network_service_t *service) {
  return stop_service(service, true);
}

bool jh_network_service_is_quiescent(jh_network_service_t *service) {
  if (service == nullptr || !port_is_valid(service->port)) {
    return false;
  }
  const jh_network_service_port_t *port = service->port;
  port->state_lock(port->context);
  const bool quiescent =
      service->depth == 0u && service->pending_operations == 0u;
  port->state_unlock(port->context);
  return quiescent;
}

hal_status_t jh_network_service_enter(jh_network_service_t *service,
                                      bool require_ipv4) {
  if (service == nullptr || !port_is_valid(service->port)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service->port;
  const jh_network_context_owner_t owner = port->current_owner(port->context);
  if (owner == 0u) {
    return HAL_ESTATE;
  }

  port->state_lock(port->context);
  if (!service->running || service->stopping) {
    port->state_unlock(port->context);
    return HAL_ESTATE;
  }
  if (service->depth != 0u && service->owner == owner) {
    if (require_ipv4 && !port->ipv4_ready(port->context)) {
      port->state_unlock(port->context);
      return HAL_ESTATE;
    }
    ++service->depth;
    port->state_unlock(port->context);
    return HAL_OK;
  }
  port->state_unlock(port->context);

  hal_status_t status = port->stack_enter(port->context);
  if (status != HAL_OK) {
    return status;
  }

  port->state_lock(port->context);
  if (!service->running || service->stopping) {
    port->state_unlock(port->context);
    port->stack_leave(port->context);
    return HAL_ESTATE;
  }
  if (service->depth != 0u) {
    port->state_unlock(port->context);
    port->stack_leave(port->context);
    return HAL_EBUSY;
  }
  service->owner = owner;
  service->depth = 1u;
  port->state_unlock(port->context);

  const hal_status_t service_status = port->service(port->context);
  if (service_status != HAL_OK) {
    (void)jh_network_service_leave(service);
    return service_status;
  }
  port->state_lock(port->context);
  const bool still_running = service->running && !service->stopping &&
                             service->depth != 0u && service->owner == owner;
  port->state_unlock(port->context);
  if (!still_running) {
    (void)jh_network_service_leave(service);
    return HAL_ESTATE;
  }
  if (require_ipv4 && !port->ipv4_ready(port->context)) {
    (void)jh_network_service_leave(service);
    return HAL_ESTATE;
  }
  return HAL_OK;
}

hal_status_t jh_network_service_leave(jh_network_service_t *service) {
  if (service == nullptr || !port_is_valid(service->port)) {
    return HAL_EINVAL;
  }
  const jh_network_service_port_t *port = service->port;
  const jh_network_context_owner_t owner = port->current_owner(port->context);
  bool leave_stack = false;

  port->state_lock(port->context);
  if (service->depth == 0u || service->owner != owner) {
    port->state_unlock(port->context);
    return HAL_ESTATE;
  }
  --service->depth;
  if (service->depth == 0u) {
    service->owner = 0u;
    leave_stack = true;
  }
  port->state_unlock(port->context);

  if (leave_stack) {
    port->stack_leave(port->context);
  }
  return HAL_OK;
}

hal_status_t jh_network_operation_begin(jh_network_service_t *service,
                                        jh_network_operation_t *operation) {
  if (operation == nullptr) {
    return HAL_EINVAL;
  }
  operation->service = nullptr;
  operation->generation = 0u;
  operation->active = false;
  if (service == nullptr || !port_is_valid(service->port)) {
    return HAL_EINVAL;
  }

  const jh_network_service_port_t *port = service->port;
  port->state_lock(port->context);
  if (!service->running || service->stopping) {
    port->state_unlock(port->context);
    return HAL_ESTATE;
  }
  ++service->pending_operations;
  operation->service = service;
  operation->generation = service->generation;
  operation->active = true;
  port->state_unlock(port->context);
  return HAL_OK;
}

static bool finish_operation(jh_network_operation_t *operation) {
  if (operation == nullptr || !operation->active ||
      operation->service == nullptr) {
    return false;
  }
  jh_network_service_t *service = operation->service;
  if (!port_is_valid(service->port)) {
    operation->active = false;
    return false;
  }

  const jh_network_service_port_t *port = service->port;
  port->state_lock(port->context);
  const bool current = operation->generation == service->generation &&
                       service->running && !service->stopping &&
                       service->pending_operations > 0u;
  if (current) {
    --service->pending_operations;
  }
  operation->service = nullptr;
  operation->generation = 0u;
  operation->active = false;
  port->state_unlock(port->context);
  return current;
}

bool jh_network_operation_complete(jh_network_operation_t *operation) {
  return finish_operation(operation);
}

bool jh_network_operation_cancel(jh_network_operation_t *operation) {
  return finish_operation(operation);
}
