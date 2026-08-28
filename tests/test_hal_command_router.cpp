#include "hal/commands/hal_command_router.h"
#include "utils/unity.h"

#include <atomic>
#include <stdio.h>
#include <string.h>
#include <thread>

namespace {

hal_command_router_t s_default_router = NULL;
uint32_t s_calls = 0u;
hal_command_request_t s_last_request{};
uint8_t s_arguments[32]{};
hal_status_t s_reentrant_status = HAL_NONE;
hal_status_t s_owned_reentrant_status = HAL_NONE;
uint8_t s_user_marker = 0u;
std::atomic<bool> s_blocking_handler_entered{false};
std::atomic<bool> s_blocking_handler_may_return{false};

hal_command_definition_t
definition(const char *name, hal_command_handler_t handler,
           hal_command_source_mask_t sources = HAL_COMMAND_SOURCE_MASK_ALL,
           hal_command_security_flags_t security = 0u) {
  hal_command_definition_t value{};
  value.name = name;
  value.allowed_sources = sources;
  value.required_security = security;
  value.handler = handler;
  return value;
}

hal_command_request_t
request(const char *name,
        hal_command_source_t source = HAL_COMMAND_SOURCE_DIRECT) {
  hal_command_request_t value{};
  value.source = source;
  value.encoding = HAL_COMMAND_ENCODING_BINARY;
  value.command = name;
  return value;
}

hal_status_t capture_handler(const hal_command_request_t *command_request,
                             hal_command_response_t *response, void *user) {
  ++s_calls;
  s_last_request = *command_request;
  TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(s_arguments),
                                 command_request->arguments_length);
  if (command_request->arguments_length > 0u) {
    memcpy(s_arguments, command_request->arguments,
           command_request->arguments_length);
    s_last_request.arguments = s_arguments;
  }
  if (user != NULL) {
    TEST_ASSERT_EQUAL_PTR(&s_user_marker, user);
  }
  const uint8_t reply[] = {0x41u, 0x00u, 0x42u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_response_set_encoding(
                                    response, HAL_COMMAND_ENCODING_BINARY));
  return hal_command_response_write(response, reply, sizeof(reply));
}

hal_status_t denied_handler(const hal_command_request_t *,
                            hal_command_response_t *, void *) {
  TEST_FAIL_MESSAGE("policy-rejected handler was invoked");
  return HAL_EINTERNAL;
}

hal_status_t response_status_handler(const hal_command_request_t *,
                                     hal_command_response_t *response, void *) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_response_set_status(response, HAL_EPERM, "denied"));
  return HAL_OK;
}

hal_status_t reentrant_handler(const hal_command_request_t *command_request,
                               hal_command_response_t *, void *user) {
  hal_command_router_t router = (hal_command_router_t)user;
  s_reentrant_status =
      hal_command_router_unregister(router, command_request->command);
  return HAL_OK;
}

hal_status_t
owned_reentrant_handler(const hal_command_request_t *command_request,
                        hal_command_response_t *, void *user) {
  hal_command_router_t router = (hal_command_router_t)user;
  s_owned_reentrant_status = hal_command_router_unregister_if_matches(
      router, command_request->command, owned_reentrant_handler, user);
  return HAL_OK;
}

hal_status_t blocking_handler(const hal_command_request_t *,
                              hal_command_response_t *, void *) {
  s_blocking_handler_entered.store(true, std::memory_order_release);
  while (!s_blocking_handler_may_return.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  return HAL_OK;
}

} // namespace

void setUp(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_default(&s_default_router));
  TEST_ASSERT_NOT_NULL(s_default_router);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_clear(s_default_router));
  s_calls = 0u;
  memset(&s_last_request, 0, sizeof(s_last_request));
  memset(s_arguments, 0, sizeof(s_arguments));
  s_reentrant_status = HAL_NONE;
  s_owned_reentrant_status = HAL_NONE;
  s_blocking_handler_entered.store(false, std::memory_order_relaxed);
  s_blocking_handler_may_return.store(false, std::memory_order_relaxed);
}

