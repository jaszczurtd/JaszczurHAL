#include "ble_stream_test_fixture.h"
#include "hal/bluetooth/hal_ble_commands.h"
#include "hal/bluetooth/jh_ble_stream_runtime.h"
#include "hal/commands/hal_command_router.h"
#include "hal/commands/hal_command_wire.h"

#include <string.h>

using namespace jh_test_ble_stream;

namespace {

constexpr uint32_t kInitialRequestId = 7u;
constexpr uint32_t kPartialTimeoutMs = 25u;

hal_ble_commands_t s_commands = nullptr;
hal_command_router_t s_router = nullptr;

struct handler_observation_t {
  uint32_t calls;
  hal_command_source_t source;
  hal_command_encoding_t encoding;
  hal_command_security_flags_t security_flags;
  uint64_t peer_id;
  uint64_t session_id;
  uint32_t request_id;
  hal_ble_commands_peer_info_t peer;
};

struct reentrant_observation_t {
  hal_ble_commands_t commands;
  uint32_t calls;
  hal_status_t info_status;
  hal_status_t process_status;
  hal_status_t destroy_status;
  hal_status_t event_status;
  hal_status_t request_status;
  uint32_t nested_request_id;
  bool process_active;
  bool dispatch_active;
};

struct session_switch_observation_t {
  hal_ble_commands_t commands;
  uint32_t calls;
  hal_status_t event_status;
};

hal_status_t echo_handler(const hal_command_request_t *request,
                          hal_command_response_t *response, void *user) {
  auto *observation = static_cast<handler_observation_t *>(user);
  ++observation->calls;
  observation->source = request->source;
  observation->encoding = request->encoding;
  observation->security_flags = request->security_flags;
  observation->peer_id = request->peer_id;
  observation->session_id = request->session_id;
  observation->request_id = request->request_id;
  if (request->source_context != nullptr) {
    observation->peer = *static_cast<const hal_ble_commands_peer_info_t *>(
        request->source_context);
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

hal_status_t reentrant_handler(const hal_command_request_t *request,
                               hal_command_response_t *response, void *user) {
  auto *observation = static_cast<reentrant_observation_t *>(user);
  ++observation->calls;

  hal_ble_commands_info_t info{};
  observation->info_status =
      hal_ble_commands_get_info(observation->commands, &info);
  observation->process_active = info.process_active;
  observation->dispatch_active = info.dispatch_active;
  observation->process_status = hal_ble_commands_process(observation->commands);
  observation->destroy_status = hal_ble_commands_destroy(observation->commands);

  const uint8_t nested_payload[] = {0xC3u};
  observation->event_status = hal_ble_commands_event_start(
      observation->commands, "nested-event", HAL_COMMAND_ENCODING_BINARY,
      nested_payload, sizeof(nested_payload));
  observation->request_status = hal_ble_commands_request_start(
      observation->commands, "nested-request", HAL_COMMAND_ENCODING_BINARY,
      nested_payload, sizeof(nested_payload), &observation->nested_request_id);

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

hal_status_t session_switch_handler(const hal_command_request_t *request,
                                    hal_command_response_t *response,
                                    void *user) {
  auto *observation = static_cast<session_switch_observation_t *>(user);
  ++observation->calls;
  authenticate();
  const uint8_t payload[] = {0x91u, 0x92u};
  observation->event_status = hal_ble_commands_event_start(
      observation->commands, "new-session", HAL_COMMAND_ENCODING_BINARY,
      payload, sizeof(payload));

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

hal_status_t invalid_response_length_handler(const hal_command_request_t *,
                                             hal_command_response_t *response,
                                             void *) {
  response->body_len = sizeof(response->body) + 1u;
  return HAL_OK;
}

void register_handler(const char *name, hal_command_handler_t handler,
                      void *user) {
  hal_command_definition_t definition{};
  definition.name = name;
  definition.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM);
  definition.required_security = HAL_COMMAND_SECURITY_ALL;
  definition.handler = handler;
  definition.user = user;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_router, &definition));
}

size_t encode_message(hal_command_message_type_t type, uint32_t request_id,
                      hal_status_t status, const char *name,
                      hal_command_encoding_t encoding, const void *payload,
                      size_t payload_length, uint8_t *wire) {
  hal_command_message_t message{};
  message.type = type;
  message.encoding = encoding;
  message.request_id = request_id;
  message.status = status;
  if (name != nullptr) {
    const size_t name_length = strlen(name);
    TEST_ASSERT_LESS_THAN(sizeof(message.name), name_length);
    memcpy(message.name, name, name_length + 1u);
  }
  TEST_ASSERT_LESS_OR_EQUAL(sizeof(message.payload), payload_length);
  if (payload_length > 0u) {
    TEST_ASSERT_NOT_NULL(payload);
    memcpy(message.payload, payload, payload_length);
  }
  message.payload_length = payload_length;

  size_t wire_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_message_encode(&message, wire,
                                         HAL_COMMAND_WIRE_MAX_FRAME_SIZE,
                                         &wire_length));
  return wire_length;
}

void inject_wire(const uint8_t *wire, size_t wire_length, size_t chunk_size) {
  size_t offset = 0u;
  while (offset < wire_length) {
    const size_t remaining = wire_length - offset;
    const size_t chunk = remaining < chunk_size ? remaining : chunk_size;
    TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(&wire[offset], chunk));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
    offset += chunk;
  }
}

