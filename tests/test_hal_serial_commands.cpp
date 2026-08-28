#include "hal/commands/hal_command_router.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/serial/hal_serial_commands.h"
#include "hal/serial/hal_serial_frame.h"
#include "support/serial_session_test_helpers.h"
#include "utils/unity.h"

#include <stdio.h>
#include <string.h>

namespace {

const hal_serial_session_vocabulary_t kVocabulary = {
    .reply_unknown_cmd = "SC_UNKNOWN_CMD",
    .reply_not_ready_hello_required = "SC_NOT_READY HELLO_REQUIRED",
};

struct handler_observation_t {
  uint32_t calls;
  hal_command_source_t source;
  hal_command_encoding_t encoding;
  hal_command_security_flags_t security_flags;
  uint32_t request_id;
  uint64_t peer_id;
  uint64_t session_id;
  const void *source_context;
  char command[HAL_COMMAND_ROUTER_NAME_MAX];
  char arguments[HAL_SERIAL_SESSION_MAX_LINE + 1u];
  const char *body;
  hal_command_encoding_t response_encoding;
};

struct formatter_observation_t {
  uint32_t calls;
  hal_status_t response_status;
  uint32_t request_id;
  const char *payload;
};

struct fallback_observation_t {
  hal_serial_session_t *session;
  uint32_t calls;
  char line[HAL_SERIAL_SESSION_MAX_LINE + 1u];
  hal_status_t deinit_status;
};

struct inactive_observation_t {
  uint32_t calls;
  uint32_t request_id;
  uint64_t session_id;
  char command[HAL_COMMAND_ROUTER_NAME_MAX];
  char arguments[HAL_SERIAL_SESSION_MAX_LINE + 1u];
  size_t arguments_length;
  hal_status_t deinit_status;
  bool try_deinit;
};

hal_serial_session_t s_session;
hal_serial_commands_t s_commands;
hal_command_router_t s_router = nullptr;
handler_observation_t s_handler;

hal_status_t observe_handler(const hal_command_request_t *request,
                             hal_command_response_t *response, void *user) {
  auto *observation = static_cast<handler_observation_t *>(user);
  ++observation->calls;
  observation->source = request->source;
  observation->encoding = request->encoding;
  observation->security_flags = request->security_flags;
  observation->request_id = request->request_id;
  observation->peer_id = request->peer_id;
  observation->session_id = request->session_id;
  observation->source_context = request->source_context;
  snprintf(observation->command, sizeof(observation->command), "%s",
           request->command);
  const size_t copy_length =
      request->arguments_length < sizeof(observation->arguments) - 1u
          ? request->arguments_length
          : sizeof(observation->arguments) - 1u;
  if (copy_length > 0u) {
    memcpy(observation->arguments, request->arguments, copy_length);
  }
  observation->arguments[copy_length] = '\0';

  hal_status_t status = hal_command_response_set_encoding(
      response, observation->response_encoding);
  if (status == HAL_OK && observation->body != nullptr) {
    status = hal_command_response_write_str(response, observation->body);
  }
  return status;
}

hal_status_t legacy_formatter(const hal_command_request_t *request,
                              const hal_command_response_t *response,
                              char *output, size_t output_capacity,
                              size_t *out_length, void *user) {
  auto *observation = static_cast<formatter_observation_t *>(user);
  ++observation->calls;
  observation->response_status = response->status;
  observation->request_id = request->request_id;
  const size_t length = strlen(observation->payload);
  if (length > output_capacity) {
    return HAL_EOVERFLOW;
  }
  memcpy(output, observation->payload, length);
  *out_length = length;
  return HAL_OK;
}

void fallback_handler(const char *line, void *user) {
  auto *observation = static_cast<fallback_observation_t *>(user);
  ++observation->calls;
  snprintf(observation->line, sizeof(observation->line), "%s", line);
  hal_serial_session_println(observation->session, "PID_OK");
}

void reentrant_fallback_handler(const char *line, void *user) {
  fallback_handler(line, user);
  auto *observation = static_cast<fallback_observation_t *>(user);
  observation->deinit_status = hal_serial_commands_deinit(&s_commands);
}

hal_status_t reentrant_handler(const hal_command_request_t *request,
                               hal_command_response_t *response, void *user) {
  auto *status = static_cast<hal_status_t *>(user);
  *status = hal_serial_commands_deinit(&s_commands);
  hal_status_t result =
      hal_command_response_set_encoding(response, request->encoding);
  if (result == HAL_OK) {
    result = hal_command_response_write_str(response, "SC_OK REENTRANT");
  }
  return result;
}

hal_status_t reentrant_formatter(const hal_command_request_t *request,
                                 const hal_command_response_t *response,
                                 char *output, size_t output_capacity,
                                 size_t *out_length, void *user) {
  (void)request;
  (void)response;
  auto *status = static_cast<hal_status_t *>(user);
  *status = hal_serial_commands_deinit(&s_commands);
  static const char kPayload[] = "SC_UNKNOWN_CMD";
  if (sizeof(kPayload) - 1u > output_capacity) {
    return HAL_EOVERFLOW;
  }
  memcpy(output, kPayload, sizeof(kPayload) - 1u);
  *out_length = sizeof(kPayload) - 1u;
  return HAL_OK;
}

void foreign_handler(const char *line, void *user) {
  (void)line;
  (void)user;
}

bool allow_reboot_inactive(const hal_command_request_t *request, void *user) {
  auto *observation = static_cast<inactive_observation_t *>(user);
  ++observation->calls;
  observation->request_id = request->request_id;
  observation->session_id = request->session_id;
  snprintf(observation->command, sizeof(observation->command), "%s",
           request->command);
  observation->arguments_length = request->arguments_length;
  const size_t copy_length =
      request->arguments_length < sizeof(observation->arguments) - 1u
          ? request->arguments_length
          : sizeof(observation->arguments) - 1u;
  if (copy_length > 0u) {
    memcpy(observation->arguments, request->arguments, copy_length);
  }
  observation->arguments[copy_length] = '\0';
  if (observation->try_deinit) {
    observation->deinit_status = hal_serial_commands_deinit(&s_commands);
  }
  return strcmp(request->command, "SC_REBOOT_BOOTLOADER") == 0 &&
         request->arguments_length == 0u;
}

void register_command(const char *name) {
  const hal_command_definition_t definition = {
      .name = name,
      .allowed_sources =
          HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION),
      .required_security = 0u,
      .handler = observe_handler,
      .user = &s_handler,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_router, &definition));
}

