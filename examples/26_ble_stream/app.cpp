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
#include <hal/serial/hal_serial.h>

#include <stdio.h>
#include <string.h>

#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
#include <hal/bluetooth/hal_ble_commands.h>
#include <hal/commands/hal_command_router.h>
#include <hal/security/hal_crc.h>
#endif

namespace {

constexpr uint16_t kAdvertisingInterval100Ms = 0x00A0u;
constexpr uint8_t kAdStructureTypeFieldSize = 1u;
constexpr uint8_t kAdTypeFlags = 0x01u;
constexpr uint8_t kAdTypeCompleteLocalName = 0x09u;
constexpr uint8_t kAdFlagGeneralDiscoverable = 0x02u;
constexpr uint8_t kAdFlagBrEdrNotSupported = 0x04u;
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
constexpr char kDeviceName[] = "JH Commands";
#else
constexpr char kDeviceName[] = "JH Stream";
constexpr uint32_t kTelemetryPeriodMs = 1000u;
#endif

/* Provisioning placeholder: replace with a per-device secret. */
const uint8_t kDeviceSecret[HAL_BLE_STREAM_SECRET_MIN_LEN] = {
    0x8Fu, 0x2Cu, 0x51u, 0xE4u, 0xB7u, 0x0Du, 0x93u, 0xA6u, 0x14u, 0x7Bu, 0xC8u,
    0x35u, 0x6Eu, 0xF1u, 0x2Au, 0x59u, 0xD3u, 0x60u, 0x8Bu, 0x47u, 0xE2u, 0x1Cu,
    0x75u, 0xB0u, 0x39u, 0xA8u, 0x4Fu, 0xD6u, 0x62u, 0x1Eu, 0xC4u, 0x97u};

hal_ble_advertising_handle_t s_advertising;
hal_status_t s_status = HAL_NONE;
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
constexpr char kEchoCommand[] = "echo";
constexpr char kMetadataCommand[] = "metadata";
constexpr char kForbiddenCommand[] = "ble-forbidden";
constexpr char kReadyEvent[] = "peripheral.ready";
constexpr char kResultEvent[] = "peripheral.result";
constexpr char kHostEchoCommand[] = "host.echo";
constexpr char kHostEchoPayload[] = "peripheral-to-central";
#if defined(HAL_ENABLE_FREERTOS)
constexpr char kRuntimeName[] = "freertos";
#else
constexpr char kRuntimeName[] = "baremetal";
#endif

hal_command_router_t s_router = nullptr;
hal_ble_commands_t s_commands = nullptr;
uint64_t s_active_session;
uint32_t s_outbound_request_id;
uint32_t s_echo_calls;
uint32_t s_metadata_calls;
bool s_ready_event_started;
bool s_outbound_request_started;
bool s_result_event_pending;
#else
uint32_t s_next_telemetry_ms;
uint32_t s_sequence;
char s_pending_telemetry[48];
size_t s_pending_telemetry_length;
bool s_telemetry_pending;
#endif
bool s_started;

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
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
      deb("JHBC1 READY target=%s board=%s runtime=%s address=%s",
          HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME, kRuntimeName, text);
#else
      deb("BLE ready, address %s", text);
#endif
    }
    const hal_ble_advertising_config_t config = advertising_config();
    (void)hal_ble_advertising_start(&config, &s_advertising);
    break;
  }
  case HAL_BLE_EVENT_CONNECTED:
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
    deb("JHBC1 CONNECTED handle=%lu", (unsigned long)event->connection);
#else
    deb("Client connected");
#endif
    break;
  case HAL_BLE_EVENT_DISCONNECTED: {
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
    deb("JHBC1 DISCONNECTED reason=0x%02X", (unsigned)event->disconnect_reason);
#else
    deb("Client disconnected; advertising resumes automatically");
#endif
    break;
  }
  case HAL_BLE_EVENT_MTU_UPDATED:
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
    deb("JHBC1 MTU value=%u", (unsigned)event->mtu);
#else
    deb("ATT MTU %u", (unsigned)event->mtu);
#endif
    if (event->mtu < HAL_BLE_STREAM_MIN_ATT_MTU) {
      deb("MTU below %u; the handshake needs a larger one",
          (unsigned)HAL_BLE_STREAM_MIN_ATT_MTU);
    }
    break;
  case HAL_BLE_EVENT_ERROR:
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
    derr("JHBC1 FAULT stage=ble status=%s",
         hal_status_to_string(event->status));
