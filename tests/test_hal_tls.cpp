#include "hal/hal_net.h"
#include "hal/hal_tls.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/network/jh_tls_timeout_policy.h"
#include "utils/unity.h"

#include <string.h>

#include "fixtures/tls_test_ca_der.inc"

static hal_status_t fake_time(void *context, uint64_t *out_unix_seconds) {
  *out_unix_seconds = *static_cast<uint64_t *>(context);
  return HAL_OK;
}

static hal_status_t failing_entropy(void *, void *, size_t) { return HAL_EHW; }

typedef struct {
  hal_tls_client_t client;
  hal_status_t reentrant_status;
  hal_status_t close_status;
  bool close_client;
} callback_context_t;

static hal_status_t reentrant_time(void *context, uint64_t *out_unix_seconds) {
  callback_context_t *callback = static_cast<callback_context_t *>(context);
  hal_tls_state_t state = HAL_TLS_STATE_FAILED;
  callback->reentrant_status =
      hal_tls_client_get_state_ex(callback->client, &state);
  if (callback->close_client) {
    callback->close_status = hal_tls_client_close_ex(callback->client);
  }
  *out_unix_seconds = HAL_TLS_MIN_VALID_UNIX_TIME;
  return HAL_OK;
}

static hal_tls_trust_anchor_t fake_anchor(void) {
  static const uint8_t dn[] = {0x30u, 0x00u};
  static const uint8_t modulus[] = {1u};
  static const uint8_t exponent[] = {3u};
  hal_tls_trust_anchor_t anchor = {};
  anchor.subject_dn = dn;
  anchor.subject_dn_length = sizeof(dn);
  anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
  anchor.key.rsa.modulus = modulus;
  anchor.key.rsa.modulus_length = sizeof(modulus);
  anchor.key.rsa.exponent = exponent;
  anchor.key.rsa.exponent_length = sizeof(exponent);
  return anchor;
}

void setUp(void) { hal_mock_time_reset(); }
void tearDown(void) {}

void test_default_config_is_finite_and_poll_driven(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  TEST_ASSERT_EQUAL_INT(HAL_TLS_EXECUTION_POLL, config.execution_model);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, config.transport_timeout_ms);
  TEST_ASSERT_NOT_EQUAL_UINT32(HAL_NET_TIMEOUT_FOREVER,
                               config.transport_timeout_ms);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, config.operation_timeout_ms);
  TEST_ASSERT_GREATER_THAN_UINT16(0u, config.poll_step_budget);
}

void test_operation_timeout_applies_only_to_transition_states(void) {
  TEST_ASSERT_TRUE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_CONNECTING));
  TEST_ASSERT_TRUE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_CLOSING));
  TEST_ASSERT_FALSE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_CONNECTED));
  TEST_ASSERT_FALSE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_CREATED));
  TEST_ASSERT_FALSE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_CONFIGURED));
  TEST_ASSERT_FALSE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_CLOSED));
  TEST_ASSERT_FALSE(jh_tls_operation_timeout_applies(HAL_TLS_STATE_FAILED));
}

void test_default_time_and_entropy_fail_closed_until_platform_is_ready(void) {
  uint64_t unix_seconds = UINT64_MAX;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_tls_default_time(NULL, &unix_seconds));
  TEST_ASSERT_EQUAL_UINT64(0u, unix_seconds);

  hal_mock_time_set_unix(HAL_TLS_MIN_VALID_UNIX_TIME);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_default_time(NULL, &unix_seconds));
  TEST_ASSERT_EQUAL_UINT64(HAL_TLS_MIN_VALID_UNIX_TIME, unix_seconds);

  uint8_t entropy[16];
  memset(entropy, 0xa5, sizeof(entropy));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_tls_default_entropy(
                                              NULL, entropy, sizeof(entropy)));
  const uint8_t zeroes[sizeof(entropy)] = {};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zeroes, entropy, sizeof(entropy));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tls_default_entropy(NULL, NULL, 1u));
}

