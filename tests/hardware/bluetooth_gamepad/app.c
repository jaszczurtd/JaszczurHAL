#include <hal/bluetooth/jh_bluetooth_classic_hid_probe.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

#include <stdint.h>

static uint32_t s_last_report_ms;
static hal_status_t s_start_status = HAL_NONE;

static void report_status(void) {
  jh_bluetooth_classic_hid_probe_snapshot_t snapshot = {0};
  jh_bluetooth_classic_hid_probe_snapshot(&snapshot);
  deb("JHBT4 start=%s ready=%u profile=%u hid_events=%lu rejected=%lu "
      "rx=%lu(event=%lu,acl=%lu) tx=%lu(cmd=%lu,acl=%lu) drain_limited=%lu "
      "status=%s transport=%s",
      hal_status_to_string(s_start_status), snapshot.controller_ready ? 1u : 0u,
      snapshot.profile_ready ? 1u : 0u, (unsigned long)snapshot.hid_events,
      (unsigned long)snapshot.rejected_incoming_connections,
      (unsigned long)snapshot.rx_packets,
      (unsigned long)snapshot.rx_event_packets,
      (unsigned long)snapshot.rx_acl_packets,
      (unsigned long)snapshot.tx_packets,
      (unsigned long)snapshot.tx_command_packets,
      (unsigned long)snapshot.tx_acl_packets,
      (unsigned long)snapshot.drain_budget_hits,
      hal_status_to_string(snapshot.last_status),
      hal_status_to_string(snapshot.transport_status));
}

void app_start(void) {
  debugInit();
  s_start_status = jh_bluetooth_classic_hid_probe_start();
  report_status();
}

void app_task0(void) {
  if (s_start_status == HAL_OK) {
    const hal_status_t status = jh_bluetooth_classic_hid_probe_service();
    if (status != HAL_OK) {
      s_start_status = status;
    }
  }

  const uint32_t now = hal_millis();
  if (now - s_last_report_ms >= 1000u) {
    s_last_report_ms = now;
    report_status();
  }
  hal_delay_ms(1u);
}
