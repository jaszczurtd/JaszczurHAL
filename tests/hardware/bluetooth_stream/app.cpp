#include <JaszczurHAL.h>
#include <hal/security/jh_secure_random.h>
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
constexpr uint32_t kRestartDelayMs = 750u;
constexpr uint32_t kPowerLossDelayMs = 750u;
constexpr uint32_t kPowerLossWatchdogMs = 10u;
constexpr uint32_t kSaturationHoldMinMs = 2000u;
constexpr uint32_t kSaturationHoldMaxMs = 30000u;
constexpr char kIdentityCommand[] = "JHBL5/IDENTITY";
constexpr char kRestartCommand[] = "JHBL5/RESTART";
constexpr char kRestartResponse[] = "JHBL5/RESTARTING";
constexpr char kSaturationCommand[] = "JHBL5/SATURATE";
constexpr char kSaturationResponse[] = "JHBL5/SATURATE-READY";
constexpr char kStatsCommand[] = "JHBL5/STATS";
constexpr char kStatsResponse[] = "J5S1";
constexpr char kBootCommand[] = "JHBL5/BOOT";
constexpr char kBootResponse[] = "J5B1";
constexpr size_t kBootIdLength = sizeof(uint64_t);
constexpr char kPowerLossCommand[] = "JHBL5/POWER-LOSS";
constexpr char kPowerLossResponse[] = "JHBL5/POWER-LOSS-ARMED";
#if defined(HAL_ENABLE_FREERTOS)
#define JHBL5_FIXTURE_RUNTIME_NAME "freertos"
#else
#define JHBL5_FIXTURE_RUNTIME_NAME "baremetal"
#endif
constexpr char kIdentityResponse[] =
    "J5I1|" HAL_TARGET_NAME "|" HAL_BOARD_PROFILE_NAME
    "|" JHBL5_FIXTURE_RUNTIME_NAME;
#undef JHBL5_FIXTURE_RUNTIME_NAME
constexpr size_t kStatsFieldCount = 11u;
constexpr size_t kStatsPayloadLength =
    (sizeof(kStatsResponse) - 1u) + (kStatsFieldCount * sizeof(uint32_t));
constexpr size_t kBootPayloadLength =
    (sizeof(kBootResponse) - 1u) + sizeof(uint8_t) + kBootIdLength;
constexpr size_t authenticated_att_length(size_t payload_length) {
  return HAL_BLE_STREAM_FRAME_HEADER_LEN + HAL_BLE_STREAM_AEAD_COUNTER_LEN +
         payload_length + HAL_BLE_STREAM_AEAD_TAG_LEN +
         HAL_BLE_STREAM_ATT_OVERHEAD;
}
static_assert(authenticated_att_length(kStatsPayloadLength) <=
                  HAL_BLE_STREAM_MIN_ATT_MTU,
              "fixture stats must fit the minimum authenticated ATT MTU");
static_assert(authenticated_att_length(sizeof(kIdentityResponse) - 1u) <=
                  HAL_BLE_STREAM_MIN_ATT_MTU,
              "fixture identity must fit the minimum authenticated ATT MTU");
static_assert(authenticated_att_length(kBootPayloadLength) <=
                  HAL_BLE_STREAM_MIN_ATT_MTU,
              "fixture boot status must fit the minimum authenticated ATT MTU");

const uint8_t kTestSecret[HAL_BLE_STREAM_SECRET_MIN_LEN] = {
    0x8fu, 0x2cu, 0x51u, 0xe4u, 0xb7u, 0x0du, 0x93u, 0xa6u, 0x14u, 0x7bu, 0xc8u,
    0x35u, 0x6eu, 0xf1u, 0x2au, 0x59u, 0xd3u, 0x60u, 0x8bu, 0x47u, 0xe2u, 0x1cu,
    0x75u, 0xb0u, 0x39u, 0xa8u, 0x4fu, 0xd6u, 0x62u, 0x1eu, 0xc4u, 0x97u};