#else
    derr("BLE error %s", hal_status_to_string(event->status));
#endif
    break;
  default:
    break;
  }
}

#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)

const hal_ble_commands_peer_info_t *
command_peer(const hal_command_request_t *request) {
  if (request == nullptr || request->source != HAL_COMMAND_SOURCE_BLE_STREAM ||
      request->source_context == nullptr || request->session_id == 0u ||
      request->security_flags != HAL_COMMAND_SECURITY_ALL) {
    return nullptr;
  }
  const auto *peer = static_cast<const hal_ble_commands_peer_info_t *>(
      request->source_context);
  return peer->session_id == request->session_id &&
                 peer->security_flags == request->security_flags
             ? peer
             : nullptr;
}

hal_status_t echo_command(const hal_command_request_t *request,
                          hal_command_response_t *response, void *) {
  if (request == nullptr || response == nullptr) {
    return HAL_EINVAL;
  }
  const hal_ble_commands_peer_info_t *peer = command_peer(request);
  if (peer == nullptr) {
    derr("JHBC1 FAULT stage=echo-metadata");
    return HAL_EINTERNAL;
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  const uint32_t crc = hal_crc32(request->arguments, request->arguments_length);
  deb("JHBC1 HANDLE id=%lu len=%u crc=%08lX call=%lu source=%s "
      "security=0x%08lX peer=0x%016llX session=0x%016llX mtu=%u "
      "counters=%llu-%llu status=%s",
      (unsigned long)request->request_id, (unsigned)request->arguments_length,
      (unsigned long)crc, (unsigned long)++s_echo_calls,
      hal_command_source_to_string(request->source),
      (unsigned long)request->security_flags,
      (unsigned long long)request->peer_id,
      (unsigned long long)request->session_id, (unsigned)peer->mtu,
      (unsigned long long)peer->first_rx_counter,
      (unsigned long long)peer->last_rx_counter, hal_status_to_string(status));
  return status;
}

hal_status_t metadata_command(const hal_command_request_t *request,
                              hal_command_response_t *response, void *) {
  if (request == nullptr || response == nullptr) {
    return HAL_EINVAL;
  }
  const hal_ble_commands_peer_info_t *peer = command_peer(request);
  if (peer == nullptr) {
    derr("JHBC1 FAULT stage=metadata-provenance");
    return HAL_EINTERNAL;
  }

  char metadata[224];
  const int written = snprintf(
      metadata, sizeof(metadata),
      "JBCM1|%s|%08lX|%016llX|%016llX|%lu|%u|%lu|%lu|%llu|%llu|%lu",
      hal_command_source_to_string(request->source),
      (unsigned long)request->security_flags,
      (unsigned long long)request->peer_id,
      (unsigned long long)request->session_id, (unsigned long)peer->connection,
      (unsigned)peer->mtu, (unsigned long)peer->ble_generation,
      (unsigned long)peer->stream_generation,
      (unsigned long long)peer->first_rx_counter,
      (unsigned long long)peer->last_rx_counter,
      (unsigned long)++s_metadata_calls);
  if (written <= 0 || (size_t)written >= sizeof(metadata)) {
    return HAL_EOVERFLOW;
  }
  hal_status_t status =
      hal_command_response_set_encoding(response, HAL_COMMAND_ENCODING_TEXT);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, metadata, (size_t)written);
  }
  deb("JHBC1 METADATA id=%lu session=0x%016llX counters=%llu-%llu "
      "status=%s",
      (unsigned long)request->request_id,
      (unsigned long long)request->session_id,
      (unsigned long long)peer->first_rx_counter,
      (unsigned long long)peer->last_rx_counter, hal_status_to_string(status));
  return status;
}

hal_status_t forbidden_command(const hal_command_request_t *,
                               hal_command_response_t *, void *) {
  derr("JHBC1 FAULT stage=forbidden-handler");
  return HAL_EINTERNAL;
}

hal_status_t register_command(const char *name,
                              hal_command_source_mask_t sources,
                              hal_command_security_flags_t security,
                              hal_command_handler_t handler) {
  hal_command_definition_t definition{};
  definition.name = name;
  definition.allowed_sources = sources;
  definition.required_security = security;
  definition.handler = handler;
  return hal_command_router_register(s_router, &definition);
}

