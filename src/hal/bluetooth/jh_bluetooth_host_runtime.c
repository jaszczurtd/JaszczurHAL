#include "jh_bluetooth_host_runtime.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool valid_profile(jh_bluetooth_host_profile_t profile) {
  return (unsigned)profile < (unsigned)JH_BLUETOOTH_HOST_PROFILE_COUNT;
}

static bool valid_controller(const jh_bluetooth_controller_t *controller) {
  return controller != NULL && controller->start != NULL &&
         controller->stop != NULL && controller->service != NULL;
}

static bool valid_port(const jh_bluetooth_host_port_t *port) {
  return port != NULL && port->prepare != NULL && port->power_on != NULL &&
         port->stop != NULL && port->service != NULL &&
         port->invalidated != NULL;
}

static bool
valid_profile_ops(const jh_bluetooth_host_profile_ops_t *profile_ops) {
  return profile_ops != NULL && profile_ops->start != NULL &&
         profile_ops->stop != NULL;
}

static bool profile_ops_equal(const jh_bluetooth_host_profile_ops_t *left,
                              const jh_bluetooth_host_profile_ops_t *right) {
  return left->context == right->context && left->start == right->start &&
         left->stop == right->stop && left->service == right->service &&
         left->invalidated == right->invalidated;
}

static uint32_t next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static uint32_t total_references(const jh_bluetooth_host_runtime_t *runtime) {
  uint32_t total = 0u;
  for (unsigned index = 0u; index < JH_BLUETOOTH_HOST_PROFILE_COUNT; ++index) {
    total += runtime->profiles[index].references;
  }
  return total;
}

static void clear_slot(jh_bluetooth_host_profile_slot_t *slot) {
  memset(slot, 0, sizeof(*slot));
}

static void reset_stopped_runtime(jh_bluetooth_host_runtime_t *runtime) {
  for (unsigned index = 0u; index < JH_BLUETOOTH_HOST_PROFILE_COUNT; ++index) {
    clear_slot(&runtime->profiles[index]);
  }
  runtime->controller_started = false;
  runtime->base_prepared = false;
  runtime->powered = false;
  runtime->started = false;
  runtime->failed = false;
  runtime->stopping = false;
  runtime->in_service = false;
  runtime->transition = JH_BLUETOOTH_HOST_TRANSITION_NONE;
  runtime->transition_status = HAL_NONE;
}

static hal_status_t
start_host_under_lock(jh_bluetooth_host_runtime_t *runtime,
                      jh_bluetooth_host_profile_slot_t *slot) {
  hal_status_t status = runtime->port->prepare(runtime->port->context);
  if (status != HAL_OK) {
    runtime->port->stop(runtime->port->context);
    return status;
  }
  runtime->base_prepared = true;

  status = slot->ops.start(slot->ops.context);
  if (status != HAL_OK) {
    slot->ops.stop(slot->ops.context);
    runtime->port->stop(runtime->port->context);
    runtime->base_prepared = false;
    return status;
  }
  slot->started = true;

  status = runtime->port->power_on(runtime->port->context);
  if (status != HAL_OK) {
    slot->ops.stop(slot->ops.context);
    slot->started = false;
    runtime->port->stop(runtime->port->context);
    runtime->base_prepared = false;
    return status;
  }
  runtime->powered = true;
  return HAL_OK;
}

static hal_status_t
run_transition_under_lock(jh_bluetooth_host_runtime_t *runtime) {
  jh_bluetooth_host_profile_slot_t *slot =
      &runtime->profiles[runtime->pending_profile];
  hal_status_t status = HAL_OK;
  switch (runtime->transition) {
  case JH_BLUETOOTH_HOST_TRANSITION_START:
    status = start_host_under_lock(runtime, slot);
    break;
  case JH_BLUETOOTH_HOST_TRANSITION_ADD_PROFILE:
    status = slot->ops.start(slot->ops.context);
    if (status == HAL_OK) {
      slot->started = true;
    } else {
      slot->ops.stop(slot->ops.context);
    }
    break;
  case JH_BLUETOOTH_HOST_TRANSITION_REMOVE_PROFILE:
    if (slot->started) {
      slot->ops.stop(slot->ops.context);
      slot->started = false;
    }
    break;
  case JH_BLUETOOTH_HOST_TRANSITION_STOP:
    if (slot->started) {
      slot->ops.stop(slot->ops.context);
      slot->started = false;
    }
    if (runtime->base_prepared) {
      runtime->port->stop(runtime->port->context);
    }
    runtime->base_prepared = false;
    runtime->powered = false;
    break;
  case JH_BLUETOOTH_HOST_TRANSITION_NONE:
  default:
    status = HAL_EINTERNAL;
    break;
  }
  runtime->transition_status = status;
  runtime->transition = JH_BLUETOOTH_HOST_TRANSITION_NONE;
  return status;
}

