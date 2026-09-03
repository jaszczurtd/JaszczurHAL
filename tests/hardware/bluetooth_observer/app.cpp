#include <JaszczurHAL.h>
#include <tools.h>

#include <string.h>

namespace {

constexpr uint16_t kScanInterval60Ms = 0x0060u;
constexpr uint16_t kScanWindow60Ms = 0x0060u;
constexpr uint8_t kAdTypeManufacturerSpecificData = 0xffu;
constexpr uint8_t kAdTypeServiceData16Bit = 0x16u;
constexpr uint16_t kTeltonikaCompanyId = 0x089au;
constexpr uint16_t kAppleCompanyId = 0x004cu;
constexpr uint16_t kEddystoneServiceUuid = 0xfeaau;
constexpr uint8_t kIBeaconType = 0x02u;
constexpr uint8_t kIBeaconLength = 0x15u;
constexpr uint32_t kSummaryPeriodMs = 1000u;

static hal_status_t s_status = HAL_NONE;
static uint32_t s_last_summary_ms;
static uint32_t s_reports;
static uint32_t s_teltonika_reports;
static uint32_t s_ibeacon_reports;
static uint32_t s_eddystone_reports;

uint16_t little_endian_u16(const uint8_t *data) {
  return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8u);
}

void classify_report(const hal_ble_advertising_report_t &report,
                     bool *teltonika, bool *ibeacon, bool *eddystone) {
  size_t offset = 0u;
  hal_ble_advertising_field_t field{};
  while (hal_ble_advertising_field_next(&report, &offset, &field) == HAL_OK) {
    if (field.type == kAdTypeManufacturerSpecificData &&
        field.data_length >= 2u) {
      const uint16_t company_id = little_endian_u16(field.data);
      *teltonika = *teltonika || company_id == kTeltonikaCompanyId;
      *ibeacon =
          *ibeacon ||
          (company_id == kAppleCompanyId && field.data_length >= 4u &&
           field.data[2] == kIBeaconType && field.data[3] == kIBeaconLength);
    }
    if (field.type == kAdTypeServiceData16Bit && field.data_length >= 2u) {
      *eddystone =
          *eddystone || little_endian_u16(field.data) == kEddystoneServiceUuid;
    }
  }
}

void report_advertisement(const hal_ble_advertising_report_t &report) {
  char address[HAL_BLE_ADDRESS_TEXT_SIZE] = {};
  char payload_hex[HAL_BLE_LEGACY_ADV_MAX_DATA_LEN * 2u + 1u] = {};
  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  for (size_t index = 0u; index < report.data_length; ++index) {
    payload_hex[index * 2u] = kHexDigits[report.data[index] >> 4u];
    payload_hex[index * 2u + 1u] = kHexDigits[report.data[index] & 0x0fu];
  }
  (void)hal_ble_format_address(&report.address, address, sizeof(address));

  bool teltonika = false;
  bool ibeacon = false;
  bool eddystone = false;
  classify_report(report, &teltonika, &ibeacon, &eddystone);
  ++s_reports;
  s_teltonika_reports += teltonika ? 1u : 0u;
  s_ibeacon_reports += ibeacon ? 1u : 0u;
  s_eddystone_reports += eddystone ? 1u : 0u;
  deb("JHBL4A report=%lu addr=%s rssi=%d type=%u len=%u teltonika=%u "
      "ibeacon=%u eddystone=%u data=%s",
      (unsigned long)s_reports, address, (int)report.rssi,
      (unsigned)report.event_type, (unsigned)report.data_length,
      teltonika ? 1u : 0u, ibeacon ? 1u : 0u, eddystone ? 1u : 0u, payload_hex);
}

void drain_reports(void) {
  hal_ble_advertising_report_t report{};
  for (;;) {
    const hal_status_t status = hal_ble_scan_report_next(&report);
    if (status == HAL_OK) {
      report_advertisement(report);
      continue;
    }
    if (status == HAL_EOVERFLOW) {
      derr("JHBL4A scan report queue overflow");
      continue;
    }
    break;
  }
}

void on_ble_event(const hal_ble_event_t *event, void *) {
  switch (event->type) {
  case HAL_BLE_EVENT_CONTROLLER_READY:
    deb("JHBL4A controller ready");
    break;
  case HAL_BLE_EVENT_SCAN_STARTED:
    deb("JHBL4A passive scan started");
    break;
  case HAL_BLE_EVENT_SCAN_STOPPED:
    deb("JHBL4A passive scan stopped");
    break;
  case HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE:
    drain_reports();
    break;
  case HAL_BLE_EVENT_ERROR:
    derr("JHBL4A BLE error: %s", hal_status_to_string(event->status));
    break;
  case HAL_BLE_EVENT_ADVERTISING_STARTED:
  case HAL_BLE_EVENT_ADVERTISING_STOPPED:
  case HAL_BLE_EVENT_CONNECTED:
  case HAL_BLE_EVENT_DISCONNECTED:
  case HAL_BLE_EVENT_MTU_UPDATED:
    break;
  }
}

void report_summary(void) {
  hal_ble_info_t info{};
  const hal_status_t info_status = hal_ble_get_info(&info);
  deb("JHBL4A status=%s info=%s state=%u scan=%u reports=%lu teltonika=%lu "
      "ibeacon=%lu eddystone=%lu pending=%lu dropped=%lu",
      hal_status_to_string(s_status), hal_status_to_string(info_status),
      (unsigned)info.state, info.scan_requested ? 1u : 0u,
      (unsigned long)s_reports, (unsigned long)s_teltonika_reports,
      (unsigned long)s_ibeacon_reports, (unsigned long)s_eddystone_reports,
      (unsigned long)info.pending_scan_reports,
      (unsigned long)info.dropped_scan_reports);
}

} // namespace

extern "C" void app_start(void) {
  hal_debug_init_default();
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
  hal_ble_scan_config_t config{};
  config.interval = kScanInterval60Ms;
  config.window = kScanWindow60Ms;
  config.filter_duplicates = true;
  s_status = hal_ble_scan_start(&config);
  report_summary();
}

extern "C" void app_task0(void) {
  if (s_status == HAL_OK) {
    const hal_status_t status = hal_ble_poll();
    if (status != HAL_OK && status != HAL_EOVERFLOW) {
      s_status = status;
    }
  }
  drain_reports();
  const uint32_t now = hal_millis();
  if (now - s_last_summary_ms >= kSummaryPeriodMs) {
    s_last_summary_ms = now;
    report_summary();
  }
  hal_delay_ms(1u);
}