size_t read_device_wire(uint8_t *wire) {
  size_t wire_length = 0u;
  size_t expected_length = HAL_COMMAND_WIRE_HEADER_SIZE;
  size_t notifications = hal_mock_ble_stream_notify_count();

  for (size_t attempt = 0u; attempt < 64u; ++attempt) {
    const hal_status_t process_status = hal_ble_commands_process(s_commands);
    TEST_ASSERT_TRUE(process_status == HAL_OK || process_status == HAL_EAGAIN);
    const size_t current_notifications = hal_mock_ble_stream_notify_count();
    if (current_notifications == notifications) {
      continue;
    }
    TEST_ASSERT_EQUAL_size_t(notifications + 1u, current_notifications);
    notifications = current_notifications;

    uint8_t chunk[HAL_BLE_STREAM_MAX_PAYLOAD]{};
    size_t chunk_length = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          client_receive(chunk, sizeof(chunk), &chunk_length));
    TEST_ASSERT_LESS_OR_EQUAL(HAL_COMMAND_WIRE_MAX_FRAME_SIZE - wire_length,
                              chunk_length);
    memcpy(&wire[wire_length], chunk, chunk_length);
    wire_length += chunk_length;

    const hal_status_t frame_status =
        hal_command_message_frame_size(wire, wire_length, &expected_length);
    TEST_ASSERT_TRUE(frame_status == HAL_OK || frame_status == HAL_EAGAIN);
    if (frame_status == HAL_OK) {
      TEST_ASSERT_EQUAL_size_t(expected_length, wire_length);
      return wire_length;
    }
  }

  TEST_FAIL_MESSAGE("BLE command wire did not complete");
  return 0u;
}

hal_command_message_t decode_device_message(void) {
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = read_device_wire(wire);
  hal_command_message_t message{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_message_decode(wire, wire_length, &message));
  return message;
}

uint64_t expected_peer_id(const hal_ble_address_t &peer) {
  uint64_t value = (uint64_t)peer.type << 56u;
  for (size_t index = 0u; index < HAL_BLE_ADDRESS_LEN; ++index) {
    value |= (uint64_t)peer.bytes[index]
             << ((HAL_BLE_ADDRESS_LEN - 1u - index) * 8u);
  }
  return value;
}

void create_adapter(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&s_router));
  hal_ble_commands_config_t config = hal_ble_commands_config_defaults();
  config.router = s_router;
  config.initial_request_id = kInitialRequestId;
  config.partial_frame_timeout_ms = kPartialTimeoutMs;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_create(&config, &s_commands));
}

void cleanup_adapter(void) {
  if (s_commands != nullptr) {
    (void)hal_ble_stream_close_session(HAL_BLE_STREAM_CLOSE_LOCAL_REQUEST);
    (void)hal_ble_commands_process(s_commands);
    (void)hal_ble_commands_destroy(s_commands);
    s_commands = nullptr;
  }
  if (s_router != nullptr) {
    (void)hal_command_router_destroy(s_router);
    s_router = nullptr;
  }
}

} // namespace

void setUp(void) {
  s_commands = nullptr;
  s_router = nullptr;
  hal_mock_set_millis(0u);
  setup_stream();
  authenticate();
  create_adapter();
}

void tearDown(void) {
  cleanup_adapter();
  (void)hal_ble_stream_deinitialize();
  (void)hal_ble_deinitialize();
  hal_mock_secure_random_reset();
}