static hal_status_t service_under_radio_lock(void *context) {
  jh_bluetooth_host_runtime_t *runtime = (jh_bluetooth_host_runtime_t *)context;
  if (runtime == NULL || !runtime->initialized) {
    return HAL_EINVAL;
  }
  if (runtime->in_service) {
    return HAL_EBUSY;
  }
  runtime->in_service = true;

  hal_status_t status = HAL_OK;
  if (runtime->transition != JH_BLUETOOTH_HOST_TRANSITION_NONE) {
    status = run_transition_under_lock(runtime);
  } else if (runtime->failed) {
    status = HAL_EHW;
  } else if (!runtime->started) {
    status = HAL_EUNINIT;
  } else {
    status = runtime->port->service(runtime->port->context);
    for (unsigned index = 0u;
         status == HAL_OK && index < JH_BLUETOOTH_HOST_PROFILE_COUNT; ++index) {
      jh_bluetooth_host_profile_slot_t *slot = &runtime->profiles[index];
      if (slot->started && slot->ops.service != NULL) {
        status = slot->ops.service(slot->ops.context);
      }
    }
  }
  runtime->last_status = status;
  runtime->in_service = false;
  return status;
}

static void controller_invalidated(void *context, uint32_t generation) {
  jh_bluetooth_host_runtime_t *runtime = (jh_bluetooth_host_runtime_t *)context;
  if (runtime == NULL || !runtime->initialized || runtime->stopping) {
    return;
  }
  runtime->port->invalidated(runtime->port->context, generation);
  runtime->generation = next_generation(runtime->generation);
  runtime->failed = true;
  runtime->last_status = HAL_EHW;
  for (unsigned index = 0u; index < JH_BLUETOOTH_HOST_PROFILE_COUNT; ++index) {
    jh_bluetooth_host_profile_slot_t *slot = &runtime->profiles[index];
    if (slot->references != 0u && slot->ops.invalidated != NULL) {
      slot->ops.invalidated(slot->ops.context, runtime->generation);
    }
  }
}

static hal_status_t execute_transition(jh_bluetooth_host_runtime_t *runtime) {
  runtime->transition_status = HAL_NONE;
  const hal_status_t service_status =
      runtime->controller->service(runtime->controller->context);
  if (service_status != HAL_OK) {
    return service_status;
  }
  return runtime->transition == JH_BLUETOOTH_HOST_TRANSITION_NONE &&
                 runtime->transition_status != HAL_NONE
             ? runtime->transition_status
             : HAL_EIO;
}

hal_status_t
jh_bluetooth_host_runtime_init(jh_bluetooth_host_runtime_t *runtime,
                               const jh_bluetooth_controller_t *controller,
                               const jh_bluetooth_host_port_t *port) {
  if (runtime == NULL || !valid_controller(controller) || !valid_port(port)) {
    return HAL_EINVAL;
  }
  if (runtime->initialized) {
    return runtime->controller == controller && runtime->port == port
               ? HAL_OK
               : HAL_EBUSY;
  }
  memset(runtime, 0, sizeof(*runtime));
  runtime->controller = controller;
  runtime->port = port;
  runtime->last_status = HAL_NONE;
  runtime->transition_status = HAL_NONE;
  runtime->initialized = true;
  return HAL_OK;
}

hal_status_t jh_bluetooth_host_runtime_acquire(
    jh_bluetooth_host_runtime_t *runtime, jh_bluetooth_host_profile_t profile,
    const jh_bluetooth_host_profile_ops_t *profile_ops,
    jh_bluetooth_host_reference_t *out_reference) {
  if (runtime == NULL || !runtime->initialized || !valid_profile(profile) ||
      !valid_profile_ops(profile_ops) || out_reference == NULL) {
    return HAL_EINVAL;
  }
  if (out_reference->active) {
    return HAL_EBUSY;
  }
  if (runtime->failed ||
      runtime->transition != JH_BLUETOOTH_HOST_TRANSITION_NONE) {
    return runtime->failed ? HAL_EHW : HAL_EBUSY;
  }

  jh_bluetooth_host_profile_slot_t *slot = &runtime->profiles[profile];
  if (slot->references != 0u) {
    if (!profile_ops_equal(&slot->ops, profile_ops)) {
      return HAL_EBUSY;
    }
    if (slot->references == UINT16_MAX) {
      return HAL_EOVERFLOW;
    }
    ++slot->references;
    *out_reference = (jh_bluetooth_host_reference_t){
        .runtime = runtime,
        .generation = runtime->generation,
        .profile = profile,
        .active = true,
    };
    return HAL_OK;
  }

  const bool first_profile = total_references(runtime) == 0u;
  slot->ops = *profile_ops;
  slot->configured = true;
  runtime->pending_profile = profile;
  runtime->transition = first_profile
                            ? JH_BLUETOOTH_HOST_TRANSITION_START
                            : JH_BLUETOOTH_HOST_TRANSITION_ADD_PROFILE;

  hal_status_t status = HAL_OK;
  if (first_profile) {
    status = runtime->controller->start(runtime->controller->context,
                                        service_under_radio_lock, runtime,
                                        controller_invalidated, runtime);
    if (status == HAL_OK) {
      runtime->controller_started = true;
    }
  }
  if (status == HAL_OK) {
    status = execute_transition(runtime);
  }
  if (status != HAL_OK) {
    if (runtime->controller_started && first_profile) {
      runtime->stopping = true;
      (void)runtime->controller->stop(runtime->controller->context);
      runtime->stopping = false;
    }
    clear_slot(slot);
    runtime->transition = JH_BLUETOOTH_HOST_TRANSITION_NONE;
    if (first_profile) {
      runtime->controller_started = false;
      runtime->base_prepared = false;
      runtime->powered = false;
    }
    runtime->last_status = status;
    return status;
  }

  if (first_profile) {
    runtime->generation = next_generation(runtime->generation);
    runtime->started = true;
  }
  slot->references = 1u;
  runtime->last_status = HAL_OK;
  *out_reference = (jh_bluetooth_host_reference_t){
      .runtime = runtime,
      .generation = runtime->generation,
      .profile = profile,
      .active = true,
  };
  return HAL_OK;
}