void activate_session(void) {
  inject_framed_line(1u, "HELLO", '\n');
  hal_serial_session_poll(&s_session);
  TEST_ASSERT_TRUE(hal_serial_session_is_active(&s_session));
  hal_mock_serial_reset();
}

void attach_adapter(const hal_serial_commands_config_t *custom = nullptr) {
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  if (custom != nullptr) {
    config = *custom;
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_serial_commands_init(&s_commands, &config));
}

void send_payload(uint16_t sequence, const char *payload) {
  inject_framed_line(sequence, payload, '\n');
  hal_serial_session_poll(&s_session);
}

void assert_reply(uint16_t expected_sequence, const char *expected_payload) {
  uint16_t sequence = 0u;
  char payload[HAL_SERIAL_FRAME_PAYLOAD_MAX + 1u] = {};
  TEST_ASSERT_TRUE(
      decode_last_framed_reply(&sequence, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT16(expected_sequence, sequence);
  TEST_ASSERT_EQUAL_STRING(expected_payload, payload);
}

} // namespace

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_set_millis(17u);
  memset(&s_session, 0, sizeof(s_session));
  memset(&s_commands, 0, sizeof(s_commands));
  memset(&s_handler, 0, sizeof(s_handler));
  s_handler.response_encoding = HAL_COMMAND_ENCODING_TEXT;
  hal_serial_session_init_with_vocabulary(&s_session, "TEST", "1.0", "dev",
                                          &kVocabulary);
  s_router = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&s_router));
}

void tearDown(void) {
  if (s_commands.initialized) {
    const hal_status_t status = hal_serial_commands_deinit(&s_commands);
    if (status != HAL_OK) {
      hal_serial_session_set_unknown_handler(&s_session, nullptr, nullptr);
      memset(&s_commands, 0, sizeof(s_commands));
    }
  }
  if (s_router != nullptr) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(s_router));
    s_router = nullptr;
  }
}