hal_status_t s_status = HAL_NONE;
hal_ble_advertising_handle_t s_advertising = HAL_BLE_INVALID_HANDLE;
uint32_t s_last_summary_ms;
uint32_t s_received;
uint32_t s_echoed;
uint32_t s_receive_overflows;
uint32_t s_restarts;
uint32_t s_lifecycle_failures;
uint32_t s_restart_at_ms;
uint32_t s_power_loss_at_ms;
uint32_t s_saturation_until_ms;
bool s_started;
bool s_restart_pending;
bool s_power_loss_pending;
hal_reset_reason_t s_boot_reason = HAL_RESET_REASON_UNKNOWN;
uint64_t s_boot_id;
char s_address[HAL_BLE_ADDRESS_TEXT_SIZE] = "unknown";

bool deadline_reached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

void write_u32_le(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8u);
  out[2] = (uint8_t)(value >> 16u);
  out[3] = (uint8_t)(value >> 24u);
}

void write_u64_le(uint8_t *out, uint64_t value) {
  for (size_t index = 0u; index < sizeof(value); ++index) {
    out[index] = (uint8_t)(value >> (index * 8u));
  }
}

uint32_t read_u32_le(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
         ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

bool payload_equals(const uint8_t *payload, size_t length, const char *value,
                    size_t value_length) {
  return length == value_length && memcmp(payload, value, value_length) == 0;
}

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

hal_status_t send_payload(const void *payload, size_t length) {
  const hal_status_t status = hal_ble_stream_send(payload, length);
  if (status == HAL_OK) {
    ++s_echoed;
  } else {
    derr("JHBL5 send=%s", hal_status_to_string(status));
  }
  return status;
}

void send_stats(void) {
  hal_ble_info_t ble{};
  hal_ble_stream_info_t stream{};
  const hal_status_t ble_status = hal_ble_get_info(&ble);
  const hal_status_t stream_status = hal_ble_stream_get_info(&stream);
  if (ble_status != HAL_OK || stream_status != HAL_OK) {
    ++s_lifecycle_failures;
    derr("JHBL5 stats ble=%s stream=%s", hal_status_to_string(ble_status),
         hal_status_to_string(stream_status));
    return;
  }

  constexpr size_t prefix_length = sizeof(kStatsResponse) - 1u;
  uint8_t response[kStatsPayloadLength];
  memcpy(response, kStatsResponse, prefix_length);
  const uint32_t fields[kStatsFieldCount] = {
      HAL_BLE_STREAM_RX_QUEUE_DEPTH,
      HAL_BLE_STREAM_TX_QUEUE_DEPTH,
      s_received,
      s_echoed,
      s_receive_overflows,
      stream.dropped_rx_frames,
      stream.dropped_tx_frames,
      s_restarts,
      s_lifecycle_failures,
      ble.generation,
      stream.generation,
  };
  for (size_t index = 0u; index < kStatsFieldCount; ++index) {
    write_u32_le(&response[prefix_length + (index * sizeof(uint32_t))],
                 fields[index]);
  }
  (void)send_payload(response, sizeof(response));
}

void send_boot_status(void) {
  uint8_t response[kBootPayloadLength];
  constexpr size_t prefix_length = sizeof(kBootResponse) - 1u;
  memcpy(response, kBootResponse, prefix_length);
  response[prefix_length] = (uint8_t)s_boot_reason;
  write_u64_le(&response[prefix_length + sizeof(uint8_t)], s_boot_id);
  (void)send_payload(response, sizeof(response));
}

void handle_payload(const uint8_t *payload, size_t length) {
  constexpr size_t identity_length = sizeof(kIdentityCommand) - 1u;
  constexpr size_t restart_length = sizeof(kRestartCommand) - 1u;
  constexpr size_t saturation_length = sizeof(kSaturationCommand) - 1u;
  constexpr size_t stats_length = sizeof(kStatsCommand) - 1u;
  constexpr size_t boot_length = sizeof(kBootCommand) - 1u;
  constexpr size_t power_loss_length = sizeof(kPowerLossCommand) - 1u;

  if (payload_equals(payload, length, kIdentityCommand, identity_length)) {
    (void)send_payload(kIdentityResponse, sizeof(kIdentityResponse) - 1u);
    return;
  }
  if (payload_equals(payload, length, kStatsCommand, stats_length)) {
    send_stats();
    return;
  }
  if (payload_equals(payload, length, kBootCommand, boot_length)) {
    send_boot_status();
    return;
  }
  if (payload_equals(payload, length, kRestartCommand, restart_length)) {
    if (send_payload(kRestartResponse, sizeof(kRestartResponse) - 1u) ==
        HAL_OK) {
      s_restart_at_ms = hal_millis() + kRestartDelayMs;
      s_restart_pending = true;
    }
    return;
  }
  if (payload_equals(payload, length, kPowerLossCommand, power_loss_length)) {
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_RP2350_ARM
    if (send_payload(kPowerLossResponse, sizeof(kPowerLossResponse) - 1u) ==
        HAL_OK) {
      s_power_loss_at_ms = hal_millis() + kPowerLossDelayMs;
      s_power_loss_pending = true;
    }
#endif
    return;
  }
  if (length == saturation_length + sizeof(uint32_t) &&
      memcmp(payload, kSaturationCommand, saturation_length) == 0) {
    const uint32_t hold_ms = read_u32_le(&payload[saturation_length]);
    if (hold_ms >= kSaturationHoldMinMs && hold_ms <= kSaturationHoldMaxMs &&
        send_payload(kSaturationResponse, sizeof(kSaturationResponse) - 1u) ==
            HAL_OK) {
      s_saturation_until_ms = hal_millis() + hold_ms;
      deb("JHBL5 saturation hold_ms=%lu", (unsigned long)hold_ms);
      return;
    }
  }
  (void)send_payload(payload, length);
}

void echo_received(void) {
  const uint32_t now = hal_millis();
  if (s_saturation_until_ms != 0u &&
      !deadline_reached(now, s_saturation_until_ms)) {
    return;
  }
  s_saturation_until_ms = 0u;

  uint8_t payload[HAL_BLE_STREAM_MAX_PAYLOAD];
  size_t length = 0u;
  for (;;) {
    const hal_status_t status =
        hal_ble_stream_receive(payload, sizeof(payload), &length);
    if (status == HAL_EOVERFLOW) {
      ++s_receive_overflows;
      derr("JHBL5 RX overflow");
      continue;
    }
    if (status != HAL_OK) {
      return;
    }
    ++s_received;
    handle_payload(payload, length);
    if (s_restart_pending || s_power_loss_pending ||
        s_saturation_until_ms != 0u) {
      return;
    }
  }
}

#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_RP2350_ARM
[[noreturn]] void simulate_power_loss(void) {
  const hal_status_t status = hal_watchdog_enable(kPowerLossWatchdogMs, false);
  if (status != HAL_OK) {
    s_status = status;
    ++s_lifecycle_failures;
    s_power_loss_pending = false;
    derr("JHBL5 power-loss FAIL status=%s", hal_status_to_string(status));
  }
  for (;;) {
    /* Deliberately do not service BLE or feed the watchdog. */
  }
}
#endif

hal_status_t initialize_runtime(void) {
  s_advertising = HAL_BLE_INVALID_HANDLE;
  hal_status_t status = hal_ble_initialize();
  if (status != HAL_OK) {
    return status;
  }
  status = hal_ble_set_event_callback(on_ble_event, nullptr);
  if (status != HAL_OK) {
    (void)hal_ble_deinitialize();
    return status;
  }
  hal_ble_stream_config_t config{};
  config.capabilities =
      HAL_BLE_STREAM_CAP_TELEMETRY | HAL_BLE_STREAM_CAP_DIAGNOSTICS;
  status = hal_ble_stream_initialize(&config);
  if (status == HAL_OK) {
    status = hal_ble_stream_set_secret(kTestSecret, sizeof(kTestSecret));
  }
  if (status != HAL_OK) {
    (void)hal_ble_stream_deinitialize();
    (void)hal_ble_deinitialize();
  }
  return status;
}

void restart_runtime(void) {
  s_saturation_until_ms = 0u;
  hal_status_t status = hal_ble_stream_deinitialize();
  const hal_status_t ble_status = hal_ble_deinitialize();
  if (status == HAL_OK) {
    status = ble_status;
  }
  if (status == HAL_OK) {
    status = initialize_runtime();
  }
  s_restart_pending = false;
  s_status = status;
  if (status == HAL_OK) {
    ++s_restarts;
    deb("JHBL5 restart PASS count=%lu", (unsigned long)s_restarts);
  } else {
    ++s_lifecycle_failures;
    derr("JHBL5 restart FAIL status=%s", hal_status_to_string(status));
  }
}

void report_summary(void) {
  hal_ble_info_t ble{};
  hal_ble_stream_info_t stream{};
  const hal_status_t ble_status = hal_ble_get_info(&ble);
  const hal_status_t stream_status = hal_ble_stream_get_info(&stream);
  deb("JHBL5 address=%s status=%s ble=%s stream=%s boot_reason=%u "
      "ble_state=%u stream_state=%u mtu=%u sub=%u secret=%u "
      "rx=%lu tx=%lu auth_fail=%lu replay=%lu drop_rx=%lu drop_tx=%lu "
      "overflows=%lu restarts=%lu lifecycle_fail=%lu",
      s_address, hal_status_to_string(s_status),
      hal_status_to_string(ble_status), hal_status_to_string(stream_status),
      (unsigned)s_boot_reason, (unsigned)ble.state, (unsigned)stream.state,
      (unsigned)ble.mtu, stream.subscribed ? 1u : 0u,
      stream.secret_provisioned ? 1u : 0u, (unsigned long)s_received,
      (unsigned long)s_echoed, (unsigned long)stream.auth_failures,
      (unsigned long)stream.replay_rejections,
      (unsigned long)stream.dropped_rx_frames,
      (unsigned long)stream.dropped_tx_frames,
      (unsigned long)s_receive_overflows, (unsigned long)s_restarts,
      (unsigned long)s_lifecycle_failures);
}

} // namespace

