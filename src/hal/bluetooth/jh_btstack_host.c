#include "jh_btstack_host.h"

#include "btstack_memory.h"
#include "hci.h"
#include "jh_bluetooth_controller.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "jh_btstack_run_loop.h"
#include "l2cap.h"

#include <stdbool.h>
#include <stddef.h>

static jh_bluetooth_host_runtime_t s_runtime;
static bool s_memory_ready;
static bool s_hci_ready;
static bool s_l2cap_ready;

static hal_status_t host_prepare(void *context) {
  (void)context;
  if (s_memory_ready || s_hci_ready || s_l2cap_ready) {
    return HAL_EBUSY;
  }
  btstack_memory_init();
  s_memory_ready = true;
  hal_status_t status = jh_btstack_run_loop_init();
  if (status != HAL_OK) {
    btstack_memory_deinit();
    s_memory_ready = false;
    return status;
  }
  hci_init(jh_btstack_cyw43_hci_transport_instance(), NULL);
  s_hci_ready = true;
  l2cap_init();
  s_l2cap_ready = true;
  return HAL_OK;
}

static hal_status_t host_power_on(void *context) {
  (void)context;
  return hci_power_control(HCI_POWER_ON) == 0 ? HAL_OK : HAL_EIO;
}

static void host_stop(void *context) {
  (void)context;
  if (s_l2cap_ready) {
    l2cap_deinit();
    s_l2cap_ready = false;
  }
  if (s_hci_ready) {
    hci_close();
    s_hci_ready = false;
  }
  if (s_memory_ready) {
    btstack_memory_deinit();
    s_memory_ready = false;
  }
  jh_btstack_run_loop_deinit();
}

static hal_status_t host_service(void *context) {
  return jh_btstack_run_loop_service_once(context);
}

static void host_invalidated(void *context, uint32_t generation) {
  jh_btstack_run_loop_invalidate(context, generation);
  jh_btstack_cyw43_transport_invalidate();
}

static const jh_bluetooth_host_port_t s_port = {
    .context = NULL,
    .prepare = host_prepare,
    .power_on = host_power_on,
    .stop = host_stop,
    .service = host_service,
    .invalidated = host_invalidated,
};

static hal_status_t ensure_runtime(void) {
  const jh_bluetooth_controller_t *controller =
      jh_bluetooth_controller_backend();
  return jh_bluetooth_host_runtime_init(&s_runtime, controller, &s_port);
}

hal_status_t
jh_btstack_host_acquire(jh_bluetooth_host_profile_t profile,
                        const jh_bluetooth_host_profile_ops_t *profile_ops,
                        jh_bluetooth_host_reference_t *out_reference) {
  const hal_status_t status = ensure_runtime();
  return status == HAL_OK ? jh_bluetooth_host_runtime_acquire(
                                &s_runtime, profile, profile_ops, out_reference)
                          : status;
}

hal_status_t jh_btstack_host_release(jh_bluetooth_host_reference_t *reference) {
  return jh_bluetooth_host_runtime_release(reference);
}

hal_status_t
jh_btstack_host_service(const jh_bluetooth_host_reference_t *reference) {
  return jh_bluetooth_host_runtime_service(reference);
}

void jh_btstack_host_snapshot(jh_bluetooth_host_snapshot_t *out_snapshot) {
  jh_bluetooth_host_runtime_snapshot(&s_runtime, out_snapshot);
}