void tearDown(void) {
  if (s_default_router != NULL) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_clear(s_default_router));
  }
}

void test_dispatch_preserves_binary_arguments_and_metadata(void) {
  hal_command_definition_t route = definition("echo", capture_handler);
  route.user = &s_user_marker;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_default_router, &route));

  const uint8_t arguments[] = {0x00u, 0x3au, 0x3bu, 0xffu};
  hal_command_request_t command_request =
      request("echo", HAL_COMMAND_SOURCE_BLE_STREAM);
  command_request.arguments = arguments;
  command_request.arguments_length = sizeof(arguments);
  command_request.request_id = 0x10203040u;
  command_request.peer_id = UINT64_C(0x1122334455667788);
  command_request.session_id = UINT64_C(0x8877665544332211);
  command_request.security_flags = HAL_COMMAND_SECURITY_ALL;
  command_request.source_context = &route;

  hal_command_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_dispatch(s_default_router,
                                                            &command_request,
                                                            &response));
  TEST_ASSERT_EQUAL_UINT32(1u, s_calls);
  TEST_ASSERT_EQUAL_INT(HAL_COMMAND_SOURCE_BLE_STREAM, s_last_request.source);
  TEST_ASSERT_EQUAL_UINT32(command_request.request_id,
                           s_last_request.request_id);
  TEST_ASSERT_EQUAL_UINT64(command_request.peer_id, s_last_request.peer_id);
  TEST_ASSERT_EQUAL_UINT64(command_request.session_id,
                           s_last_request.session_id);
  TEST_ASSERT_EQUAL_PTR(&route, s_last_request.source_context);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(arguments, s_last_request.arguments,
                                sizeof(arguments));
  const uint8_t expected[] = {0x41u, 0x00u, 0x42u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, response.body, sizeof(expected));
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), response.body_len);
}

void test_source_and_security_policies_reject_before_handler(void) {
  hal_command_definition_t route =
      definition("secure", denied_handler,
                 HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_LORA_LINK) |
                     HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM),
                 HAL_COMMAND_SECURITY_AUTHENTICATED |
                     HAL_COMMAND_SECURITY_REPLAY_PROTECTED);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_default_router, &route));

  hal_command_response_t response{};
  hal_command_request_t command_request = request("secure");
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, hal_command_router_dispatch(s_default_router,
                                                               &command_request,
                                                               &response));
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, response.status);

  command_request.source = HAL_COMMAND_SOURCE_LORA_LINK;
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, hal_command_router_dispatch(s_default_router,
                                                               &command_request,
                                                               &response));
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, response.status);
}

void test_register_copies_name_and_replaces_existing_route(void) {
  char mutable_name[] = "copy";
  hal_command_definition_t route = definition(mutable_name, denied_handler);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_default_router, &route));
  mutable_name[0] = 'X';

  route = definition("copy", response_status_handler);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_default_router, &route));
  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(s_default_router, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);

  hal_command_request_t command_request = request("copy");
  hal_command_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, hal_command_router_dispatch(s_default_router,
                                                               &command_request,
                                                               &response));
  TEST_ASSERT_EQUAL_STRING("denied", response.message);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_unregister(s_default_router, "copy"));
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, hal_command_router_unregister(s_default_router, "copy"));
}