void test_create_rejects_unbounded_or_invalid_configuration(void) {
  hal_tls_client_t client = nullptr;
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));

  config.transport_timeout_ms = HAL_NET_TIMEOUT_FOREVER;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tls_client_create_ex(&config, &client));
  config.transport_timeout_ms = 10u;
  config.operation_timeout_ms = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tls_client_create_ex(&config, &client));
  config.operation_timeout_ms = 10u;
  config.poll_step_budget = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tls_client_create_ex(&config, &client));
  TEST_ASSERT_NULL(client);
}

void test_handle_lifecycle_is_generation_checked(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t stale = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &stale));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(stale));

  hal_tls_client_t current = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &current));
  TEST_ASSERT_NOT_EQUAL(stale, current);
  hal_tls_state_t state = HAL_TLS_STATE_FAILED;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tls_client_get_state_ex(stale, &state));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_get_state_ex(current, &state));
  TEST_ASSERT_EQUAL_INT(HAL_TLS_STATE_CREATED, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(current));
}

void test_server_configuration_retains_no_provider_types_and_is_bounded(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t client = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &client));

  char too_long[HAL_TLS_HOSTNAME_MAX_LENGTH + 2u] = {};
  memset(too_long, 'a', sizeof(too_long) - 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_tls_client_configure_server_ex(
                                           client, too_long, 443u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_server_ex(client, "example.com", 443u));

  hal_tls_state_t state = HAL_TLS_STATE_CREATED;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_get_state_ex(client, &state));
  TEST_ASSERT_EQUAL_INT(HAL_TLS_STATE_CONFIGURED, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(client));
}

void test_connect_fails_closed_until_security_configuration_exists(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t client = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &client));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_server_ex(client, "example.com", 443u));
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_tls_client_connect_ex(client));

  hal_status_t last_status = HAL_NONE;
  int32_t provider_error = -1;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_get_last_error_ex(
                                    client, &last_status, &provider_error));
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, last_status);
  TEST_ASSERT_EQUAL_INT32(0, provider_error);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_get_last_error_ex(client, &last_status, NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_shutdown_ex(client));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(client));
}

void test_security_configuration_requires_ca_time_and_entropy(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t client = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &client));

  hal_tls_security_config_t security = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_ECONFIG, hal_tls_client_configure_security_ex(client, &security));
  static const uint8_t dn[] = {0x30u, 0x00u};
  static const uint8_t modulus[] = {1u};
  static const uint8_t exponent[] = {3u};
  hal_tls_trust_anchor_t anchor = {};
  anchor.subject_dn = dn;
  anchor.subject_dn_length = sizeof(dn);
  anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
  anchor.key.rsa.modulus = modulus;
  anchor.key.rsa.modulus_length = sizeof(modulus);
  anchor.key.rsa.exponent = exponent;
  anchor.key.rsa.exponent_length = sizeof(exponent);
  uint64_t unix_seconds = HAL_TLS_MIN_VALID_UNIX_TIME;
  security.trust_anchors = &anchor;
  security.trust_anchor_count = 1u;
  security.get_time = fake_time;
  security.get_entropy = failing_entropy;
  security.callback_context = &unix_seconds;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_security_ex(client, &security));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_server_ex(client, "example.com", 443u));
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_tls_client_connect_ex(client));
  hal_tls_state_t state = HAL_TLS_STATE_CREATED;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_get_state_ex(client, &state));
  TEST_ASSERT_EQUAL_INT(HAL_TLS_STATE_FAILED, state);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(client));
}

void test_der_ca_is_decoded_into_provider_neutral_anchor(void) {
  hal_tls_trust_anchor_storage_t storage = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_trust_anchor_from_der_ex(jh_test_ca_der,
                                               jh_test_ca_der_len, &storage));
  TEST_ASSERT_GREATER_THAN_UINT(0u, storage.anchor.subject_dn_length);
  TEST_ASSERT_EQUAL_INT(HAL_TLS_TRUST_KEY_RSA, storage.anchor.key_type);
  TEST_ASSERT_GREATER_THAN_UINT(0u, storage.anchor.key.rsa.modulus_length);
  TEST_ASSERT_GREATER_THAN_UINT(0u, storage.anchor.key.rsa.exponent_length);

  uint8_t malformed[] = {0x30u, 0x01u, 0x00u};
  TEST_ASSERT_EQUAL_INT(HAL_EAUTH, hal_tls_trust_anchor_from_der_ex(
                                       malformed, sizeof(malformed), &storage));
}

