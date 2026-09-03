/**
 * @file link_app.cpp
 * @brief Fragmented command-router round trip over a reliable LoRa link.
 */

#ifdef HAL_ENABLE_LORA_COMMANDS

#include <hal/commands/hal_command_router.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/radio/hal_lora_commands.h>
#include <hal/radio/hal_lora_link.h>
#include <hal/radio/hal_lora_radio.h>
#include <hal/security/hal_crc.h>
#include <hal/serial/hal_serial.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>

#include <stdint.h>
#include <string.h>

namespace {

constexpr uint16_t kInitiatorAddress = UINT16_C(0x1001);
constexpr uint16_t kResponderAddress = UINT16_C(0x1002);
constexpr uint8_t kApplicationPort = 1u;
constexpr uint32_t kLfTestFrequencyHz = UINT32_C(434000000);
constexpr uint32_t kFirstRequestDelayMs = UINT32_C(1000);
constexpr uint32_t kRequestPeriodMs = UINT32_C(3000);
constexpr uint32_t kResponseTimeoutMs = UINT32_C(20000);
constexpr uint32_t kReadyPeriodMs = UINT32_C(5000);
constexpr size_t kPayloadLength = 500u;
constexpr char kCommandName[] = "echo";

#ifdef HAL_LORA_LINK_EXAMPLE_RESPONDER
constexpr uint16_t kLocalAddress = kResponderAddress;
constexpr uint16_t kPeerAddress = kInitiatorAddress;
constexpr char kRole[] = "responder";
#else
constexpr uint16_t kLocalAddress = kInitiatorAddress;
constexpr uint16_t kPeerAddress = kResponderAddress;
constexpr char kRole[] = "initiator";
#endif

hal_lora_radio_t s_radio = nullptr;
hal_lora_link_t s_link = nullptr;
hal_command_router_t s_router = nullptr;
hal_lora_commands_t s_commands = nullptr;
bool s_ready = false;
uint32_t s_handler_calls = 0u;
uint32_t s_session_id = 0u;
uint32_t s_next_ready_ms = 0u;

#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
uint8_t s_expected_payload[kPayloadLength]{};
bool s_request_active = false;
uint32_t s_expected_request_id = 0u;
uint32_t s_expected_crc = 0u;
uint32_t s_request_started_ms = 0u;
uint32_t s_next_request_ms = 0u;
uint32_t s_payload_generation = 0u;
#endif

hal_lora_modem_config_t modem_config(const hal_lora_radio_config_t &hardware) {
  hal_lora_modem_config_t modem = hal_lora_default_eu868();
  modem.tx_power_dbm = 10;
  if (hardware.hardware.sx126x.max_frequency_hz < UINT32_C(800000000)) {
    /* Deliberate LF hardware-test configuration, not a regulatory preset. */
    modem.frequency_hz = kLfTestFrequencyHz;
  }
  return modem;
}

uint32_t example_session_id(void) {
  uint8_t seed[HAL_DEVICE_UID_BYTES + sizeof(uint32_t) + sizeof(uint16_t)]{};
  (void)hal_get_device_uid(seed);
  const uint32_t started_us = hal_micros();
  memcpy(&seed[HAL_DEVICE_UID_BYTES], &started_us, sizeof(started_us));
  memcpy(&seed[HAL_DEVICE_UID_BYTES + sizeof(started_us)], &kLocalAddress,
         sizeof(kLocalAddress));
  uint32_t session_id = hal_crc32(seed, sizeof(seed));
  if (session_id == 0u) {
    session_id = static_cast<uint32_t>(kLocalAddress);
  }
  return session_id;
}

hal_status_t echo_handler(const hal_command_request_t *request,
                          hal_command_response_t *response, void *) {
  if (request == nullptr || response == nullptr) {
    return HAL_EINVAL;
  }

  uint8_t fragments = 0u;
  int16_t rssi_dbm = 0;
  int8_t snr_db = 0;
  if (request->source == HAL_COMMAND_SOURCE_LORA_LINK &&
      request->source_context != nullptr) {
    const auto *link_info = static_cast<const hal_lora_link_message_info_t *>(
        request->source_context);
    fragments = link_info->fragment_count;
    rssi_dbm = link_info->packet.rssi_dbm;
    snr_db = link_info->packet.snr_db;
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  const uint32_t crc = hal_crc32(request->arguments, request->arguments_length);
  const uint32_t handler_call = ++s_handler_calls;
  deb("JHCMD1 HANDLE id=%lu len=%u crc=%08lX fragments=%u status=%s "
      "call=%lu source=%s peer=0x%04llX session=0x%08llX "
      "security=0x%08lX rssi=%d snr=%d",
      static_cast<unsigned long>(request->request_id),
      static_cast<unsigned>(request->arguments_length),
      static_cast<unsigned long>(crc), static_cast<unsigned>(fragments),
      hal_status_to_string(status), static_cast<unsigned long>(handler_call),
      hal_command_source_to_string(request->source),
      static_cast<unsigned long long>(request->peer_id),
      static_cast<unsigned long long>(request->session_id),
      static_cast<unsigned long>(request->security_flags),
      static_cast<int>(rssi_dbm), static_cast<int>(snr_db));
  return status;
}

hal_status_t register_routes(void) {
  hal_status_t status = hal_command_router_default(&s_router);
  if (status != HAL_OK) {
    return status;
  }

  hal_command_definition_t echo = {};
  echo.name = kCommandName;
  echo.allowed_sources = HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_LORA_LINK) |
                         HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM);
  echo.required_security = 0u;
  echo.handler = echo_handler;
  return hal_command_router_register(s_router, &echo);
}