static void test_defaults_lifecycle_stale_handle_and_validation(void) {
  const hal_ble_commands_config_t defaults = hal_ble_commands_config_defaults();
  TEST_ASSERT_NULL(defaults.router);
  TEST_ASSERT_EQUAL_UINT32(1u, defaults.initial_request_id);
  TEST_ASSERT_EQUAL_UINT32(HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS,
                           defaults.partial_frame_timeout_ms);

  hal_ble_commands_t duplicate = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_ble_commands_create(&defaults, &duplicate));
  TEST_ASSERT_NULL(duplicate);

  uint32_t request_id = UINT32_MAX;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_ble_commands_request_start(s_commands, "", HAL_COMMAND_ENCODING_TEXT,
                                     nullptr, 0u, &request_id));
  TEST_ASSERT_EQUAL_UINT32(0u, request_id);
  uint8_t oversized[HAL_COMMAND_MESSAGE_MAX_PAYLOAD + 1u]{};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_ble_commands_event_start(
                            s_commands, "large", HAL_COMMAND_ENCODING_BINARY,
                            oversized, sizeof(oversized)));

  const uint8_t pending[] = {0x33u};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_event_start(s_commands, "pending",
                                           HAL_COMMAND_ENCODING_BINARY, pending,
                                           sizeof(pending)));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_commands_destroy(s_commands));

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_stream_close_session(HAL_BLE_STREAM_CLOSE_LOCAL_REQUEST));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  const hal_ble_commands_t stale = s_commands;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_destroy(s_commands));
  s_commands = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_ble_commands_destroy(stale));

  hal_ble_commands_config_t replacement_config =
      hal_ble_commands_config_defaults();
  replacement_config.router = s_router;
  hal_ble_commands_t replacement = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_create(&replacement_config, &replacement));
  TEST_ASSERT_NOT_EQUAL(stale, replacement);
  hal_ble_commands_info_t info{};
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_ble_commands_get_info(stale, &info));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_destroy(replacement));
}

static void test_info_hides_session_immediately_after_stream_close(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  hal_ble_commands_info_t adapter_info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_NOT_EQUAL(0u, adapter_info.session_id);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_stream_close_session(HAL_BLE_STREAM_CLOSE_LOCAL_REQUEST));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_EQUAL_UINT64(0u, adapter_info.session_id);
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED,
                        adapter_info.stream_state);
}

static void
test_fragmented_500_byte_round_trip_and_authenticated_metadata(void) {
  handler_observation_t observation{};
  register_handler("echo", echo_handler, &observation);

  uint8_t payload[500]{};
  for (size_t index = 0u; index < sizeof(payload); ++index) {
    payload[index] = (uint8_t)(index * 7u + 3u);
  }
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = encode_message(
      HAL_COMMAND_MESSAGE_REQUEST, 91u, HAL_NONE, "echo",
      HAL_COMMAND_ENCODING_BINARY, payload, sizeof(payload), wire);
  constexpr size_t chunk_size = 47u;
  const size_t expected_chunks = (wire_length + chunk_size - 1u) / chunk_size;
  inject_wire(wire, wire_length, chunk_size);

  const hal_command_message_t response = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, response.type);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_BINARY, response.encoding);
  TEST_ASSERT_EQUAL_UINT32(91u, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_size_t(sizeof(payload), response.payload_length);
  TEST_ASSERT_EQUAL_MEMORY(payload, response.payload, sizeof(payload));

  hal_ble_info_t ble{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_get_info(&ble));
  const hal_ble_stream_info_t stream = info();
  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_BLE_STREAM, observation.source);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_BINARY, observation.encoding);
  TEST_ASSERT_EQUAL_UINT32(HAL_COMMAND_SECURITY_ALL,
                           observation.security_flags);
  TEST_ASSERT_EQUAL_UINT64(expected_peer_id(ble.peer_address),
                           observation.peer_id);
  TEST_ASSERT_EQUAL_UINT64(stream.session_id, observation.session_id);
  TEST_ASSERT_EQUAL_UINT32(91u, observation.request_id);
  TEST_ASSERT_EQUAL_MEMORY(&ble.peer_address, &observation.peer.peer_address,
                           sizeof(ble.peer_address));
  TEST_ASSERT_EQUAL_UINT32(ble.connection, observation.peer.connection);
  TEST_ASSERT_EQUAL_UINT16(ble.mtu, observation.peer.mtu);
  TEST_ASSERT_EQUAL_UINT16(kCapabilities & kClientCapabilities,
                           observation.peer.negotiated_capabilities);
  TEST_ASSERT_EQUAL_UINT32(ble.generation, observation.peer.ble_generation);
  TEST_ASSERT_EQUAL_UINT32(stream.generation,
                           observation.peer.stream_generation);
  TEST_ASSERT_EQUAL_UINT64(stream.session_id, observation.peer.session_id);
  TEST_ASSERT_EQUAL_UINT64(1u, observation.peer.first_rx_counter);
  TEST_ASSERT_EQUAL_UINT64(expected_chunks, observation.peer.last_rx_counter);

  hal_ble_commands_diagnostics_t diagnostics{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.requests_received);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.responses_sent);
  TEST_ASSERT_EQUAL_UINT32(expected_chunks, diagnostics.stream_chunks_received);
  TEST_ASSERT_EQUAL_UINT32(5u, diagnostics.stream_chunks_sent);
  TEST_ASSERT_EQUAL_INT(HAL_OK, diagnostics.last_dispatch_status);
}

