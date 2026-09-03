#include <hal/bluetooth/jh_bluetooth_stage1_probe.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

#include <stdbool.h>
#include <stdint.h>

static uint32_t s_last_report_ms;
static hal_status_t s_start_status = HAL_NONE;

static void report_status(void) {
  jh_bluetooth_stage1_snapshot_t snapshot = {0};
  jh_bluetooth_stage1_snapshot(&snapshot);
  deb("JHBT1 start=%s ready=%u advertising=%u connected=%u connections=%lu "
      "writes=%lu rx=%lu(event=%lu,acl=%lu) tx=%lu(cmd=%lu,acl=%lu) "
      "drain_limited=%lu reason=0x%02x host_buf=0x%02x flow=0x%02x "
      "status=%s transport=%s",
      hal_status_to_string(s_start_status), snapshot.controller_ready ? 1u : 0u,
      snapshot.advertising ? 1u : 0u, snapshot.connected ? 1u : 0u,
      (unsigned long)snapshot.connection_count,
      (unsigned long)snapshot.writes_received,
      (unsigned long)snapshot.rx_packets,
      (unsigned long)snapshot.rx_event_packets,
      (unsigned long)snapshot.rx_acl_packets,
      (unsigned long)snapshot.tx_packets,
      (unsigned long)snapshot.tx_command_packets,
      (unsigned long)snapshot.tx_acl_packets,
      (unsigned long)snapshot.drain_budget_hits,
      (unsigned)snapshot.last_disconnect_reason,
      (unsigned)snapshot.host_buffer_size_status,
      (unsigned)snapshot.controller_to_host_flow_control_status,
      hal_status_to_string(snapshot.last_status),
      hal_status_to_string(snapshot.transport_status));
}

void app_start(void) {
  hal_debug_init_default();
  s_start_status = jh_bluetooth_stage1_start();
  report_status();
}

void app_task0(void) {
  if (s_start_status == HAL_OK) {
    const hal_status_t status = jh_bluetooth_stage1_service();
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
