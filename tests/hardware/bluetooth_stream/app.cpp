#include <JaszczurHAL.h>
#include <tools.h>

#include <string.h>

namespace {

constexpr uint16_t kAdvertisingInterval100Ms = 0x00a0u;
constexpr uint8_t kAdTypeFlags = 0x01u;
constexpr uint8_t kAdTypeCompleteLocalName = 0x09u;
constexpr uint8_t kAdFlagGeneralDiscoverable = 0x02u;
constexpr uint8_t kAdFlagBrEdrNotSupported = 0x04u;
constexpr char kDeviceName[] = "JH Stream HW";
constexpr uint32_t kSummaryPeriodMs = 1000u;

const uint8_t kTestSecret[HAL_BLE_STREAM_SECRET_MIN_LEN] = {
    0x8fu, 0x2cu, 0x51u, 0xe4u, 0xb7u, 0x0du, 0x93u, 0xa6u, 0x14u, 0x7bu, 0xc8u,
    0x35u, 0x6eu, 0xf1u, 0x2au, 0x59u, 0xd3u, 0x60u, 0x8bu, 0x47u, 0xe2u, 0x1cu,
    0x75u, 0xb0u, 0x39u, 0xa8u, 0x4fu, 0xd6u, 0x62u, 0x1eu, 0xc4u, 0x97u};

hal_status_t s_status = HAL_NONE;
hal_ble_advertising_handle_t s_advertising = HAL_BLE_INVALID_HANDLE;
uint32_t s_last_summary_ms;
uint32_t s_received;
uint32_t s_echoed;
char s_address[HAL_BLE_ADDRESS_TEXT_SIZE] = "unknown";

hal_ble_advertising_config_t advertising_config(void) {
  hal_ble_advertising_config_t config{};
  config.interval_min = kAdvertisingInterval100Ms;
  config.interval_max = kAdvertisingInterval100Ms;
  size_t offset = 0u;
  config.data[offset++] = 2u;
  config.data[offset++] = kAdTypeFlags;
  config.data[offset++] = kAdFlagGeneralDiscoverable | kAdFlagBrEdrNotSupported;
  const size_t name_length = strlen(kDeviceName);
  config.data[offset++] = (uint8_t)(name_length + 1u);
  config.data[offset++] = kAdTypeCompleteLocalName;
  memcpy(&config.data[offset], kDeviceName, name_length);
  offset += name_length;
  config.data_length = (uint8_t)offset;
  return config;
}

void start_advertising(void) {
  const hal_ble_advertising_config_t config = advertising_config();
  const hal_status_t status =
      hal_ble_advertising_start(&config, &s_advertising);
  if (status != HAL_OK && status != HAL_EBUSY) {
    s_status = status;
    derr("JHBL5 advertising=%s", hal_status_to_string(status));
  }
}

void on_ble_event(const hal_ble_event_t *event, void *) {
  switch (event->type) {
  case HAL_BLE_EVENT_CONTROLLER_READY: {
    hal_ble_address_t address{};
    if (hal_ble_get_local_address(&address) == HAL_OK &&
        hal_ble_format_address(&address, s_address, sizeof(s_address)) ==
            HAL_OK) {
      deb("JHBL5 ready address=%s", s_address);
    }
    start_advertising();
    break;
  }
  case HAL_BLE_EVENT_CONNECTED:
    deb("JHBL5 connected handle=%lu mtu=%u", (unsigned long)event->connection,
        (unsigned)event->mtu);
    break;
  case HAL_BLE_EVENT_DISCONNECTED:
    deb("JHBL5 disconnected reason=0x%02x", (unsigned)event->disconnect_reason);
    start_advertising();
    break;
  case HAL_BLE_EVENT_MTU_UPDATED:
    deb("JHBL5 mtu=%u", (unsigned)event->mtu);
    break;
  case HAL_BLE_EVENT_ERROR:
    s_status = event->status;
    derr("JHBL5 BLE error=%s", hal_status_to_string(event->status));
    break;
  case HAL_BLE_EVENT_ADVERTISING_STARTED:
    deb("JHBL5 advertising");
    break;
  case HAL_BLE_EVENT_ADVERTISING_STOPPED:
  case HAL_BLE_EVENT_SCAN_STARTED:
  case HAL_BLE_EVENT_SCAN_STOPPED:
  case HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE:
    break;
  }
}

void echo_received(void) {
  uint8_t payload[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t length = 0u;
  for (;;) {
    const hal_status_t status =
        hal_ble_stream_receive(payload, sizeof(payload), &length);
    if (status == HAL_EOVERFLOW) {
      derr("JHBL5 RX overflow");
      continue;
    }
    if (status != HAL_OK) {
      return;
    }
    ++s_received;
    const hal_status_t sent = hal_ble_stream_send(payload, length);
    if (sent == HAL_OK) {
      ++s_echoed;
    } else {
      derr("JHBL5 echo=%s", hal_status_to_string(sent));
    }
  }
}

void report_summary(void) {
  hal_ble_info_t ble{};
  hal_ble_stream_info_t stream{};
  const hal_status_t ble_status = hal_ble_get_info(&ble);
  const hal_status_t stream_status = hal_ble_stream_get_info(&stream);
  deb("JHBL5 address=%s status=%s ble=%s stream=%s state=%u mtu=%u sub=%u "
      "secret=%u "
      "rx=%lu tx=%lu auth_fail=%lu replay=%lu drop_rx=%lu drop_tx=%lu",
      s_address, hal_status_to_string(s_status),
      hal_status_to_string(ble_status), hal_status_to_string(stream_status),
      (unsigned)stream.state, (unsigned)ble.mtu, stream.subscribed ? 1u : 0u,
      stream.secret_provisioned ? 1u : 0u, (unsigned long)s_received,
      (unsigned long)s_echoed, (unsigned long)stream.auth_failures,
      (unsigned long)stream.replay_rejections,
      (unsigned long)stream.dropped_rx_frames,
      (unsigned long)stream.dropped_tx_frames);
}

} // namespace

extern "C" void app_start(void) {
  debugInit();
  s_status = hal_ble_initialize();
  if (s_status != HAL_OK) {
    report_summary();
    return;
  }
  s_status = hal_ble_set_event_callback(on_ble_event, nullptr);
  if (s_status != HAL_OK) {
    report_summary();
    return;
  }
  hal_ble_stream_config_t config{};
  config.capabilities =
      HAL_BLE_STREAM_CAP_TELEMETRY | HAL_BLE_STREAM_CAP_DIAGNOSTICS;
  s_status = hal_ble_stream_initialize(&config);
  if (s_status == HAL_OK) {
    s_status = hal_ble_stream_set_secret(kTestSecret, sizeof(kTestSecret));
  }
  report_summary();
}

extern "C" void app_task0(void) {
  if (s_status == HAL_OK) {
    const hal_status_t status = hal_ble_poll();
    if (status != HAL_OK && status != HAL_EOVERFLOW) {
      s_status = status;
    }
  }
  echo_received();
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_summary_ms) >= kSummaryPeriodMs) {
    s_last_summary_ms = now;
    report_summary();
  }
  hal_delay_ms(1u);
}