hal_status_t register_commands(void) {
  hal_status_t status = hal_command_router_default(&s_router);
  if (status == HAL_OK) {
    status = register_command(
        kEchoCommand, HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM),
        HAL_COMMAND_SECURITY_ALL, echo_command);
  }
  if (status == HAL_OK) {
    status =
        register_command(kMetadataCommand,
                         HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM),
                         HAL_COMMAND_SECURITY_ALL, metadata_command);
  }
  if (status == HAL_OK) {
    status = register_command(
        kForbiddenCommand, HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_DIRECT),
        0u, forbidden_command);
  }
  return status;
}

void reset_command_session(uint64_t session_id) {
  s_active_session = session_id;
  s_outbound_request_id = 0u;
  s_ready_event_started = false;
  s_outbound_request_started = false;
  s_result_event_pending = false;
  if (session_id != 0u) {
    deb("JHBC1 SESSION id=0x%016llX", (unsigned long long)session_id);
  }
}

void consume_adapter_message(void) {
  hal_command_message_t message{};
  hal_ble_commands_peer_info_t peer{};
  const hal_status_t status =
      hal_ble_commands_receive(s_commands, &message, &peer);
  if (status == HAL_EAGAIN) {
    return;
  }
  if (status != HAL_OK) {
    derr("JHBC1 FAULT stage=receive status=%s", hal_status_to_string(status));
    return;
  }
  const bool valid = message.type == HAL_COMMAND_MESSAGE_RESPONSE &&
                     message.request_id == s_outbound_request_id &&
                     message.status == HAL_OK &&
                     message.encoding == HAL_COMMAND_ENCODING_BINARY &&
                     message.payload_length == sizeof(kHostEchoPayload) - 1u &&
                     memcmp(message.payload, kHostEchoPayload,
                            sizeof(kHostEchoPayload) - 1u) == 0 &&
                     peer.session_id == s_active_session;
  deb("JHBC1 OUTBOUND id=%lu status=%s match=%u session=0x%016llX",
      (unsigned long)message.request_id, hal_status_to_string(message.status),
      valid ? 1u : 0u, (unsigned long long)peer.session_id);
  if (!valid) {
    derr("JHBC1 FAULT stage=outbound-response");
    return;
  }
  s_result_event_pending = true;
}

void advance_session_output(const hal_ble_commands_info_t &info) {
  if (info.session_id != s_active_session) {
    reset_command_session(info.session_id);
  }
  if (info.session_id == 0u) {
    return;
  }

  if (s_result_event_pending) {
    const char result[] = "PASS";
    const hal_status_t status = hal_ble_commands_event_start(
        s_commands, kResultEvent, HAL_COMMAND_ENCODING_TEXT, result,
        sizeof(result) - 1u);
    if (status == HAL_OK) {
      s_result_event_pending = false;
      deb("JHBC1 RESULT event=queued session=0x%016llX",
          (unsigned long long)info.session_id);
    } else if (status != HAL_EBUSY && status != HAL_EAGAIN) {
      derr("JHBC1 FAULT stage=result-event status=%s",
           hal_status_to_string(status));
    }
    return;
  }

  if (!s_ready_event_started) {
    char payload[96];
    const int written =
        snprintf(payload, sizeof(payload), "JBC1|%s|%s|%s|%016llX",
                 HAL_TARGET_NAME, HAL_BOARD_PROFILE_NAME, kRuntimeName,
                 (unsigned long long)info.session_id);
    if (written <= 0 || (size_t)written >= sizeof(payload)) {
      derr("JHBC1 FAULT stage=ready-format");
      return;
    }
    const hal_status_t status = hal_ble_commands_event_start(
        s_commands, kReadyEvent, HAL_COMMAND_ENCODING_TEXT, payload,
        (size_t)written);
    if (status == HAL_OK) {
      s_ready_event_started = true;
    } else if (status != HAL_EBUSY && status != HAL_EAGAIN) {
      derr("JHBC1 FAULT stage=ready-event status=%s",
           hal_status_to_string(status));
    }
    return;
  }

  if (!s_outbound_request_started && info.transmit_length == 0u &&
      !info.pending_response) {
    uint32_t request_id = 0u;
    const hal_status_t status = hal_ble_commands_request_start(
        s_commands, kHostEchoCommand, HAL_COMMAND_ENCODING_BINARY,
        kHostEchoPayload, sizeof(kHostEchoPayload) - 1u, &request_id);
    if (status == HAL_OK) {
      s_outbound_request_started = true;
      s_outbound_request_id = request_id;
      deb("JHBC1 OUTBOUND request=%lu len=%u", (unsigned long)request_id,
          (unsigned)(sizeof(kHostEchoPayload) - 1u));
    } else if (status != HAL_EBUSY && status != HAL_EAGAIN) {
      derr("JHBC1 FAULT stage=outbound-request status=%s",
           hal_status_to_string(status));
    }
  }
}

