#include "hal/commands/hal_command_router.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/radio/hal_lora_commands.h"
#include "lora_test_fixture.h"
#include "utils/unity.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string.h>
#include <thread>

static const uint8_t kKey[HAL_LORA_LINK_CRYPTO_KEY_BYTES] = {
    0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u, 0x88u, 0x89u, 0x8Au,
    0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu, 0x90u, 0x91u, 0x92u, 0x93u, 0x94u, 0x95u,
    0x96u, 0x97u, 0x98u, 0x99u, 0x9Au, 0x9Bu, 0x9Cu, 0x9Du, 0x9Eu, 0x9Fu,
};

typedef struct {
  hal_lora_radio_t radio;
  hal_lora_link_t link;
  hal_lora_commands_t commands;
} endpoint_t;

typedef struct {
  uint32_t calls;
  hal_command_source_t source;
  hal_command_security_flags_t security_flags;
  uint64_t peer_id;
  uint64_t session_id;
  uint32_t request_id;
  uint16_t link_source;
  uint8_t link_fragments;
  bool link_encrypted;
} handler_observation_t;

typedef struct {
  hal_lora_commands_t commands;
  hal_status_t info_status;
  hal_status_t process_status;
  hal_status_t destroy_status;
  hal_status_t event_status;
  hal_status_t request_status;
  uint32_t nested_request_id;
  bool process_active;
  bool dispatch_active;
  uint32_t calls;
} reentrant_observation_t;

struct blocking_observation_t {
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false;
  bool release = false;
  uint32_t calls = 0u;
};

static endpoint_t
create_endpoint_with_ack(uint16_t address, uint32_t session_id, bool encrypted,
                         hal_command_router_t router, uint8_t port,
                         uint32_t initial_request_id, bool acknowledged) {
  endpoint_t endpoint = {};
  const hal_lora_radio_config_t hardware = jh_test_lora_radio_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_create(&hardware, &endpoint.radio));
  const hal_lora_modem_config_t modem = hal_lora_default_eu868();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_radio_configure(endpoint.radio, &modem));

  hal_lora_link_config_t link_config =
      hal_lora_link_config_defaults(endpoint.radio, address, session_id);
  link_config.max_retries = 2u;
  link_config.acknowledgement_timeout_ms = 10u;
  link_config.retry_backoff_ms = 0u;
  if (encrypted) {
    link_config.security = HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305;
    link_config.key = kKey;
    link_config.key_length = sizeof(kKey);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_link_create(&link_config, &endpoint.link));

  hal_lora_commands_config_t command_config =
      hal_lora_commands_config_defaults(endpoint.link, port);
  command_config.router = router;
  command_config.acknowledged = acknowledged;
  command_config.initial_request_id = initial_request_id;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_create(&command_config, &endpoint.commands));
  return endpoint;
}

static endpoint_t create_endpoint(uint16_t address, uint32_t session_id,
                                  bool encrypted, hal_command_router_t router,
                                  uint8_t port, uint32_t initial_request_id) {
  return create_endpoint_with_ack(address, session_id, encrypted, router, port,
                                  initial_request_id, true);
}

static void destroy_endpoint(endpoint_t *endpoint) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_destroy(endpoint->commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_link_destroy(endpoint->link));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_radio_destroy(endpoint->radio));
  *endpoint = {};
}

static void assert_process_status(hal_status_t status) {
  TEST_ASSERT_TRUE(status == HAL_OK || status == HAL_EAGAIN ||
                   status == HAL_IGNORED || status == HAL_ETIMEOUT);
}

static void pump(endpoint_t *first, endpoint_t *second, uint32_t iterations) {
  for (uint32_t iteration = 0u; iteration < iterations; ++iteration) {
    assert_process_status(hal_lora_commands_process(first->commands));
    assert_process_status(hal_lora_commands_process(second->commands));
    hal_mock_advance_millis(1u);
  }
}