void test_defaults_validation_and_safe_callback_ownership(void) {
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  TEST_ASSERT_EQUAL_PTR(&s_session, config.session);
  TEST_ASSERT_NULL(config.router);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_TEXT, config.encoding);
  TEST_ASSERT_NULL(config.command_prefix);
  TEST_ASSERT_NULL(config.formatter);
  TEST_ASSERT_NULL(config.allow_inactive);
  TEST_ASSERT_NULL(config.fallback);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_serial_commands_init(nullptr, &config));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_serial_commands_init(&s_commands, nullptr));
  config.encoding = HAL_COMMAND_ENCODING_BINARY;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_serial_commands_init(&s_commands, &config));

  config.encoding = HAL_COMMAND_ENCODING_TEXT;
  config.router = s_router;
  hal_serial_session_set_unknown_handler(&s_session, foreign_handler,
                                         &s_handler);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_serial_commands_init(&s_commands, &config));
  TEST_ASSERT_EQUAL_PTR(foreign_handler, s_session.unknown_handler);

  hal_serial_session_set_unknown_handler(&s_session, nullptr, nullptr);
  attach_adapter(&config);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_serial_commands_init(&s_commands, &config));
  hal_serial_session_set_unknown_handler(&s_session, foreign_handler,
                                         &s_handler);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_serial_commands_deinit(&s_commands));
  TEST_ASSERT_EQUAL_PTR(foreign_handler, s_session.unknown_handler);
}

void test_matching_command_requires_hello_and_echoes_sequence(void) {
  register_command("SC_ECHO");
  attach_adapter();

  send_payload(9u, "SC_ECHO value");

  TEST_ASSERT_EQUAL_UINT32(0u, s_handler.calls);
  assert_reply(9u, "SC_NOT_READY HELLO_REQUIRED");
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, last);
}

void test_selected_inactive_command_reaches_router_security_policy(void) {
  const hal_command_definition_t definition = {
      .name = "SC_REBOOT_BOOTLOADER",
      .allowed_sources =
          HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION),
      .required_security = HAL_COMMAND_SECURITY_AUTHENTICATED,
      .handler = observe_handler,
      .user = &s_handler,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_router, &definition));
  formatter_observation_t formatter = {
      .payload = "SC_NOT_AUTHORIZED",
  };
  inactive_observation_t inactive = {
      .deinit_status = HAL_NONE,
      .try_deinit = true,
  };
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  config.command_prefix = "SC_";
  config.formatter = legacy_formatter;
  config.formatter_user = &formatter;
  config.allow_inactive = allow_reboot_inactive;
  config.allow_inactive_user = &inactive;
  attach_adapter(&config);

  send_payload(10u, "SC_REBOOT_BOOTLOADER");

  TEST_ASSERT_EQUAL_UINT32(1u, inactive.calls);
  TEST_ASSERT_EQUAL_UINT32(10u, inactive.request_id);
  TEST_ASSERT_EQUAL_UINT64(0u, inactive.session_id);
  TEST_ASSERT_EQUAL_STRING("SC_REBOOT_BOOTLOADER", inactive.command);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, inactive.deinit_status);
  TEST_ASSERT_EQUAL_UINT32(0u, s_handler.calls);
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, formatter.response_status);
  assert_reply(10u, "SC_NOT_AUTHORIZED");
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, last);

  inactive.try_deinit = false;
  send_payload(11u, "SC_REBOOT_BOOTLOADER \t");
  TEST_ASSERT_EQUAL_UINT32(2u, inactive.calls);
  assert_reply(11u, "SC_NOT_READY HELLO_REQUIRED");
  TEST_ASSERT_EQUAL_STRING("SC_REBOOT_BOOTLOADER", inactive.command);
  TEST_ASSERT_EQUAL_UINT32(2u, inactive.arguments_length);
  TEST_ASSERT_EQUAL_STRING(" \t", inactive.arguments);

  send_payload(12u, "SC_OTHER");
  TEST_ASSERT_EQUAL_UINT32(3u, inactive.calls);
  assert_reply(12u, "SC_NOT_READY HELLO_REQUIRED");
}

