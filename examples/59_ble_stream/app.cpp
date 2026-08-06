/*
 * JH BLE Stream v1 consumer.
 *
 * Advertises a connectable Peripheral, publishes the stream service and waits
 * for a client that proves knowledge of the per-device secret. Only an
 * authenticated session may exchange payloads; unauthenticated clients read
 * the protocol version and capabilities and nothing else.
 *
 * The secret below stands in for provisioning. A product derives it per device
 * and delivers it out of band, for example through a label QR code or an
 * authenticated USB channel.
 */

#include <JaszczurHAL.h>
#include <tools.h>

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t kAdvertisingInterval100Ms = 0x00A0u;
constexpr uint8_t kAdStructureTypeFieldSize = 1u;
constexpr uint8_t kAdTypeFlags = 0x01u;
constexpr uint8_t kAdTypeCompleteLocalName = 0x09u;
constexpr uint8_t kAdFlagGeneralDiscoverable = 0x02u;
constexpr uint8_t kAdFlagBrEdrNotSupported = 0x04u;
constexpr char kDeviceName[] = "JH Stream";
constexpr uint32_t kTelemetryPeriodMs = 1000u;

/* Provisioning placeholder: replace with a per-device secret. */
const uint8_t kDeviceSecret[HAL_BLE_STREAM_SECRET_MIN_LEN] = {
    0x8Fu, 0x2Cu, 0x51u, 0xE4u, 0xB7u, 0x0Du, 0x93u, 0xA6u, 0x14u, 0x7Bu, 0xC8u,
    0x35u, 0x6Eu, 0xF1u, 0x2Au, 0x59u, 0xD3u, 0x60u, 0x8Bu, 0x47u, 0xE2u, 0x1Cu,
    0x75u, 0xB0u, 0x39u, 0xA8u, 0x4Fu, 0xD6u, 0x62u, 0x1Eu, 0xC4u, 0x97u};

hal_ble_advertising_handle_t s_advertising;
uint32_t s_next_telemetry_ms;
uint32_t s_sequence;

hal_ble_advertising_config_t advertising_config(void) {
  hal_ble_advertising_config_t config{};
  config.interval_min = kAdvertisingInterval100Ms;
  config.interval_max = kAdvertisingInterval100Ms;

  uint8_t offset = 0u;
  config.data[offset++] = kAdStructureTypeFieldSize + sizeof(uint8_t);
  config.data[offset++] = kAdTypeFlags;
  config.data[offset++] = kAdFlagGeneralDiscoverable | kAdFlagBrEdrNotSupported;

  const uint8_t name_length = (uint8_t)strlen(kDeviceName);
  config.data[offset++] = kAdStructureTypeFieldSize + name_length;
  config.data[offset++] = kAdTypeCompleteLocalName;
  memcpy(&config.data[offset], kDeviceName, name_length);
  offset += name_length;
  config.data_length = offset;
  return config;
}

void on_ble_event(const hal_ble_event_t *event, void *) {
  switch (event->type) {
  case HAL_BLE_EVENT_CONTROLLER_READY: {
    hal_ble_address_t address{};
    char text[HAL_BLE_ADDRESS_TEXT_SIZE];
    if (hal_ble_get_local_address(&address) == HAL_OK &&
        hal_ble_format_address(&address, text, sizeof(text)) == HAL_OK) {
      deb("BLE ready, address %s", text);
    }
    const hal_ble_advertising_config_t config = advertising_config();
    (void)hal_ble_advertising_start(&config, &s_advertising);
    break;
  }
  case HAL_BLE_EVENT_CONNECTED:
    deb("Client connected");
    break;
  case HAL_BLE_EVENT_DISCONNECTED: {
    deb("Client disconnected, advertising again");
    const hal_ble_advertising_config_t config = advertising_config();
    (void)hal_ble_advertising_start(&config, &s_advertising);
    break;
  }
  case HAL_BLE_EVENT_MTU_UPDATED:
    deb("ATT MTU %u", (unsigned)event->mtu);
    if (event->mtu < HAL_BLE_STREAM_MIN_ATT_MTU) {
      deb("MTU below %u; the handshake needs a larger one",
          (unsigned)HAL_BLE_STREAM_MIN_ATT_MTU);
    }
    break;
  case HAL_BLE_EVENT_ERROR:
    derr("BLE error %s", hal_status_to_string(event->status));
    break;
  default:
    break;
  }
}

void drain_received_payloads(void) {
  uint8_t payload[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t length = 0u;
  for (;;) {
    const hal_status_t status =
        hal_ble_stream_receive(payload, sizeof(payload), &length);
    if (status == HAL_EOVERFLOW) {
      deb("Stream RX overflow; frames were dropped");
      continue;
    }
    if (status != HAL_OK) {
      break;
    }
    deb("Received %u authenticated bytes, first=0x%02X", (unsigned)length,
        (unsigned)payload[0]);
  }
}

void publish_telemetry(void) {
  hal_ble_stream_info_t info{};
  if (hal_ble_stream_get_info(&info) != HAL_OK ||
      info.state != HAL_BLE_STREAM_STATE_AUTHENTICATED) {
    return;
  }
  if ((int32_t)(hal_millis() - s_next_telemetry_ms) < 0) {
    return;
  }
  s_next_telemetry_ms = hal_millis() + kTelemetryPeriodMs;

  char message[48];
  const int written =
      snprintf(message, sizeof(message), "seq=%lu uptime=%lu",
               (unsigned long)++s_sequence, (unsigned long)hal_millis());
  if (written <= 0) {
    return;
  }
  const hal_status_t status = hal_ble_stream_send(message, (size_t)written);
  if (status == HAL_EAGAIN) {
    deb("Stream TX backpressure, retrying later");
  } else if (status != HAL_OK) {
    derr("Stream send failed: %s", hal_status_to_string(status));
  }
}

} // namespace

extern "C" void app_start(void) {
  debugInit();
  deb("JH BLE Stream v1 example");

  if (hal_ble_initialize() != HAL_OK) {
    derr("BLE initialize failed");
    return;
  }
  (void)hal_ble_set_event_callback(on_ble_event, nullptr);

  hal_ble_stream_config_t config{};
  config.capabilities =
      HAL_BLE_STREAM_CAP_TELEMETRY | HAL_BLE_STREAM_CAP_DIAGNOSTICS;
  if (hal_ble_stream_initialize(&config) != HAL_OK) {
    derr("Stream initialize failed");
    return;
  }
  if (hal_ble_stream_set_secret(kDeviceSecret, sizeof(kDeviceSecret)) !=
      HAL_OK) {
    derr("Stream secret rejected");
  }
}

extern "C" void app_task0(void) {
  (void)hal_ble_poll();
  drain_received_payloads();
  publish_telemetry();
}