void report_error(const char *stage, hal_status_t status) {
  derr("JHCMD1 ERROR stage=%s status=%s", stage, hal_status_to_string(status));
}

void report_ready(void) {
  deb("JHCMD1 READY role=%s local=0x%04X peer=0x%04X payload=%u "
      "sources=LORA_LINK|BLE_STREAM session=0x%08lX",
      kRole, static_cast<unsigned>(kLocalAddress),
      static_cast<unsigned>(kPeerAddress),
      static_cast<unsigned>(kPayloadLength),
      static_cast<unsigned long>(s_session_id));
  s_next_ready_ms = hal_millis() + kReadyPeriodMs;
}

void report_ready_if_due(void) {
  if (static_cast<int32_t>(hal_millis() - s_next_ready_ms) >= 0) {
    report_ready();
  }
}

#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
void fill_expected_payload(void) {
  const uint32_t generation = ++s_payload_generation;
  for (size_t index = 0u; index < sizeof(s_expected_payload); ++index) {
    s_expected_payload[index] =
        static_cast<uint8_t>(index * 29u + generation * 17u + (index >> 8u));
  }
  s_expected_crc = hal_crc32(s_expected_payload, sizeof(s_expected_payload));
}

void start_request(void) {
  if (s_request_active ||
      static_cast<int32_t>(hal_millis() - s_next_request_ms) < 0) {
    return;
  }

  fill_expected_payload();
  uint32_t request_id = 0u;
  const hal_status_t status = hal_lora_commands_request_start(
      s_commands, kPeerAddress, kCommandName, HAL_COMMAND_ENCODING_BINARY,
      s_expected_payload, sizeof(s_expected_payload), &request_id);
  if (status == HAL_EBUSY || status == HAL_EAGAIN) {
    s_next_request_ms = hal_millis() + 100u;
    return;
  }
  if (status != HAL_OK) {
    report_error("request_start", status);
    s_next_request_ms = hal_millis() + kRequestPeriodMs;
    return;
  }

  hal_lora_commands_info_t info = {};
  const hal_status_t info_status =
      hal_lora_commands_get_info(s_commands, &info);
  if (info_status != HAL_OK) {
    report_error("request_info", info_status);
  }

  s_expected_request_id = request_id;
  s_request_started_ms = hal_millis();
  s_request_active = true;
  deb("JHCMD1 REQUEST id=%lu len=%u crc=%08lX fragments=%u",
      static_cast<unsigned long>(request_id),
      static_cast<unsigned>(sizeof(s_expected_payload)),
      static_cast<unsigned long>(s_expected_crc),
      static_cast<unsigned>(info.link_send.fragment_count));
}