void test_dispatches_name_arguments_and_serial_session_metadata(void) {
  register_command("SC_ECHO");
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  config.peer_id = UINT64_C(0x1122334455667788);
  config.encoding = HAL_COMMAND_ENCODING_JSON;
  config.command_prefix = "SC_";
  attach_adapter(&config);
  activate_session();
#ifdef HAL_ENABLE_CRYPTO
  s_session.authenticated = true;
#endif
  s_handler.body = "SC_OK ECHO";

  send_payload(77u, "SC_ECHO   {\"ready\":true}");

  TEST_ASSERT_EQUAL_UINT32(1u, s_handler.calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_SERIAL_SESSION, s_handler.source);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_ENCODING_JSON, s_handler.encoding);
  const hal_command_security_flags_t expected_security =
      hal_serial_session_is_authenticated(&s_session)
          ? HAL_COMMAND_SECURITY_AUTHENTICATED
          : 0u;
  TEST_ASSERT_EQUAL_UINT32(expected_security, s_handler.security_flags);
  TEST_ASSERT_EQUAL_UINT32(77u, s_handler.request_id);
  TEST_ASSERT_EQUAL_UINT64(config.peer_id, s_handler.peer_id);
  TEST_ASSERT_EQUAL_UINT64(hal_serial_session_id(&s_session),
                           s_handler.session_id);
  TEST_ASSERT_EQUAL_PTR(&s_session, s_handler.source_context);
  TEST_ASSERT_EQUAL_STRING("SC_ECHO", s_handler.command);
  TEST_ASSERT_EQUAL_STRING("{\"ready\":true}", s_handler.arguments);
  assert_reply(77u, "SC_OK ECHO");
}

void test_text_handler_body_is_returned_verbatim(void) {
  register_command("SC_GET_META");
  attach_adapter();
  activate_session();
  s_handler.body = "SC_OK META module=ECU";

  send_payload(42u, "SC_GET_META");

  assert_reply(42u, s_handler.body);
  TEST_ASSERT_EQUAL_STRING("", s_handler.arguments);
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_OK, last);
}

void test_formatter_maps_empty_router_error_without_fiesta_tokens_in_adapter(
    void) {
  formatter_observation_t formatter = {
      .payload = "SC_UNKNOWN_CMD",
  };
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  config.command_prefix = "SC_";
  config.formatter = legacy_formatter;
  config.formatter_user = &formatter;
  attach_adapter(&config);
  activate_session();

  send_payload(91u, "SC_MISSING argument");

  TEST_ASSERT_EQUAL_UINT32(1u, formatter.calls);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, formatter.response_status);
  TEST_ASSERT_EQUAL_UINT32(91u, formatter.request_id);
  assert_reply(91u, formatter.payload);
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, last);
}

void test_prefix_routes_non_command_payload_to_fallback(void) {
  register_command("SC_ECHO");
  fallback_observation_t fallback = {
      .session = &s_session,
  };
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  config.command_prefix = "SC_";
  config.fallback = fallback_handler;
  config.fallback_user = &fallback;
  attach_adapter(&config);
  activate_session();

  send_payload(13u, "pid 1200 33");

  TEST_ASSERT_EQUAL_UINT32(1u, fallback.calls);
  TEST_ASSERT_EQUAL_STRING("pid 1200 33", fallback.line);
  TEST_ASSERT_EQUAL_UINT32(0u, s_handler.calls);
  assert_reply(13u, "PID_OK");
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_IGNORED, last);
}

void test_unmatched_prefix_without_fallback_uses_session_vocabulary(void) {
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  config.command_prefix = "SC_";
  attach_adapter(&config);
  activate_session();

  send_payload(14u, "pid 1000");

  assert_reply(14u, "SC_UNKNOWN_CMD");
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, last);
}

void test_default_empty_response_and_unknown_error_are_neutral_text(void) {
  register_command("SC_EMPTY");
  attach_adapter();
  activate_session();

  send_payload(20u, "SC_EMPTY");
  assert_reply(20u, "OK");

  send_payload(21u, "SC_UNKNOWN");
  assert_reply(21u, "ERR HAL_ENOENT");
}

void test_forbidden_and_oversized_handler_bodies_fail_safely(void) {
  register_command("SC_BODY");
  attach_adapter();
  activate_session();
  s_handler.body = "SC_BAD*PAYLOAD";

  send_payload(30u, "SC_BODY");
  assert_reply(30u, "ERR HAL_EPROTO");
  hal_status_t last = HAL_NONE;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, last);

  static char oversized[HAL_SERIAL_FRAME_PAYLOAD_MAX + 2u];
  memset(oversized, 'A', sizeof(oversized) - 1u);
  oversized[sizeof(oversized) - 1u] = '\0';
  s_handler.body = oversized;
  send_payload(31u, "SC_BODY");
  assert_reply(31u, "ERR HAL_EOVERFLOW");
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, last);

  s_handler.response_encoding = HAL_COMMAND_ENCODING_BINARY;
  s_handler.body = "binary";
  send_payload(32u, "SC_BODY");
  assert_reply(32u, "ERR HAL_EUNSUPPORTED");
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_serial_commands_get_last_status(&s_commands, &last));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, last);
}