static void test_outgoing_request_response_and_bidirectional_events(void) {
  const uint8_t request_payload[] = {'p', 'i', 'n', 'g'};
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_request_start(
                                    s_commands, "remote",
                                    HAL_COMMAND_ENCODING_TEXT, request_payload,
                                    sizeof(request_payload), &request_id));
  TEST_ASSERT_EQUAL_UINT32(kInitialRequestId, request_id);
  uint32_t blocked_id = UINT32_MAX;
  TEST_ASSERT_EQUAL_INT(
      HAL_EBUSY, hal_ble_commands_request_start(s_commands, "blocked",
                                                HAL_COMMAND_ENCODING_TEXT,
                                                nullptr, 0u, &blocked_id));
  TEST_ASSERT_EQUAL_UINT32(0u, blocked_id);

  hal_command_message_t message = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_REQUEST, message.type);
  TEST_ASSERT_EQUAL_STRING("remote", message.name);
  TEST_ASSERT_EQUAL_UINT32(request_id, message.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_TEXT, message.encoding);
  TEST_ASSERT_EQUAL_MEMORY(request_payload, message.payload,
                           sizeof(request_payload));

  const uint8_t response_payload[] = {'p', 'o', 'n', 'g'};
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  size_t wire_length =
      encode_message(HAL_COMMAND_MESSAGE_RESPONSE, request_id, HAL_OK, nullptr,
                     HAL_COMMAND_ENCODING_TEXT, response_payload,
                     sizeof(response_payload), wire);
  inject_wire(wire, wire_length, wire_length);

  hal_ble_commands_peer_info_t peer{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_receive(s_commands, &message, &peer));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, message.type);
  TEST_ASSERT_EQUAL_UINT32(request_id, message.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, message.status);
  TEST_ASSERT_EQUAL_MEMORY(response_payload, message.payload,
                           sizeof(response_payload));
  TEST_ASSERT_EQUAL_UINT64(1u, peer.first_rx_counter);
  TEST_ASSERT_EQUAL_UINT64(1u, peer.last_rx_counter);

  const uint8_t event_payload[] = {0x31u, 0x32u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_event_start(
                                    s_commands, "local-state",
                                    HAL_COMMAND_ENCODING_BINARY, event_payload,
                                    sizeof(event_payload)));
  message = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("local-state", message.name);
  TEST_ASSERT_EQUAL_MEMORY(event_payload, message.payload,
                           sizeof(event_payload));

  const uint8_t remote_event_payload[] = {0x41u, 0x42u, 0x43u};
  wire_length =
      encode_message(HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "remote-state",
                     HAL_COMMAND_ENCODING_BINARY, remote_event_payload,
                     sizeof(remote_event_payload), wire);
  inject_wire(wire, wire_length, wire_length);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_receive(s_commands, &message, &peer));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("remote-state", message.name);
  TEST_ASSERT_EQUAL_MEMORY(remote_event_payload, message.payload,
                           sizeof(remote_event_payload));
  TEST_ASSERT_EQUAL_UINT64(2u, peer.first_rx_counter);
  TEST_ASSERT_EQUAL_UINT64(2u, peer.last_rx_counter);
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_ble_commands_receive(s_commands, &message, nullptr));
}