void test_unique_registration_and_owned_unregister_preserve_foreign_route(
    void) {
  hal_command_definition_t owned = definition("owned", capture_handler);
  owned.user = &s_user_marker;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_register_unique(s_default_router, &owned));

  hal_command_definition_t foreign =
      definition("owned", denied_handler,
                 HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION),
                 HAL_COMMAND_SECURITY_AUTHENTICATED);
  TEST_ASSERT_EQUAL_INT(HAL_EEXIST, hal_command_router_register_unique(
                                        s_default_router, &foreign));

  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(s_default_router, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);

  hal_command_request_t command_request = request("owned");
  hal_command_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_dispatch(s_default_router,
                                                            &command_request,
                                                            &response));
  TEST_ASSERT_EQUAL_UINT32(1u, s_calls);

  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_command_router_unregister_if_matches(
                                       s_default_router, "owned",
                                       denied_handler, &s_user_marker));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        hal_command_router_unregister_if_matches(
                            s_default_router, "owned", capture_handler, NULL));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_unregister_if_matches(
                  s_default_router, "owned", capture_handler, &s_user_marker));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_command_router_unregister_if_matches(
                                        s_default_router, "owned",
                                        capture_handler, &s_user_marker));
}

void test_concurrent_unique_registration_has_one_owner(void) {
  hal_command_definition_t first = definition("race", capture_handler);
  first.user = &s_user_marker;
  hal_command_definition_t second = definition("race", response_status_handler);

  std::atomic<uint32_t> ready{0u};
  std::atomic<bool> start{false};
  hal_status_t first_status = HAL_NONE;
  hal_status_t second_status = HAL_NONE;
  auto register_after_start = [&](const hal_command_definition_t *route,
                                  hal_status_t *out_status) {
    ready.fetch_add(1u, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    *out_status = hal_command_router_register_unique(s_default_router, route);
  };

  std::thread first_thread(register_after_start, &first, &first_status);
  std::thread second_thread(register_after_start, &second, &second_status);
  while (ready.load(std::memory_order_acquire) != 2u) {
    std::this_thread::yield();
  }
  start.store(true, std::memory_order_release);
  first_thread.join();
  second_thread.join();

  TEST_ASSERT_TRUE((first_status == HAL_OK && second_status == HAL_EEXIST) ||
                   (first_status == HAL_EEXIST && second_status == HAL_OK));
  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(s_default_router, &count));
  TEST_ASSERT_EQUAL_UINT(1u, count);
  if (first_status == HAL_OK) {
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_command_router_unregister_if_matches(
                    s_default_router, "race", capture_handler, &s_user_marker));
  } else {
    TEST_ASSERT_EQUAL_INT(
        HAL_OK, hal_command_router_unregister_if_matches(
                    s_default_router, "race", response_status_handler, NULL));
  }
}

void test_independent_router_has_a_separate_registry(void) {
  hal_command_router_t private_router = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&private_router));
  hal_command_definition_t route = definition("private", capture_handler);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(private_router, &route));

  hal_command_response_t response{};
  hal_command_request_t command_request = request("private");
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, hal_command_router_dispatch(s_default_router,
                                              &command_request, &response));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_command_router_dispatch(private_router, &command_request, &response));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(private_router));
  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_command_router_count(private_router, &count));
  TEST_ASSERT_EQUAL_INT(HAL_EPERM,
                        hal_command_router_destroy(s_default_router));
}

void test_active_route_cannot_remove_itself(void) {
  hal_command_definition_t route = definition("busy", reentrant_handler);
  route.user = s_default_router;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(s_default_router, &route));
  hal_command_request_t command_request = request("busy");
  hal_command_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_dispatch(s_default_router,
                                                            &command_request,
                                                            &response));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, s_reentrant_status);
}

void test_active_route_cannot_owned_unregister_itself(void) {
  hal_command_definition_t route =
      definition("owned_busy", owned_reentrant_handler);
  route.user = s_default_router;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_register_unique(s_default_router, &route));
  hal_command_request_t command_request = request("owned_busy");
  hal_command_response_t response{};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_dispatch(s_default_router,
                                                            &command_request,
                                                            &response));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, s_owned_reentrant_status);
}

