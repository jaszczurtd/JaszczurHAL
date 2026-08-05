#include <JaszczurHAL.h>
#include <tools.h>

#include <string.h>

namespace {

constexpr uint8_t kAdStructureTypeFieldSize = 1u;
constexpr uint8_t kAdTypeFlags = 0x01u; // Bluetooth LE Flags AD type.
constexpr uint8_t kAdTypeCompleteLocalName =
    0x09u; // Bluetooth LE Complete Local Name AD type.
constexpr uint8_t kAdFlagGeneralDiscoverable =
    0x02u; // LE General Discoverable Mode flag.
constexpr uint8_t kAdFlagBrEdrNotSupported =
    0x04u; // BR/EDR Not Supported flag.
constexpr uint16_t kAdvertisingInterval100Ms =
    0x00a0u; // 160 Bluetooth advertising units of 0.625 ms.
constexpr char kDeviceName[] = "JH BLE Peripheral";

static hal_status_t s_ble_status = HAL_ESTATE;
static hal_ble_advertising_handle_t s_advertising = HAL_BLE_INVALID_HANDLE;

hal_ble_advertising_config_t advertising_config(void) {
  hal_ble_advertising_config_t config = {};
  size_t offset = 0u;

  config.data[offset++] = kAdStructureTypeFieldSize + sizeof(uint8_t);
  config.data[offset++] = kAdTypeFlags;
  config.data[offset++] = kAdFlagGeneralDiscoverable | kAdFlagBrEdrNotSupported;

  constexpr size_t name_length = sizeof(kDeviceName) - 1u;
  config.data[offset++] = (uint8_t)(kAdStructureTypeFieldSize + name_length);
  config.data[offset++] = kAdTypeCompleteLocalName;
  memcpy(&config.data[offset], kDeviceName, name_length);
  offset += name_length;

  config.data_length = (uint8_t)offset;
  config.interval_min = kAdvertisingInterval100Ms;
  config.interval_max = kAdvertisingInterval100Ms;
  return config;
}

static void on_ble_event(const hal_ble_event_t *event, void *context) {
  (void)context;

  switch (event->type) {
  case HAL_BLE_EVENT_CONTROLLER_READY: {
    hal_ble_address_t local_address = {};
    char address[HAL_BLE_ADDRESS_TEXT_SIZE] = {};
    (void)hal_ble_get_local_address(&local_address);
    (void)hal_ble_format_address(&local_address, address, sizeof(address));
    deb("BLE ready: %s", address);
    break;
  }
  case HAL_BLE_EVENT_ADVERTISING_STARTED:
    deb("BLE advertising");
    break;
  case HAL_BLE_EVENT_CONNECTED:
    deb("BLE connected: handle=%lu", (unsigned long)event->connection);
    break;
  case HAL_BLE_EVENT_DISCONNECTED:
    deb("BLE disconnected: reason=0x%02x", (unsigned)event->disconnect_reason);
    break;
  case HAL_BLE_EVENT_MTU_UPDATED:
    deb("BLE ATT MTU: %u", (unsigned)event->mtu);
    break;
  case HAL_BLE_EVENT_ERROR:
    derr("BLE error: %s", hal_status_to_string(event->status));
    break;
  case HAL_BLE_EVENT_ADVERTISING_STOPPED:
    deb("BLE advertising stopped");
    break;
  case HAL_BLE_EVENT_SCAN_STARTED:
  case HAL_BLE_EVENT_SCAN_STOPPED:
  case HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE:
    break;
  }
}

} // namespace

extern "C" void app_start(void) {
  debugInit();
  s_ble_status = hal_ble_initialize();
  if (s_ble_status != HAL_OK) {
    derr("BLE initialization failed: %s", hal_status_to_string(s_ble_status));
    return;
  }
  s_ble_status = hal_ble_set_event_callback(on_ble_event, NULL);
  if (s_ble_status != HAL_OK) {
    derr("BLE callback setup failed: %s", hal_status_to_string(s_ble_status));
    return;
  }

  const hal_ble_advertising_config_t config = advertising_config();
  s_ble_status = hal_ble_advertising_start(&config, &s_advertising);
  if (s_ble_status != HAL_OK) {
    derr("BLE advertising failed: %s", hal_status_to_string(s_ble_status));
  }
}

extern "C" void app_task0(void) {
  if (s_ble_status == HAL_OK) {
    const hal_status_t status = hal_ble_poll();
    if (status != HAL_OK && status != HAL_EOVERFLOW) {
      derr("BLE poll failed: %s", hal_status_to_string(status));
      s_ble_status = status;
    }
  }
  hal_delay_ms(1u);
}