static void test_backpressure_retries_without_skipping_command_bytes(void) {
  uint8_t payload[500]{};
  for (size_t index = 0u; index < sizeof(payload); ++index) {
    payload[index] = (uint8_t)(index * 5u + 1u);
  }
  uint8_t expected_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t expected_length = encode_message(
      HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "bulk",
      HAL_COMMAND_ENCODING_BINARY, payload, sizeof(payload), expected_wire);
  TEST_ASSERT_EQUAL_size_t(520u, expected_length);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_event_start(s_commands, "bulk",
                                           HAL_COMMAND_ENCODING_BINARY, payload,
                                           sizeof(payload)));
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  for (size_t index = 0u; index < HAL_BLE_STREAM_TX_QUEUE_DEPTH; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));

  hal_ble_commands_info_t adapter_info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_EQUAL_size_t(512u, adapter_info.transmit_offset);
  hal_ble_commands_diagnostics_t diagnostics{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.send_retries);
  TEST_ASSERT_EQUAL_UINT32(4u, diagnostics.stream_chunks_sent);

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  uint8_t chunk[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  size_t chunk_length = 0u;
  for (size_t index = 0u; index < HAL_BLE_STREAM_TX_QUEUE_DEPTH; ++index) {
    TEST_ASSERT_EQUAL_UINT64(index + 1u, info().tx_counter);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          client_receive(chunk, sizeof(chunk), &chunk_length));
    TEST_ASSERT_EQUAL_size_t(HAL_BLE_STREAM_MAX_PAYLOAD, chunk_length);
    TEST_ASSERT_EQUAL_MEMORY(&expected_wire[index * HAL_BLE_STREAM_MAX_PAYLOAD],
                             chunk, chunk_length);
  }
  TEST_ASSERT_EQUAL_UINT64(4u, info().tx_counter);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        client_receive(chunk, sizeof(chunk), &chunk_length));
  TEST_ASSERT_EQUAL_size_t(expected_length - 512u, chunk_length);
  TEST_ASSERT_EQUAL_MEMORY(&expected_wire[512u], chunk, chunk_length);
  TEST_ASSERT_EQUAL_UINT64(5u, info().tx_counter);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.events_sent);
  TEST_ASSERT_EQUAL_UINT32(5u, diagnostics.stream_chunks_sent);
}

static void test_stale_session_tx_does_not_enter_new_session(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  const hal_ble_stream_info_t stale = info();

  authenticate();
  const hal_ble_stream_info_t current = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, current.state);
  TEST_ASSERT_NOT_EQUAL(stale.session_id, current.session_id);

  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  const size_t notifications = hal_mock_ble_stream_notify_count();
  const uint8_t stale_payload[] = {0xD1u, 0xD2u, 0xD3u};
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, jh_ble_stream_send_for_session(
                                       stale_payload, sizeof(stale_payload),
                                       stale.generation, stale.session_id));
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH,
                        jh_ble_stream_send_for_session(
                            stale_payload, sizeof(stale_payload),
                            current.generation + 1u, current.session_id));

  const hal_ble_stream_info_t after = info();
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, after.state);
  TEST_ASSERT_EQUAL_UINT32(current.generation, after.generation);
  TEST_ASSERT_EQUAL_UINT64(current.session_id, after.session_id);
  TEST_ASSERT_EQUAL_size_t(0u, after.pending_tx);
  TEST_ASSERT_EQUAL_UINT64(0u, after.tx_counter);
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  const uint8_t current_payload[] = {0xE1u, 0xE2u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_event_start(
                                    s_commands, "current-session",
                                    HAL_COMMAND_ENCODING_BINARY,
                                    current_payload, sizeof(current_payload)));
  const hal_command_message_t message = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("current-session", message.name);
  TEST_ASSERT_EQUAL_MEMORY(current_payload, message.payload,
                           sizeof(current_payload));
}