void receive_response(void) {
  hal_command_message_t response = {};
  hal_lora_link_message_info_t link_info = {};
  const hal_status_t receive_status =
      hal_lora_commands_receive(s_commands, &response, &link_info);
  if (receive_status == HAL_EAGAIN) {
    return;
  }
  if (receive_status != HAL_OK) {
    report_error("receive", receive_status);
    return;
  }

  const uint32_t response_crc =
      hal_crc32(response.payload, response.payload_length);
  const bool current_request = s_request_active &&
                               response.type == HAL_COMMAND_MESSAGE_RESPONSE &&
                               response.request_id == s_expected_request_id;
  const bool length_matches =
      response.payload_length == sizeof(s_expected_payload);
  const bool payload_matches =
      length_matches && memcmp(response.payload, s_expected_payload,
                               sizeof(s_expected_payload)) == 0;
  const bool matches = current_request && response.status == HAL_OK &&
                       response.encoding == HAL_COMMAND_ENCODING_BINARY &&
                       response_crc == s_expected_crc && payload_matches;

  const hal_command_security_flags_t security_flags =
      link_info.encrypted ? HAL_COMMAND_SECURITY_ALL : 0u;
  deb("JHCMD1 RESPONSE id=%lu len=%u crc=%08lX fragments=%u status=%s "
      "match=%u source=0x%04X session=0x%08lX security=0x%08lX "
      "rssi=%d snr=%d",
      static_cast<unsigned long>(response.request_id),
      static_cast<unsigned>(response.payload_length),
      static_cast<unsigned long>(response_crc),
      static_cast<unsigned>(link_info.fragment_count),
      hal_status_to_string(response.status), matches ? 1u : 0u,
      static_cast<unsigned>(link_info.source),
      static_cast<unsigned long>(link_info.session_id),
      static_cast<unsigned long>(security_flags),
      static_cast<int>(link_info.packet.rssi_dbm),
      static_cast<int>(link_info.packet.snr_db));

  if (current_request) {
    s_request_active = false;
    s_next_request_ms = hal_millis() + kRequestPeriodMs;
  }
}

void check_response_timeout(void) {
  if (!s_request_active ||
      hal_millis() - s_request_started_ms < kResponseTimeoutMs) {
    return;
  }
  derr("JHCMD1 TIMEOUT id=%lu len=%u crc=%08lX",
       static_cast<unsigned long>(s_expected_request_id),
       static_cast<unsigned>(sizeof(s_expected_payload)),
       static_cast<unsigned long>(s_expected_crc));
  s_request_active = false;
  s_next_request_ms = hal_millis() + kRequestPeriodMs;
}
#endif

} // namespace

extern "C" void app_start(void) {
  hal_debug_init_default();

  hal_lora_radio_config_t hardware = {};
  hal_status_t status = hal_lora_radio_config_from_board(&hardware);
  if (status == HAL_OK) {
    status = hal_spi_init(hardware.spi_bus, hardware.spi_miso_pin,
                          hardware.spi_mosi_pin, hardware.spi_sck_pin);
  }
  if (status == HAL_OK) {
    status = hal_lora_radio_create(&hardware, &s_radio);
  }
  if (status == HAL_OK) {
    const hal_lora_modem_config_t modem = modem_config(hardware);
    status = hal_lora_radio_configure(s_radio, &modem);
  }

  s_session_id = example_session_id();
  if (status == HAL_OK) {
    hal_lora_link_config_t link_config =
        hal_lora_link_config_defaults(s_radio, kLocalAddress, s_session_id);
    status = hal_lora_link_create(&link_config, &s_link);
  }
  if (status == HAL_OK) {
    status = register_routes();
  }
  if (status == HAL_OK) {
    hal_lora_commands_config_t commands_config =
        hal_lora_commands_config_defaults(s_link, kApplicationPort);
    commands_config.router = s_router;
    status = hal_lora_commands_create(&commands_config, &s_commands);
  }
  if (status != HAL_OK) {
    report_error("setup", status);
    return;
  }

  s_ready = true;
  report_ready();
#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
  s_next_request_ms = hal_millis() + kFirstRequestDelayMs;
#endif
}

extern "C" void app_task0(void) {
  if (!s_ready) {
    hal_delay_ms(100u);
    return;
  }

  const hal_status_t status = hal_lora_commands_process(s_commands);
  if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_IGNORED &&
      status != HAL_ETIMEOUT) {
    report_error("process", status);
  }
  report_ready_if_due();

#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
  receive_response();
  check_response_timeout();
  start_request();
#endif
}

#endif /* HAL_ENABLE_LORA_COMMANDS */