hal_status_t
jh_bluetooth_host_runtime_release(jh_bluetooth_host_reference_t *reference) {
  if (reference == NULL || !reference->active || reference->runtime == NULL ||
      !valid_profile(reference->profile)) {
    return HAL_EINVAL;
  }
  jh_bluetooth_host_runtime_t *runtime = reference->runtime;
  jh_bluetooth_host_profile_slot_t *slot =
      &runtime->profiles[reference->profile];
  if (!runtime->initialized || slot->references == 0u) {
    reference->active = false;
    return HAL_ESTATE;
  }

  const bool stale = reference->generation != runtime->generation;
  reference->active = false;
  --slot->references;
  if (slot->references != 0u) {
    return stale ? HAL_ESTATE : HAL_OK;
  }

  const bool last_profile = total_references(runtime) == 0u;
  runtime->pending_profile = reference->profile;
  runtime->transition = last_profile
                            ? JH_BLUETOOTH_HOST_TRANSITION_STOP
                            : JH_BLUETOOTH_HOST_TRANSITION_REMOVE_PROFILE;
  hal_status_t status = execute_transition(runtime);
  if (last_profile) {
    runtime->stopping = true;
    const hal_status_t stop_status =
        runtime->controller_started
            ? runtime->controller->stop(runtime->controller->context)
            : HAL_OK;
    runtime->stopping = false;
    if (status == HAL_OK) {
      status = stop_status;
    }
    runtime->generation = next_generation(runtime->generation);
    reset_stopped_runtime(runtime);
  } else {
    clear_slot(slot);
  }
  runtime->last_status = status;
  return stale && status == HAL_OK ? HAL_ESTATE : status;
}

bool jh_bluetooth_host_reference_is_current(
    const jh_bluetooth_host_reference_t *reference) {
  if (reference == NULL || !reference->active || reference->runtime == NULL ||
      !valid_profile(reference->profile)) {
    return false;
  }
  const jh_bluetooth_host_runtime_t *runtime = reference->runtime;
  return runtime->initialized && runtime->started && !runtime->failed &&
         reference->generation == runtime->generation &&
         runtime->profiles[reference->profile].references != 0u;
}

hal_status_t jh_bluetooth_host_runtime_service(
    const jh_bluetooth_host_reference_t *reference) {
  if (!jh_bluetooth_host_reference_is_current(reference)) {
    return HAL_ESTATE;
  }
  jh_bluetooth_host_runtime_t *runtime = reference->runtime;
  const hal_status_t status =
      runtime->controller->service(runtime->controller->context);
  runtime->last_status = status;
  return status;
}

void jh_bluetooth_host_runtime_snapshot(
    const jh_bluetooth_host_runtime_t *runtime,
    jh_bluetooth_host_snapshot_t *out_snapshot) {
  if (runtime == NULL || out_snapshot == NULL) {
    return;
  }
  *out_snapshot = (jh_bluetooth_host_snapshot_t){
      .generation = runtime->generation,
      .total_references = total_references(runtime),
      .last_status = runtime->last_status,
      .initialized = runtime->initialized,
      .started = runtime->started,
      .failed = runtime->failed,
  };
  for (unsigned index = 0u; index < JH_BLUETOOTH_HOST_PROFILE_COUNT; ++index) {
    out_snapshot->profile_references[index] =
        runtime->profiles[index].references;
  }
}