static void test_stale_session_rx_does_not_pop_new_session_payload(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  const hal_ble_stream_info_t stale = info();

  authenticate();
  const hal_ble_stream_info_t current = info();
  TEST_ASSERT_NOT_EQUAL(stale.session_id, current.session_id);

  const uint8_t event_payload[] = {0xB1u, 0xB2u, 0xB3u};
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = encode_message(
      HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "new-session-rx",
      HAL_COMMAND_ENCODING_BINARY, event_payload, sizeof(event_payload), wire);
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(wire, wire_length));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_rx);

  uint8_t chunk[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  size_t chunk_length = SIZE_MAX;
  hal_ble_stream_payload_info_t payload_info{};
  memset(&payload_info, 0xA5, sizeof(payload_info));
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH,
                        jh_ble_stream_receive_for_session(
                            chunk, sizeof(chunk), &chunk_length, &payload_info,
                            stale.generation, stale.session_id));
  TEST_ASSERT_EQUAL_size_t(0u, chunk_length);
  const hal_ble_stream_payload_info_t empty_info{};
  TEST_ASSERT_EQUAL_MEMORY(&empty_info, &payload_info, sizeof(payload_info));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_rx);
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);
  TEST_ASSERT_EQUAL_UINT64(current.session_id, info().session_id);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_rx);
  hal_command_message_t message{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_receive(s_commands, &message, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("new-session-rx", message.name);
  TEST_ASSERT_EQUAL_MEMORY(event_payload, message.payload,
                           sizeof(event_payload));
}

static void test_partial_frame_session_reset_and_timeout(void) {
  const uint8_t payload[] = {0x10u, 0x20u};
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = encode_message(
      HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "partial",
      HAL_COMMAND_ENCODING_BINARY, payload, sizeof(payload), wire);
  TEST_ASSERT_GREATER_THAN_UINT(HAL_COMMAND_WIRE_HEADER_SIZE, wire_length);

  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(wire, 8u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  hal_ble_commands_info_t adapter_info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_EQUAL_size_t(8u, adapter_info.receive_buffered);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_stream_close_session(HAL_BLE_STREAM_CLOSE_LOCAL_REQUEST));
  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_EQUAL_size_t(0u, adapter_info.receive_buffered);

  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(wire, 8u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  hal_mock_advance_millis(kPartialTimeoutMs + 1u);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, hal_ble_commands_process(s_commands));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  hal_ble_commands_diagnostics_t diagnostics{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.partial_frame_timeouts);
  TEST_ASSERT_EQUAL_UINT32(2u, diagnostics.session_starts);
  TEST_ASSERT_EQUAL_UINT32(2u, diagnostics.session_resets);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.protocol_errors);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, diagnostics.last_error);
}

static void test_malformed_wire_and_stream_overflow_close_sessions(void) {
  uint8_t malformed[HAL_COMMAND_WIRE_HEADER_SIZE]{};
  memcpy(malformed, "XX", 2u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(malformed, sizeof(malformed)));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, hal_ble_commands_process(s_commands));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  authenticate();
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  const uint8_t byte = 0x44u;
  for (size_t index = 0u; index < HAL_BLE_STREAM_RX_QUEUE_DEPTH + 1u; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(&byte, sizeof(byte)));
  }
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_ble_commands_process(s_commands));
  TEST_ASSERT_NOT_EQUAL(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  hal_ble_commands_diagnostics_t diagnostics{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(2u, diagnostics.protocol_errors);
  TEST_ASSERT_EQUAL_UINT32(2u, diagnostics.dropped_messages);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, diagnostics.last_error);
}

static void test_handler_reentrancy_preserves_event_and_response(void) {
  reentrant_observation_t observation{};
  observation.commands = s_commands;
  register_handler("reentrant", reentrant_handler, &observation);

  const uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = encode_message(
      HAL_COMMAND_MESSAGE_REQUEST, 55u, HAL_NONE, "reentrant",
      HAL_COMMAND_ENCODING_BINARY, payload, sizeof(payload), wire);
  inject_wire(wire, wire_length, wire_length);

  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);
  TEST_ASSERT_EQUAL_INT(HAL_OK, observation.info_status);
  TEST_ASSERT_TRUE(observation.process_active);
  TEST_ASSERT_TRUE(observation.dispatch_active);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, observation.process_status);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, observation.destroy_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, observation.event_status);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, observation.request_status);
  TEST_ASSERT_EQUAL_UINT32(0u, observation.nested_request_id);

  hal_command_message_t message = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("nested-event", message.name);
  const uint8_t nested_payload[] = {0xC3u};
  TEST_ASSERT_EQUAL_MEMORY(nested_payload, message.payload,
                           sizeof(nested_payload));

  message = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, message.type);
  TEST_ASSERT_EQUAL_UINT32(55u, message.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, message.status);
  TEST_ASSERT_EQUAL_MEMORY(payload, message.payload, sizeof(payload));
}

static void test_dispatch_session_switch_keeps_only_new_session_tx(void) {
  session_switch_observation_t observation{};
  observation.commands = s_commands;
  register_handler("switch", session_switch_handler, &observation);

  const uint8_t payload[] = {0x61u, 0x62u};
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = encode_message(
      HAL_COMMAND_MESSAGE_REQUEST, 77u, HAL_NONE, "switch",
      HAL_COMMAND_ENCODING_BINARY, payload, sizeof(payload), wire);
  TEST_ASSERT_LESS_OR_EQUAL(HAL_BLE_STREAM_MAX_PAYLOAD, wire_length);
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(wire, wire_length));
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);
  TEST_ASSERT_EQUAL_INT(HAL_OK, observation.event_status);

  const hal_command_message_t message = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("new-session", message.name);
  const uint8_t expected[] = {0x91u, 0x92u};
  TEST_ASSERT_EQUAL_MEMORY(expected, message.payload, sizeof(expected));

  const size_t notifications = hal_mock_ble_stream_notify_count();
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_size_t(notifications, hal_mock_ble_stream_notify_count());
  hal_ble_commands_diagnostics_t diagnostics{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(0u, diagnostics.responses_sent);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.events_sent);
}