static hal_status_t echo_handler(const hal_command_request_t *request,
                                 hal_command_response_t *response, void *user) {
  handler_observation_t *observation =
      static_cast<handler_observation_t *>(user);
  ++observation->calls;
  observation->source = request->source;
  observation->security_flags = request->security_flags;
  observation->peer_id = request->peer_id;
  observation->session_id = request->session_id;
  observation->request_id = request->request_id;
  const hal_lora_link_message_info_t *link_info =
      static_cast<const hal_lora_link_message_info_t *>(
          request->source_context);
  TEST_ASSERT_NOT_NULL(link_info);
  observation->link_source = link_info->source;
  observation->link_fragments = link_info->fragment_count;
  observation->link_encrypted = link_info->encrypted;

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

static hal_status_t reentrant_handler(const hal_command_request_t *request,
                                      hal_command_response_t *response,
                                      void *user) {
  reentrant_observation_t *observation =
      static_cast<reentrant_observation_t *>(user);
  ++observation->calls;

  hal_lora_commands_info_t info = {};
  observation->info_status =
      hal_lora_commands_get_info(observation->commands, &info);
  observation->process_active = info.process_active;
  observation->dispatch_active = info.dispatch_active;
  observation->process_status =
      hal_lora_commands_process(observation->commands);
  observation->destroy_status =
      hal_lora_commands_destroy(observation->commands);

  const uint16_t destination = (uint16_t)request->peer_id;
  const uint8_t nested_payload[] = {0xC3u};
  observation->event_status = hal_lora_commands_event_start(
      observation->commands, destination, "nested-event",
      HAL_COMMAND_ENCODING_BINARY, nested_payload, sizeof(nested_payload));
  observation->request_status = hal_lora_commands_request_start(
      observation->commands, destination, "nested-request",
      HAL_COMMAND_ENCODING_BINARY, nested_payload, sizeof(nested_payload),
      &observation->nested_request_id);

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

static hal_status_t blocking_handler(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  blocking_observation_t *observation =
      static_cast<blocking_observation_t *>(user);
  {
    std::unique_lock<std::mutex> lock(observation->mutex);
    ++observation->calls;
    observation->entered = true;
    observation->condition.notify_all();
    observation->condition.wait(lock,
                                [observation] { return observation->release; });
  }

  hal_status_t status =
      hal_command_response_set_encoding(response, request->encoding);
  if (status == HAL_OK) {
    status = hal_command_response_write(response, request->arguments,
                                        request->arguments_length);
  }
  return status;
}

static bool expected_nested_send_status(hal_status_t status) {
  return status == HAL_OK || status == HAL_EAGAIN || status == HAL_EBUSY;
}

static void register_echo(hal_command_router_t router,
                          hal_command_security_flags_t required_security,
                          handler_observation_t *observation) {
  hal_command_definition_t definition = {};
  definition.name = "echo";
  definition.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_LORA_LINK);
  definition.required_security = required_security;
  definition.handler = echo_handler;
  definition.user = observation;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(router, &definition));
}

static hal_command_router_t create_router(void) {
  hal_command_router_t router = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&router));
  return router;
}

void setUp(void) {
  hal_mock_set_millis(0u);
  hal_mock_lora_reset();
}

void tearDown(void) { hal_mock_lora_reset(); }

