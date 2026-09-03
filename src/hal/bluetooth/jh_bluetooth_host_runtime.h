#pragma once

#include "hal/core/hal_status.h"
#include "jh_bluetooth_controller.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_BLUETOOTH_HOST_PROFILE_BLE = 0,
  JH_BLUETOOTH_HOST_PROFILE_CLASSIC,
  JH_BLUETOOTH_HOST_PROFILE_COUNT,
} jh_bluetooth_host_profile_t;

/* Private C5/C6 fixture compatibility. */
#define JH_BLUETOOTH_HOST_PROFILE_CLASSIC_HID JH_BLUETOOTH_HOST_PROFILE_CLASSIC

typedef struct {
  void *context;
  hal_status_t (*start)(void *context);
  void (*stop)(void *context);
  hal_status_t (*service)(void *context);
  void (*invalidated)(void *context, uint32_t generation);
} jh_bluetooth_host_profile_ops_t;

typedef struct {
  void *context;
  hal_status_t (*prepare)(void *context);
  hal_status_t (*power_on)(void *context);
  void (*stop)(void *context);
  hal_status_t (*service)(void *context);
  void (*invalidated)(void *context, uint32_t generation);
} jh_bluetooth_host_port_t;

typedef struct jh_bluetooth_host_runtime jh_bluetooth_host_runtime_t;

typedef struct {
  jh_bluetooth_host_runtime_t *runtime;
  uint32_t generation;
  jh_bluetooth_host_profile_t profile;
  bool active;
} jh_bluetooth_host_reference_t;

typedef struct {
  jh_bluetooth_host_profile_ops_t ops;
  uint16_t references;
  bool configured;
  bool started;
} jh_bluetooth_host_profile_slot_t;

typedef enum {
  JH_BLUETOOTH_HOST_TRANSITION_NONE = 0,
  JH_BLUETOOTH_HOST_TRANSITION_START,
  JH_BLUETOOTH_HOST_TRANSITION_ADD_PROFILE,
  JH_BLUETOOTH_HOST_TRANSITION_REMOVE_PROFILE,
  JH_BLUETOOTH_HOST_TRANSITION_STOP,
} jh_bluetooth_host_transition_t;

struct jh_bluetooth_host_runtime {
  const jh_bluetooth_controller_t *controller;
  const jh_bluetooth_host_port_t *port;
  jh_bluetooth_host_profile_slot_t profiles[JH_BLUETOOTH_HOST_PROFILE_COUNT];
  uint32_t generation;
  hal_status_t last_status;
  hal_status_t transition_status;
  jh_bluetooth_host_profile_t pending_profile;
  jh_bluetooth_host_transition_t transition;
  bool initialized;
  bool controller_started;
  bool base_prepared;
  bool powered;
  bool started;
  bool failed;
  bool stopping;
  bool in_service;
};

typedef struct {
  uint32_t generation;
  uint32_t total_references;
  uint16_t profile_references[JH_BLUETOOTH_HOST_PROFILE_COUNT];
  hal_status_t last_status;
  bool initialized;
  bool started;
  bool failed;
} jh_bluetooth_host_snapshot_t;

hal_status_t
jh_bluetooth_host_runtime_init(jh_bluetooth_host_runtime_t *runtime,
                               const jh_bluetooth_controller_t *controller,
                               const jh_bluetooth_host_port_t *port);
/** Acquire a profile using a zero-initialized output reference. */
hal_status_t jh_bluetooth_host_runtime_acquire(
    jh_bluetooth_host_runtime_t *runtime, jh_bluetooth_host_profile_t profile,
    const jh_bluetooth_host_profile_ops_t *profile_ops,
    jh_bluetooth_host_reference_t *out_reference);
hal_status_t
jh_bluetooth_host_runtime_release(jh_bluetooth_host_reference_t *reference);
hal_status_t jh_bluetooth_host_runtime_service(
    const jh_bluetooth_host_reference_t *reference);
bool jh_bluetooth_host_reference_is_current(
    const jh_bluetooth_host_reference_t *reference);
void jh_bluetooth_host_runtime_snapshot(
    const jh_bluetooth_host_runtime_t *runtime,
    jh_bluetooth_host_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