static void test_invalid_handler_body_length_returns_bounded_overflow(void) {
  register_handler("bad-length", invalid_response_length_handler, nullptr);
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length =
      encode_message(HAL_COMMAND_MESSAGE_REQUEST, 81u, HAL_NONE, "bad-length",
                     HAL_COMMAND_ENCODING_BINARY, nullptr, 0u, wire);
  inject_wire(wire, wire_length, wire_length);

  const hal_command_message_t response = decode_device_message();
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, response.type);
  TEST_ASSERT_EQUAL_UINT32(81u, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, response.status);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_BINARY, response.encoding);
  TEST_ASSERT_EQUAL_size_t(0u, response.payload_length);
}

static void test_destroy_waits_for_stream_rx_and_tx_queues(void) {
  const uint8_t payload[] = {0x71u, 0x72u};
  uint8_t wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t wire_length = encode_message(
      HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "queued-rx",
      HAL_COMMAND_ENCODING_BINARY, payload, sizeof(payload), wire);
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(wire, wire_length));
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_rx);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_commands_destroy(s_commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  hal_command_message_t message{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_receive(s_commands, &message, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("queued-rx", message.name);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_destroy(s_commands));
  s_commands = nullptr;

  hal_ble_commands_config_t config = hal_ble_commands_config_defaults();
  config.router = s_router;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_create(&config, &s_commands));
  hal_mock_ble_set_stream_notify_status(HAL_EAGAIN);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_event_start(s_commands, "queued-tx",
                                           HAL_COMMAND_ENCODING_BINARY, payload,
                                           sizeof(payload)));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  hal_ble_commands_info_t adapter_info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_EQUAL_size_t(0u, adapter_info.transmit_length);
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_ble_commands_destroy(s_commands));

  hal_mock_ble_set_stream_notify_status(HAL_OK);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mock_ble_inject_stream_can_send());
  TEST_ASSERT_EQUAL_size_t(1u, info().pending_tx);
  uint8_t delivered[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  size_t delivered_length = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, client_receive(delivered, sizeof(delivered), &delivered_length));
  TEST_ASSERT_EQUAL_size_t(0u, info().pending_tx);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_destroy(s_commands));
  s_commands = nullptr;
}

static void test_max_frame_and_next_prefix_share_one_stream_chunk(void) {
  char max_name[HAL_COMMAND_ROUTER_NAME_MAX]{};
  memset(max_name, 'm', sizeof(max_name) - 1u);
  uint8_t max_payload[HAL_COMMAND_MESSAGE_MAX_PAYLOAD]{};
  for (size_t index = 0u; index < sizeof(max_payload); ++index) {
    max_payload[index] = (uint8_t)(index * 11u + 5u);
  }
  uint8_t first_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t first_length =
      encode_message(HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, max_name,
                     HAL_COMMAND_ENCODING_BINARY, max_payload,
                     sizeof(max_payload), first_wire);
  TEST_ASSERT_EQUAL_size_t(HAL_COMMAND_WIRE_MAX_FRAME_SIZE, first_length);

  const uint8_t next_payload[] = {0xA1u, 0xA2u, 0xA3u};
  uint8_t next_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t next_length =
      encode_message(HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "next",
                     HAL_COMMAND_ENCODING_BINARY, next_payload,
                     sizeof(next_payload), next_wire);
  constexpr size_t first_tail = 100u;
  const size_t initial_length = first_length - first_tail;
  size_t offset = 0u;
  while (offset < initial_length) {
    const size_t remaining = initial_length - offset;
    const size_t chunk = remaining < 37u ? remaining : 37u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(&first_wire[offset], chunk));
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
    offset += chunk;
  }

  uint8_t boundary_chunk[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  TEST_ASSERT_LESS_OR_EQUAL(sizeof(boundary_chunk), first_tail + next_length);
  memcpy(boundary_chunk, &first_wire[initial_length], first_tail);
  memcpy(&boundary_chunk[first_tail], next_wire, next_length);
  const size_t boundary_length = first_tail + next_length;
  TEST_ASSERT_EQUAL_INT(HAL_OK, client_send(boundary_chunk, boundary_length));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));

  hal_command_message_t message{};
  hal_ble_commands_peer_info_t peer{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_receive(s_commands, &message, &peer));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING(max_name, message.name);
  TEST_ASSERT_EQUAL_size_t(sizeof(max_payload), message.payload_length);
  TEST_ASSERT_EQUAL_MEMORY(max_payload, message.payload, sizeof(max_payload));
  const uint64_t boundary_counter = peer.last_rx_counter;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_receive(s_commands, &message, &peer));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("next", message.name);
  TEST_ASSERT_EQUAL_MEMORY(next_payload, message.payload, sizeof(next_payload));
  TEST_ASSERT_EQUAL_UINT64(boundary_counter, peer.first_rx_counter);
  TEST_ASSERT_EQUAL_UINT64(boundary_counter, peer.last_rx_counter);
}