void test_request_sequence_status_api_is_scoped_to_dispatch(void) {
  uint16_t sequence = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_serial_session_current_request_seq(
                                        &s_session, &sequence));
  TEST_ASSERT_EQUAL_UINT16(0u, sequence);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_serial_session_current_request_seq(nullptr, &sequence));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_serial_session_current_request_seq(&s_session, nullptr));
}

void test_status_first_session_callback_ownership_and_print_validation(void) {
  hal_serial_session_t uninitialized = {};
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_serial_session_attach_unknown_handler(
                            &uninitialized, foreign_handler, &s_handler));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_serial_session_attach_unknown_handler(
                                    &s_session, foreign_handler, &s_handler));
  TEST_ASSERT_EQUAL_INT(HAL_EEXIST,
                        hal_serial_session_attach_unknown_handler(
                            &s_session, foreign_handler, &s_handler));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_serial_session_detach_unknown_handler(
                            &s_session, foreign_handler, &s_session));
  TEST_ASSERT_EQUAL_PTR(foreign_handler, s_session.unknown_handler);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_serial_session_detach_unknown_handler(
                                    &s_session, foreign_handler, &s_handler));
  TEST_ASSERT_NULL(s_session.unknown_handler);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_serial_session_detach_unknown_handler(
                            &s_session, foreign_handler, &s_handler));

  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_serial_session_println_ex(&s_session, "OK"));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_serial_session_println_ex(nullptr, "OK"));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_serial_session_println_ex(&s_session, nullptr));
}

void test_deinit_is_busy_from_handler_formatter_and_fallback(void) {
  hal_status_t handler_deinit = HAL_NONE;
  const hal_command_definition_t definition = {
      .name = "SC_REENTRANT",
      .allowed_sources =
          HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION),
      .required_security = 0u,
      .handler = reentrant_handler,
      .user = &handler_deinit,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_router, &definition));
  hal_status_t formatter_deinit = HAL_NONE;
  fallback_observation_t fallback = {
      .session = &s_session,
      .deinit_status = HAL_NONE,
  };
  hal_serial_commands_config_t config =
      hal_serial_commands_config_defaults(&s_session);
  config.router = s_router;
  config.command_prefix = "SC_";
  config.formatter = reentrant_formatter;
  config.formatter_user = &formatter_deinit;
  config.fallback = reentrant_fallback_handler;
  config.fallback_user = &fallback;
  attach_adapter(&config);
  activate_session();

  send_payload(60u, "SC_REENTRANT");
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, handler_deinit);
  assert_reply(60u, "SC_OK REENTRANT");

  send_payload(61u, "SC_MISSING");
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, formatter_deinit);
  assert_reply(61u, "SC_UNKNOWN_CMD");

  send_payload(62u, "pid 1 2");
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, fallback.deinit_status);
  assert_reply(62u, "PID_OK");
  TEST_ASSERT_TRUE(s_commands.initialized);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_validation_and_safe_callback_ownership);
  RUN_TEST(test_matching_command_requires_hello_and_echoes_sequence);
  RUN_TEST(test_selected_inactive_command_reaches_router_security_policy);
  RUN_TEST(test_dispatches_name_arguments_and_serial_session_metadata);
  RUN_TEST(test_text_handler_body_is_returned_verbatim);
  RUN_TEST(
      test_formatter_maps_empty_router_error_without_fiesta_tokens_in_adapter);
  RUN_TEST(test_prefix_routes_non_command_payload_to_fallback);
  RUN_TEST(test_unmatched_prefix_without_fallback_uses_session_vocabulary);
  RUN_TEST(test_default_empty_response_and_unknown_error_are_neutral_text);
  RUN_TEST(test_forbidden_and_oversized_handler_bodies_fail_safely);
  RUN_TEST(test_request_sequence_status_api_is_scoped_to_dispatch);
  RUN_TEST(test_status_first_session_callback_ownership_and_print_validation);
  RUN_TEST(test_deinit_is_busy_from_handler_formatter_and_fallback);
  return UNITY_END();
}