void test_defaults_default_router_lifecycle_and_broadcast_rules(void) {
  endpoint_t endpoint = create_endpoint(1u, 91u, false, NULL, 0x43u, 3u);
  const hal_lora_commands_config_t defaults =
      hal_lora_commands_config_defaults(endpoint.link, 0x44u);
  TEST_ASSERT_EQUAL_PTR(endpoint.link, defaults.link);
  TEST_ASSERT_NULL(defaults.router);
  TEST_ASSERT_EQUAL_UINT8(0x44u, defaults.port);
  TEST_ASSERT_TRUE(defaults.acknowledged);
  TEST_ASSERT_EQUAL_UINT32(1u, defaults.initial_request_id);

  hal_lora_commands_config_t duplicate_config =
      hal_lora_commands_config_defaults(endpoint.link, 0x44u);
  hal_lora_commands_t duplicate = NULL;
  TEST_ASSERT_EQUAL_INT(
      HAL_EBUSY, hal_lora_commands_create(&duplicate_config, &duplicate));
  TEST_ASSERT_NULL(duplicate);

  uint32_t request_id = UINT32_MAX;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_lora_commands_request_start(
                            endpoint.commands, HAL_LORA_LINK_ADDRESS_BROADCAST,
                            "echo", HAL_COMMAND_ENCODING_BINARY, NULL, 0u,
                            &request_id));
  TEST_ASSERT_EQUAL_UINT32(0u, request_id);

  const uint8_t event_payload[] = {0xA5u};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_event_start(
                            endpoint.commands, HAL_LORA_LINK_ADDRESS_BROADCAST,
                            "presence", HAL_COMMAND_ENCODING_BINARY,
                            event_payload, sizeof(event_payload)));
  assert_process_status(hal_lora_commands_process(endpoint.commands));
  hal_lora_link_send_status_t send_status = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_link_get_send_status(endpoint.link, &send_status));
  TEST_ASSERT_EQUAL_INT(HAL_LORA_OPERATION_SUCCEEDED, send_status.state);

  const hal_lora_commands_t stale = endpoint.commands;
  destroy_endpoint(&endpoint);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_commands_destroy(stale));
}