static void
test_complete_buffered_event_does_not_expire_while_first_is_unread(void) {
  const uint8_t first_payload[] = {0x11u, 0x12u};
  const uint8_t second_payload[] = {0x21u, 0x22u, 0x23u};
  uint8_t first_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  uint8_t second_wire[HAL_COMMAND_WIRE_MAX_FRAME_SIZE]{};
  const size_t first_length =
      encode_message(HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "first-buffered",
                     HAL_COMMAND_ENCODING_BINARY, first_payload,
                     sizeof(first_payload), first_wire);
  const size_t second_length =
      encode_message(HAL_COMMAND_MESSAGE_EVENT, 0u, HAL_NONE, "second-buffered",
                     HAL_COMMAND_ENCODING_BINARY, second_payload,
                     sizeof(second_payload), second_wire);
  TEST_ASSERT_LESS_OR_EQUAL(HAL_BLE_STREAM_MAX_PAYLOAD,
                            first_length + second_length);

  uint8_t combined[HAL_BLE_STREAM_MAX_PAYLOAD]{};
  memcpy(combined, first_wire, first_length);
  memcpy(&combined[first_length], second_wire, second_length);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        client_send(combined, first_length + second_length));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));

  hal_ble_commands_info_t adapter_info{};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_ble_commands_get_info(s_commands, &adapter_info));
  TEST_ASSERT_TRUE(adapter_info.receive_ready);
  TEST_ASSERT_EQUAL_size_t(second_length, adapter_info.receive_buffered);

  hal_mock_advance_millis(kPartialTimeoutMs + 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_INT(HAL_BLE_STREAM_STATE_AUTHENTICATED, info().state);

  hal_ble_commands_diagnostics_t diagnostics{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_get_diagnostics(s_commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(0u, diagnostics.partial_frame_timeouts);
  TEST_ASSERT_EQUAL_UINT32(0u, diagnostics.protocol_errors);

  hal_command_message_t message{};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_receive(s_commands, &message, nullptr));
  TEST_ASSERT_EQUAL_STRING("first-buffered", message.name);
  TEST_ASSERT_EQUAL_MEMORY(first_payload, message.payload,
                           sizeof(first_payload));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_ble_commands_process(s_commands));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_ble_commands_receive(s_commands, &message, nullptr));
  TEST_ASSERT_EQUAL_STRING("second-buffered", message.name);
  TEST_ASSERT_EQUAL_MEMORY(second_payload, message.payload,
                           sizeof(second_payload));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_lifecycle_stale_handle_and_validation);
  RUN_TEST(test_info_hides_session_immediately_after_stream_close);
  RUN_TEST(test_fragmented_500_byte_round_trip_and_authenticated_metadata);
  RUN_TEST(test_outgoing_request_response_and_bidirectional_events);
  RUN_TEST(test_backpressure_retries_without_skipping_command_bytes);
  RUN_TEST(test_stale_session_tx_does_not_enter_new_session);
  RUN_TEST(test_stale_session_rx_does_not_pop_new_session_payload);
  RUN_TEST(test_partial_frame_session_reset_and_timeout);
  RUN_TEST(test_malformed_wire_and_stream_overflow_close_sessions);
  RUN_TEST(test_handler_reentrancy_preserves_event_and_response);
  RUN_TEST(test_dispatch_session_switch_keeps_only_new_session_tx);
  RUN_TEST(test_invalid_handler_body_length_returns_bounded_overflow);
  RUN_TEST(test_destroy_waits_for_stream_rx_and_tx_queues);
  RUN_TEST(test_max_frame_and_next_prefix_share_one_stream_chunk);
  RUN_TEST(test_complete_buffered_event_does_not_expire_while_first_is_unread);
  return UNITY_END();
}