void process_commands(void) {
  const hal_status_t status = hal_ble_commands_process(s_commands);
  if (status != HAL_OK && status != HAL_EAGAIN) {
    derr("JHBC1 FAULT stage=process status=%s", hal_status_to_string(status));
  }
  consume_adapter_message();
  hal_ble_commands_info_t info{};
  const hal_status_t info_status = hal_ble_commands_get_info(s_commands, &info);
  if (info_status == HAL_OK) {
    advance_session_output(info);
  } else {
    derr("JHBC1 FAULT stage=info status=%s", hal_status_to_string(info_status));
  }
}

#else

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

void clear_pending_telemetry(void) {
  s_pending_telemetry_length = 0u;
  s_telemetry_pending = false;
}

hal_status_t flush_pending_telemetry(void) {
  const hal_status_t status =
      hal_ble_stream_send(s_pending_telemetry, s_pending_telemetry_length);
  if (status != HAL_EAGAIN) {
    clear_pending_telemetry();
  }
  return status;
}

void publish_telemetry(void) {
  hal_ble_stream_info_t info{};
  if (hal_ble_stream_get_info(&info) != HAL_OK ||
      info.state != HAL_BLE_STREAM_STATE_AUTHENTICATED) {
    clear_pending_telemetry();
    return;
  }

  if (s_telemetry_pending) {
    const hal_status_t status = flush_pending_telemetry();
    if (status == HAL_EAGAIN) {
      return;
    }
    if (status != HAL_OK) {
      derr("Stream telemetry retry failed: %s", hal_status_to_string(status));
      return;
    }
    s_next_telemetry_ms = hal_millis() + kTelemetryPeriodMs;
    return;
  }

  if ((int32_t)(hal_millis() - s_next_telemetry_ms) < 0) {
    return;
  }

  const int written = snprintf(
      s_pending_telemetry, sizeof(s_pending_telemetry), "seq=%lu uptime=%lu",
      (unsigned long)++s_sequence, (unsigned long)hal_millis());
  if (written <= 0 || (size_t)written >= sizeof(s_pending_telemetry)) {
    return;
  }
  s_pending_telemetry_length = (size_t)written;
  s_telemetry_pending = true;
  const hal_status_t status = flush_pending_telemetry();
  if (status == HAL_EAGAIN) {
    deb("Stream TX backpressure; one telemetry sample queued for retry");
  } else if (status != HAL_OK) {
    derr("Stream send failed: %s", hal_status_to_string(status));
  } else {
    s_next_telemetry_ms = hal_millis() + kTelemetryPeriodMs;
  }
}

#endif

hal_status_t initialize_runtime(void) {
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
    status = hal_ble_stream_set_secret(kDeviceSecret, sizeof(kDeviceSecret));
  }
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
  if (status == HAL_OK) {
    status = register_commands();
  }
  if (status == HAL_OK) {
    const hal_ble_commands_config_t commands_config =
        hal_ble_commands_config_defaults();
    status = hal_ble_commands_create(&commands_config, &s_commands);
  }
#endif
  if (status != HAL_OK) {
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
    if (s_commands != nullptr) {
      (void)hal_ble_commands_destroy(s_commands);
      s_commands = nullptr;
    }
#endif
    (void)hal_ble_stream_deinitialize();
    (void)hal_ble_deinitialize();
  }
  return status;
}

} // namespace

extern "C" void app_start(void) {
  hal_debug_init_default();
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
  deb("JHBC1 BLE command-router Peripheral");
#else
  deb("JH BLE Stream v1 example");
#endif
}

extern "C" void app_task0(void) {
  if (!s_started) {
    s_started = true;
    s_status = initialize_runtime();
    if (s_status != HAL_OK) {
      derr("BLE Stream initialize failed: %s", hal_status_to_string(s_status));
    }
  }
  if (s_status != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }
  const hal_status_t status = hal_ble_poll();
  if (status != HAL_OK && status != HAL_EOVERFLOW) {
    s_status = status;
    derr("BLE poll failed: %s", hal_status_to_string(status));
    return;
  }
#if defined(HAL_BLE_STREAM_EXAMPLE_COMMANDS)
  process_commands();
#else
  drain_received_payloads();
  publish_telemetry();
#endif
  hal_delay_ms(1u);
}