void test_fragmented_binary_round_trip_and_lora_metadata(void) {
  hal_command_router_t router = create_router();
  handler_observation_t observation = {};
  register_echo(router, 0u, &observation);
  endpoint_t first = create_endpoint(1u, 101u, false, router, 0x43u, 77u);
  endpoint_t second = create_endpoint(2u, 202u, false, router, 0x43u, 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));

  uint8_t payload[500] = {};
  for (size_t index = 0u; index < sizeof(payload); ++index) {
    payload[index] = (uint8_t)(index * 7u + 3u);
  }
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "echo",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  TEST_ASSERT_EQUAL_UINT32(77u, request_id);
  pump(&first, &second, 80u);

  hal_command_message_t response = {};
  hal_lora_link_message_info_t response_info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_receive(
                                    first.commands, &response, &response_info));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, response.type);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_BINARY, response.encoding);
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), response.payload_length);
  TEST_ASSERT_EQUAL_MEMORY(payload, response.payload, sizeof(payload));
  TEST_ASSERT_EQUAL_UINT16(2u, response_info.source);
  TEST_ASSERT_EQUAL_UINT8(3u, response_info.fragment_count);

  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_LORA_LINK, observation.source);
  TEST_ASSERT_EQUAL_UINT32(0u, observation.security_flags);
  TEST_ASSERT_EQUAL_UINT64(1u, observation.peer_id);
  TEST_ASSERT_EQUAL_UINT64(101u, observation.session_id);
  TEST_ASSERT_EQUAL_UINT32(request_id, observation.request_id);
  TEST_ASSERT_EQUAL_UINT16(1u, observation.link_source);
  TEST_ASSERT_EQUAL_UINT8(3u, observation.link_fragments);
  TEST_ASSERT_FALSE(observation.link_encrypted);

  hal_lora_commands_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(first.commands, &info));
  TEST_ASSERT_EQUAL_UINT32(78u, info.next_request_id);
  hal_lora_commands_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_get_diagnostics(second.commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.requests_received);
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.responses_sent);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1u, diagnostics.pending_response_retries);

  destroy_endpoint(&first);
  destroy_endpoint(&second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_security_policy_rejects_plaintext_and_accepts_aead(void) {
  hal_command_router_t router = create_router();
  handler_observation_t observation = {};
  register_echo(router, HAL_COMMAND_SECURITY_ALL, &observation);

  endpoint_t first = create_endpoint(1u, 301u, false, router, 0x43u, 10u);
  endpoint_t second = create_endpoint(2u, 302u, false, router, 0x43u, 20u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  const uint8_t payload[] = {0x11u, 0x22u, 0x33u};
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "echo",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  pump(&first, &second, 40u);
  hal_command_message_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &response, NULL));
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, response.status);
  TEST_ASSERT_EQUAL_UINT32(0u, observation.calls);
  hal_lora_commands_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_get_diagnostics(second.commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.dispatch_failures);
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, diagnostics.last_dispatch_status);
  destroy_endpoint(&first);
  destroy_endpoint(&second);

  hal_mock_lora_reset();
  hal_mock_set_millis(0u);
  first = create_endpoint(1u, 401u, true, router, 0x43u, 30u);
  second = create_endpoint(2u, 402u, true, router, 0x43u, 40u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "echo",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  pump(&first, &second, 40u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &response, NULL));
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_MEMORY(payload, response.payload, sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);
  TEST_ASSERT_EQUAL_UINT32(HAL_COMMAND_SECURITY_ALL,
                           observation.security_flags);
  TEST_ASSERT_TRUE(observation.link_encrypted);

  destroy_endpoint(&first);
  destroy_endpoint(&second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_unknown_command_correlation_wrap_and_event_receive(void) {
  hal_command_router_t router = create_router();
  endpoint_t first =
      create_endpoint(1u, 501u, false, router, 0x43u, UINT32_MAX);
  endpoint_t second = create_endpoint(2u, 502u, false, router, 0x43u, 5u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));

  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_request_start(first.commands, 2u, "missing",
                                              HAL_COMMAND_ENCODING_TEXT, NULL,
                                              0u, &request_id));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, request_id);
  pump(&first, &second, 40u);

  hal_command_message_t message = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &message, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, message.type);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, message.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, message.status);
  hal_lora_commands_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(first.commands, &info));
  TEST_ASSERT_EQUAL_UINT32(1u, info.next_request_id);

  const char event_payload[] = "ready";
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_event_start(
                                    second.commands, 1u, "state",
                                    HAL_COMMAND_ENCODING_TEXT, event_payload,
                                    sizeof(event_payload) - 1u));
  pump(&first, &second, 30u);
  hal_lora_link_message_info_t event_info = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &message, &event_info));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_EVENT, message.type);
  TEST_ASSERT_EQUAL_STRING("state", message.name);
  TEST_ASSERT_EQUAL_UINT32(0u, message.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_NONE, message.status);
  TEST_ASSERT_EQUAL_UINT(sizeof(event_payload) - 1u, message.payload_length);
  TEST_ASSERT_EQUAL_MEMORY(event_payload, message.payload,
                           sizeof(event_payload) - 1u);
  TEST_ASSERT_EQUAL_UINT16(2u, event_info.source);
  TEST_ASSERT_EQUAL_INT(
      HAL_EAGAIN, hal_lora_commands_receive(first.commands, &message, NULL));

  destroy_endpoint(&first);
  destroy_endpoint(&second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_handler_can_reenter_adapter_without_deadlock_or_buffer_corruption(
    void) {
  hal_command_router_t router = create_router();
  reentrant_observation_t observation = {};
  endpoint_t first = create_endpoint(1u, 551u, false, router, 0x43u, 70u);
  endpoint_t second = create_endpoint(2u, 552u, false, router, 0x43u, 80u);
  observation.commands = second.commands;

  hal_command_definition_t definition = {};
  definition.name = "reentrant";
  definition.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_LORA_LINK);
  definition.handler = reentrant_handler;
  definition.user = &observation;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(router, &definition));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));

  const uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "reentrant",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  pump(&first, &second, 50u);

  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);
  TEST_ASSERT_EQUAL_INT(HAL_OK, observation.info_status);
  TEST_ASSERT_TRUE(observation.process_active);
  TEST_ASSERT_TRUE(observation.dispatch_active);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, observation.process_status);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, observation.destroy_status);
  TEST_ASSERT_TRUE(expected_nested_send_status(observation.event_status));
  TEST_ASSERT_TRUE(expected_nested_send_status(observation.request_status));

  hal_command_message_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &response, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_MESSAGE_RESPONSE, response.type);
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_UINT(sizeof(payload), response.payload_length);
  TEST_ASSERT_EQUAL_MEMORY(payload, response.payload, sizeof(payload));

  destroy_endpoint(&first);
  destroy_endpoint(&second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_pending_response_retry_reports_progress_after_stale_eagain(void) {
  hal_command_router_t router = create_router();
  handler_observation_t observation = {};
  register_echo(router, 0u, &observation);
  endpoint_t first =
      create_endpoint_with_ack(1u, 571u, false, router, 0x43u, 90u, false);
  endpoint_t second =
      create_endpoint_with_ack(2u, 572u, false, router, 0x43u, 100u, false);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_set_next_status(
                            second.radio, HAL_MOCK_LORA_TRANSMIT, HAL_EAGAIN));

  const uint8_t payload[] = {0x77u};
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "echo",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  assert_process_status(hal_lora_commands_process(first.commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_process(second.commands));

  hal_lora_commands_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(second.commands, &info));
  TEST_ASSERT_TRUE(info.pending_response);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_RECEIVING, info.link_state);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_process(second.commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(second.commands, &info));
  TEST_ASSERT_FALSE(info.pending_response);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_TRANSMITTING, info.link_state);

  pump(&first, &second, 12u);
  hal_command_message_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &response, NULL));
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);

  destroy_endpoint(&first);
  destroy_endpoint(&second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_process_preserves_hard_link_error(void) {
  endpoint_t endpoint = create_endpoint(1u, 575u, false, NULL, 0x43u, 105u);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_mock_lora_set_next_status(
                  endpoint.radio, HAL_MOCK_LORA_RECEIVE_POLL, HAL_EIO));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_lora_commands_process(endpoint.commands));

  hal_lora_commands_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_get_diagnostics(
                                    endpoint.commands, &diagnostics));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, diagnostics.last_error);
  destroy_endpoint(&endpoint);
}