void test_pool_limit_and_release_are_deterministic(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t clients[HAL_TLS_MAX_CLIENTS] = {};
  for (size_t index = 0u; index < HAL_TLS_MAX_CLIENTS; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_tls_client_create_ex(&config, &clients[index]));
  }
  hal_tls_client_t extra = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_tls_client_create_ex(&config, &extra));
  for (size_t index = 0u; index < HAL_TLS_MAX_CLIENTS; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(clients[index]));
  }
}

void test_security_callback_reentry_returns_busy_without_deadlock(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t client = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &client));
  callback_context_t context = {client, HAL_NONE, HAL_NONE, false};
  hal_tls_trust_anchor_t anchor = fake_anchor();
  hal_tls_security_config_t security = {};
  security.trust_anchors = &anchor;
  security.trust_anchor_count = 1u;
  security.get_time = reentrant_time;
  security.get_entropy = failing_entropy;
  security.callback_context = &context;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_security_ex(client, &security));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_server_ex(client, "example.com", 443u));

  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_tls_client_connect_ex(client));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, context.reentrant_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(client));
}

void test_security_callback_may_close_client_with_deferred_cleanup(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t client = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &client));
  callback_context_t context = {client, HAL_NONE, HAL_NONE, true};
  hal_tls_trust_anchor_t anchor = fake_anchor();
  hal_tls_security_config_t security = {};
  security.trust_anchors = &anchor;
  security.trust_anchor_count = 1u;
  security.get_time = reentrant_time;
  security.get_entropy = failing_entropy;
  security.callback_context = &context;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_security_ex(client, &security));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_configure_server_ex(client, "example.com", 443u));

  TEST_ASSERT_EQUAL_INT(HAL_ECANCELED, hal_tls_client_connect_ex(client));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, context.reentrant_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, context.close_status);
  hal_tls_state_t state = HAL_TLS_STATE_FAILED;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tls_client_get_state_ex(client, &state));

  hal_tls_client_t replacement = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tls_client_create_ex(&config, &replacement));
  TEST_ASSERT_NOT_EQUAL(client, replacement);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(replacement));
}

void test_zero_length_io_accepts_null_buffers_without_provider_access(void) {
  hal_tls_client_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_config_init(&config));
  hal_tls_client_t client = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_create_ex(&config, &client));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_tls_mark_connected_for_zero_io(client));

  size_t transferred = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tls_client_read_ex(client, NULL, 0u, &transferred));
  TEST_ASSERT_EQUAL_size_t(0u, transferred);
  transferred = 99u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_tls_client_write_ex(client, NULL, 0u, &transferred));
  TEST_ASSERT_EQUAL_size_t(0u, transferred);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tls_client_close_ex(client));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_is_finite_and_poll_driven);
  RUN_TEST(test_operation_timeout_applies_only_to_transition_states);
  RUN_TEST(test_default_time_and_entropy_fail_closed_until_platform_is_ready);
  RUN_TEST(test_create_rejects_unbounded_or_invalid_configuration);
  RUN_TEST(test_handle_lifecycle_is_generation_checked);
  RUN_TEST(test_server_configuration_retains_no_provider_types_and_is_bounded);
  RUN_TEST(test_connect_fails_closed_until_security_configuration_exists);
  RUN_TEST(test_security_configuration_requires_ca_time_and_entropy);
  RUN_TEST(test_der_ca_is_decoded_into_provider_neutral_anchor);
  RUN_TEST(test_pool_limit_and_release_are_deterministic);
  RUN_TEST(test_security_callback_reentry_returns_busy_without_deadlock);
  RUN_TEST(test_security_callback_may_close_client_with_deferred_cleanup);
  RUN_TEST(test_zero_length_io_accepts_null_buffers_without_provider_access);
  return UNITY_END();
}