void test_destroy_does_not_reuse_context_during_active_dispatch(void) {
  hal_command_router_t private_router = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&private_router));
  hal_command_definition_t route = definition("blocking", blocking_handler);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(private_router, &route));

  hal_command_router_t fillers[HAL_COMMAND_ROUTER_MAX_INSTANCES] = {};
  size_t filler_count = 0u;
  hal_status_t fill_status = HAL_OK;
  while (filler_count < HAL_COMMAND_ROUTER_MAX_INSTANCES) {
    hal_command_router_t candidate = NULL;
    fill_status = hal_command_router_create(&candidate);
    if (fill_status != HAL_OK) {
      break;
    }
    fillers[filler_count++] = candidate;
  }
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, fill_status);

  hal_status_t dispatch_status = HAL_NONE;
  std::thread dispatcher([&]() {
    hal_command_request_t command_request = request("blocking");
    hal_command_response_t response{};
    dispatch_status = hal_command_router_dispatch(private_router,
                                                  &command_request, &response);
  });
  while (!s_blocking_handler_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  const hal_status_t active_destroy_status =
      hal_command_router_destroy(private_router);
  size_t live_count = 0u;
  const hal_status_t live_count_status =
      hal_command_router_count(private_router, &live_count);
  hal_command_router_t premature_router = NULL;
  const hal_status_t premature_create_status =
      hal_command_router_create(&premature_router);

  s_blocking_handler_may_return.store(true, std::memory_order_release);
  dispatcher.join();

  const hal_status_t final_destroy_status =
      hal_command_router_destroy(private_router);
  size_t stale_count = 0u;
  const hal_status_t stale_status =
      hal_command_router_count(private_router, &stale_count);
  if (final_destroy_status != HAL_OK) {
    (void)hal_command_router_destroy(private_router);
  }
  if (premature_router != NULL) {
    (void)hal_command_router_destroy(premature_router);
  }
  hal_command_router_t replacement_router = NULL;
  const hal_status_t replacement_create_status =
      hal_command_router_create(&replacement_router);
  if (replacement_router != NULL) {
    (void)hal_command_router_destroy(replacement_router);
  }
  for (size_t index = 0u; index < filler_count; ++index) {
    (void)hal_command_router_destroy(fillers[index]);
  }

  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, active_destroy_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, live_count_status);
  TEST_ASSERT_EQUAL_UINT(1u, live_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, dispatch_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, final_destroy_status);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, stale_status);
  TEST_ASSERT_EQUAL_UINT(0u, stale_count);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, premature_create_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, replacement_create_status);
}

void test_router_rejects_invalid_definitions_and_requests(void) {
  hal_command_definition_t route = definition("bad name", capture_handler);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_command_router_register(s_default_router, &route));
  route = definition("zero", capture_handler, 0u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_command_router_register(s_default_router, &route));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_command_router_register(s_default_router, NULL));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_command_router_register_unique(s_default_router, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_command_router_unregister_if_matches(
                            s_default_router, "missing", NULL, NULL));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_command_router_unregister_if_matches(
                      s_default_router, "bad name", capture_handler, NULL));

  hal_command_request_t command_request = request("missing");
  hal_command_response_t response{};
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, hal_command_router_dispatch(s_default_router,
                                              &command_request, &response));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, response.status);
  command_request.arguments_length = 1u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_command_router_dispatch(s_default_router,
                                              &command_request, &response));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dispatch_preserves_binary_arguments_and_metadata);
  RUN_TEST(test_source_and_security_policies_reject_before_handler);
  RUN_TEST(test_register_copies_name_and_replaces_existing_route);
  RUN_TEST(
      test_unique_registration_and_owned_unregister_preserve_foreign_route);
  RUN_TEST(test_concurrent_unique_registration_has_one_owner);
  RUN_TEST(test_independent_router_has_a_separate_registry);
  RUN_TEST(test_active_route_cannot_remove_itself);
  RUN_TEST(test_active_route_cannot_owned_unregister_itself);
  RUN_TEST(test_destroy_does_not_reuse_context_during_active_dispatch);
  RUN_TEST(test_router_rejects_invalid_definitions_and_requests);
  return UNITY_END();
}