void test_concurrent_dispatch_keeps_handle_alive_and_stale_handle_cannot_alias(
    void) {
  hal_command_router_t router = create_router();
  blocking_observation_t observation = {};
  hal_command_definition_t definition = {};
  definition.name = "blocking";
  definition.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_LORA_LINK);
  definition.handler = blocking_handler;
  definition.user = &observation;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(router, &definition));

  endpoint_t first = create_endpoint(1u, 581u, false, router, 0x43u, 110u);
  endpoint_t second = create_endpoint(2u, 582u, false, router, 0x43u, 120u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));

  const uint8_t payload[] = {0xD1u, 0xD2u};
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "blocking",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  assert_process_status(hal_lora_commands_process(first.commands));

  hal_status_t worker_status = HAL_NONE;
  std::thread worker(
      [&] { worker_status = hal_lora_commands_process(second.commands); });

  bool entered = false;
  {
    std::unique_lock<std::mutex> lock(observation.mutex);
    entered = observation.condition.wait_for(
        lock, std::chrono::seconds(2),
        [&observation] { return observation.entered; });
  }
  if (!entered) {
    {
      std::lock_guard<std::mutex> lock(observation.mutex);
      observation.release = true;
    }
    observation.condition.notify_all();
    worker.join();
    TEST_FAIL_MESSAGE("blocking handler did not start");
    return;
  }

  hal_lora_commands_info_t info = {};
  const hal_status_t info_status =
      hal_lora_commands_get_info(second.commands, &info);
  const hal_status_t nested_process_status =
      hal_lora_commands_process(second.commands);
  const hal_status_t destroy_status =
      hal_lora_commands_destroy(second.commands);

  {
    std::lock_guard<std::mutex> lock(observation.mutex);
    observation.release = true;
  }
  observation.condition.notify_all();
  worker.join();

  TEST_ASSERT_EQUAL_INT(HAL_OK, info_status);
  TEST_ASSERT_TRUE(info.process_active);
  TEST_ASSERT_TRUE(info.dispatch_active);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, nested_process_status);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, destroy_status);
  assert_process_status(worker_status);
  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);

  pump(&first, &second, 30u);
  hal_command_message_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &response, NULL));
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_MEMORY(payload, response.payload, sizeof(payload));

  const hal_lora_commands_t stale_first = first.commands;
  const hal_lora_commands_t stale_second = second.commands;
  destroy_endpoint(&first);
  destroy_endpoint(&second);

  hal_mock_lora_reset();
  hal_mock_set_millis(0u);
  endpoint_t replacement =
      create_endpoint(3u, 583u, false, router, 0x43u, 130u);
  TEST_ASSERT_NOT_EQUAL(stale_first, replacement.commands);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_lora_commands_get_info(stale_first, &info));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_lora_commands_destroy(stale_second));

  destroy_endpoint(&replacement);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_response_waits_for_request_transport_ack(void) {
  hal_command_router_t router = create_router();
  handler_observation_t observation = {};
  register_echo(router, 0u, &observation);
  endpoint_t first = create_endpoint(1u, 601u, false, router, 0x43u, 9u);
  endpoint_t second = create_endpoint(2u, 602u, false, router, 0x43u, 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_lora_connect(first.radio, second.radio));

  const uint8_t payload[] = {0x5Au};
  uint32_t request_id = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_lora_commands_request_start(
                                    first.commands, 2u, "echo",
                                    HAL_COMMAND_ENCODING_BINARY, payload,
                                    sizeof(payload), &request_id));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_lora_commands_destroy(first.commands));
  assert_process_status(hal_lora_commands_process(first.commands));
  assert_process_status(hal_lora_commands_process(second.commands));

  hal_lora_commands_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(second.commands, &info));
  TEST_ASSERT_TRUE(info.pending_response);
  TEST_ASSERT_EQUAL_UINT16(1u, info.pending_response_destination);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_SENDING_ACKNOWLEDGEMENT,
                        info.link_state);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_lora_commands_destroy(second.commands));
  hal_lora_commands_diagnostics_t diagnostics = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_get_diagnostics(second.commands, &diagnostics));
  TEST_ASSERT_EQUAL_UINT32(1u, diagnostics.pending_response_retries);
  TEST_ASSERT_EQUAL_UINT32(0u, diagnostics.responses_sent);

  assert_process_status(hal_lora_commands_process(first.commands));
  assert_process_status(hal_lora_commands_process(second.commands));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(second.commands, &info));
  TEST_ASSERT_FALSE(info.pending_response);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_TRANSMITTING, info.link_state);
  pump(&first, &second, 20u);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_lora_commands_get_info(first.commands, &info));
  TEST_ASSERT_TRUE(info.receive_ready);
  TEST_ASSERT_EQUAL_INT(HAL_LORA_LINK_STATE_RECEIVING, info.link_state);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_lora_commands_destroy(first.commands));

  hal_command_message_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_lora_commands_receive(first.commands, &response, NULL));
  TEST_ASSERT_EQUAL_UINT32(request_id, response.request_id);
  TEST_ASSERT_EQUAL_INT(HAL_OK, response.status);
  TEST_ASSERT_EQUAL_UINT32(1u, observation.calls);

  destroy_endpoint(&first);
  destroy_endpoint(&second);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_default_router_lifecycle_and_broadcast_rules);
  RUN_TEST(test_fragmented_binary_round_trip_and_lora_metadata);
  RUN_TEST(test_security_policy_rejects_plaintext_and_accepts_aead);
  RUN_TEST(test_unknown_command_correlation_wrap_and_event_receive);
  RUN_TEST(
      test_handler_can_reenter_adapter_without_deadlock_or_buffer_corruption);
  RUN_TEST(test_pending_response_retry_reports_progress_after_stale_eagain);
  RUN_TEST(test_process_preserves_hard_link_error);
  RUN_TEST(
      test_concurrent_dispatch_keeps_handle_alive_and_stale_handle_cannot_alias);
  RUN_TEST(test_response_waits_for_request_transport_ack);
  return UNITY_END();
}