extern "C" void app_start(void) {
  hal_fault_subsystem_init();
  s_boot_reason = hal_get_reset_reason();
  debugInit();
}

extern "C" void app_task0(void) {
  if (!s_started) {
    s_started = true;
    s_status = jh_secure_random_bytes(&s_boot_id, sizeof(s_boot_id));
    if (s_status == HAL_OK && s_boot_id == 0u) {
      s_status = HAL_EIO;
    }
    if (s_status == HAL_OK) {
      s_status = initialize_runtime();
    }
    report_summary();
  }
  if (s_status == HAL_OK) {
    const uint32_t now = hal_millis();
    if (s_power_loss_pending && deadline_reached(now, s_power_loss_at_ms)) {
#if HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_RP2350_ARM
      simulate_power_loss();
#else
      s_power_loss_pending = false;
#endif
    } else if (s_restart_pending && deadline_reached(now, s_restart_at_ms)) {
      restart_runtime();
    } else {
      const hal_status_t status = hal_ble_poll();
      if (status != HAL_OK && status != HAL_EOVERFLOW) {
        s_status = status;
      }
    }
  }
  if (s_status == HAL_OK && !s_restart_pending && !s_power_loss_pending) {
    echo_received();
  }
  const uint32_t now = hal_millis();
  if ((uint32_t)(now - s_last_summary_ms) >= kSummaryPeriodMs) {
    s_last_summary_ms = now;
    report_summary();
  }
  hal_delay_ms(1u);
}
