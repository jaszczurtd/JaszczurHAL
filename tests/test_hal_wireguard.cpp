#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/wireguard/hal_wireguard.h"
#include "utils/unity.h"

#include <string.h>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_wireguard_reset();
}

void tearDown(void) {}

void test_parse_ipv4_helper_validation(void) {
  uint8_t parsed[4] = {0u, 0u, 0u, 0u};
  const uint8_t expected[4] = {192u, 168u, 10u, 25u};

  TEST_ASSERT_TRUE(hal_wireguard_parse_ipv4("192.168.10.25", parsed));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, parsed, 4);

  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4(NULL, parsed));
  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4("", parsed));
  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4("10.8.0", parsed));
  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4("10.8.0.300", parsed));
  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4("10.8.0.1x", parsed));
  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4("10..0.1", parsed));
  TEST_ASSERT_FALSE(hal_wireguard_parse_ipv4("10.8.0.1", NULL));
}

void test_text_helpers_begin_begin_advanced_and_kick_success_path(void) {
  const uint8_t expected_local_ip[4] = {10u, 8u, 0u, 60u};
  const uint8_t expected_allowed_ip[4] = {192u, 168u, 20u, 0u};
  const uint8_t expected_allowed_mask[4] = {255u, 255u, 255u, 0u};
  const uint8_t expected_probe_ip[4] = {198u, 51u, 100u, 9u};

  TEST_ASSERT_TRUE(hal_wireguard_begin_text(
      "10.8.0.60", "priv-txt", "wg.example.com", "pub-txt", 51820u));
  TEST_ASSERT_TRUE(hal_wireguard_is_initialized());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_local_ip,
                                hal_mock_wireguard_get_last_local_ip(), 4);

  TEST_ASSERT_TRUE(hal_wireguard_begin_advanced_text(
      "10.8.0.60", "priv-txt-adv", "wg.example.com", "pub-txt-adv", 51820u,
      "192.168.20.0", "255.255.255.0"));
  TEST_ASSERT_TRUE(hal_mock_wireguard_was_begin_advanced());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_local_ip,
                                hal_mock_wireguard_get_last_local_ip(), 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_allowed_ip,
                                hal_mock_wireguard_get_last_allowed_ip(), 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_allowed_mask,
                                hal_mock_wireguard_get_last_allowed_mask(), 4);

  TEST_ASSERT_TRUE(
      hal_wireguard_kick_handshake_text("198.51.100.9", 33434u, 444u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_probe_ip,
                                hal_mock_wireguard_get_last_probe_ip(), 4);
  TEST_ASSERT_EQUAL_UINT16(33434u, hal_mock_wireguard_get_last_probe_port());
  TEST_ASSERT_EQUAL_UINT32(444u,
                           hal_mock_wireguard_get_last_probe_min_interval_ms());
}

void test_text_helpers_reject_invalid_ipv4_and_preserve_state_on_failure(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 77u};

  TEST_ASSERT_TRUE(hal_wireguard_begin(local_ip, "priv-pre", "wg.example.com",
                                       "pub-pre", 51820u));

  hal_mock_wireguard_set_kick_result(true);

  TEST_ASSERT_FALSE(hal_wireguard_begin_text("10.8.0.999", "priv",
                                             "wg.example.com", "pub", 51820u));
  TEST_ASSERT_FALSE(hal_wireguard_begin_advanced_text(
      "10.8.0.77", "priv", "wg.example.com", "pub", 51820u, "192.168.20.0",
      "255.255.bad.0"));

  TEST_ASSERT_FALSE(
      hal_wireguard_kick_handshake_text("300.1.1.1", 33434u, 100u));

  TEST_ASSERT_TRUE(hal_wireguard_is_initialized());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_begin_and_end_full_tunnel_flow(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 50u};

  TEST_ASSERT_TRUE(hal_wireguard_begin(
      local_ip, "priv-base64", "vpn.example.com", "pub-base64", 51820u));
  TEST_ASSERT_TRUE(hal_wireguard_is_initialized());

  const uint8_t *saved_local = hal_mock_wireguard_get_last_local_ip();
  TEST_ASSERT_EQUAL_UINT8_ARRAY(local_ip, saved_local, 4);
  TEST_ASSERT_EQUAL_STRING("vpn.example.com",
                           hal_mock_wireguard_get_last_remote_peer_address());
  TEST_ASSERT_EQUAL_UINT16(51820u,
                           hal_mock_wireguard_get_last_remote_peer_port());
  TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());

  hal_wireguard_end();
  TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
}

void test_begin_advanced_peer_up_and_kick(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 51u};
  const uint8_t allowed_ip[4] = {192u, 168u, 10u, 0u};
  const uint8_t allowed_mask[4] = {255u, 255u, 255u, 0u};
  const uint8_t peer_ip[4] = {203u, 0u, 113u, 10u};
  const uint8_t probe_ip[4] = {192u, 168u, 10u, 1u};

  hal_mock_wireguard_set_peer_up_result(true);
  hal_mock_wireguard_set_peer_endpoint(peer_ip, 42123u);

  TEST_ASSERT_TRUE(hal_wireguard_begin_advanced(local_ip, "priv-2",
                                                "203.0.113.10", "pub-2", 51820u,
                                                allowed_ip, allowed_mask));

  TEST_ASSERT_TRUE(hal_mock_wireguard_was_begin_advanced());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(allowed_ip,
                                hal_mock_wireguard_get_last_allowed_ip(), 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(allowed_mask,
                                hal_mock_wireguard_get_last_allowed_mask(), 4);

  char endpoint_ip[HAL_WIREGUARD_IP_STR_LEN] = {0};
  uint16_t endpoint_port = 0u;
  TEST_ASSERT_TRUE(
      hal_wireguard_peer_up(endpoint_ip, sizeof(endpoint_ip), &endpoint_port));
  TEST_ASSERT_EQUAL_STRING("203.0.113.10", endpoint_ip);
  TEST_ASSERT_EQUAL_UINT16(42123u, endpoint_port);

  TEST_ASSERT_TRUE(hal_wireguard_kick_handshake(probe_ip, 33434u, 333u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(probe_ip,
                                hal_mock_wireguard_get_last_probe_ip(), 4);
  TEST_ASSERT_EQUAL_UINT16(33434u, hal_mock_wireguard_get_last_probe_port());
  TEST_ASSERT_EQUAL_UINT32(333u,
                           hal_mock_wireguard_get_last_probe_min_interval_ms());
}

void test_invalid_inputs_are_rejected(void) {
  const uint8_t ip[4] = {10u, 8u, 0u, 1u};

  TEST_ASSERT_FALSE(hal_wireguard_begin(NULL, "priv", "host", "pub", 51820u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_begin(ip, "", "host", "pub", 51820u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_begin(ip, "priv", NULL, "pub", 51820u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_begin(ip, "priv", "host", "pub", 0u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_begin_advanced(ip, "priv", "host", "pub",
                                                 51820u, NULL, ip));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_peer_up((char *)ip, 0u, NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_kick_handshake(NULL, 33434u, 100u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  hal_mock_serial_reset();
  TEST_ASSERT_FALSE(hal_wireguard_kick_handshake(ip, 0u, 100u));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_begin_failure_does_not_initialize_and_recovery_works(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 52u};
  const uint8_t allowed_ip[4] = {172u, 16u, 0u, 0u};
  const uint8_t allowed_mask[4] = {255u, 240u, 0u, 0u};

  hal_mock_wireguard_set_begin_result(false);
  TEST_ASSERT_FALSE(hal_wireguard_begin(local_ip, "priv-fail",
                                        "wg.fail.example", "pub-fail", 51820u));
  TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
  TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());

  TEST_ASSERT_FALSE(hal_wireguard_begin_advanced(
      local_ip, "priv-fail", "wg.fail.example", "pub-fail", 51820u, allowed_ip,
      allowed_mask));
  TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
  TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());

  hal_mock_wireguard_set_begin_result(true);
  TEST_ASSERT_TRUE(
      hal_wireguard_begin_advanced(local_ip, "priv-ok", "wg.ok.example",
                                   "pub-ok", 51820u, allowed_ip, allowed_mask));
  TEST_ASSERT_TRUE(hal_wireguard_is_initialized());
  TEST_ASSERT_TRUE(hal_mock_wireguard_was_begin_advanced());
}

void test_peer_up_output_variants_and_down_state(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 53u};
  const uint8_t peer_ip[4] = {198u, 51u, 100u, 20u};

  TEST_ASSERT_TRUE(hal_wireguard_begin(local_ip, "priv-peer", "wg.peer.example",
                                       "pub-peer", 51820u));

  hal_mock_wireguard_set_peer_up_result(true);
  hal_mock_wireguard_set_peer_endpoint(peer_ip, 41000u);

  TEST_ASSERT_TRUE(hal_wireguard_peer_up_quick());
  TEST_ASSERT_EQUAL_UINT32(1u,
                           hal_mock_wireguard_get_peer_up_quick_call_count());

  uint16_t endpoint_port = 0u;
  TEST_ASSERT_TRUE(hal_wireguard_peer_up(NULL, 0u, &endpoint_port));
  TEST_ASSERT_EQUAL_UINT16(41000u, endpoint_port);

  char endpoint_ip[HAL_WIREGUARD_IP_STR_LEN] = {0};
  TEST_ASSERT_TRUE(
      hal_wireguard_peer_up(endpoint_ip, sizeof(endpoint_ip), NULL));
  TEST_ASSERT_EQUAL_STRING("198.51.100.20", endpoint_ip);

  hal_mock_wireguard_set_peer_up_result(false);
  endpoint_port = 11111u;
  TEST_ASSERT_FALSE(hal_wireguard_peer_up(NULL, 0u, &endpoint_port));
  TEST_ASSERT_EQUAL_UINT16(11111u, endpoint_port);
  TEST_ASSERT_FALSE(hal_wireguard_peer_up_quick());
  TEST_ASSERT_EQUAL_UINT32(2u,
                           hal_mock_wireguard_get_peer_up_quick_call_count());

  hal_wireguard_end();
  hal_mock_wireguard_set_peer_up_result(true);
  TEST_ASSERT_FALSE(hal_wireguard_peer_up(NULL, 0u, &endpoint_port));
  TEST_ASSERT_FALSE(hal_wireguard_peer_up_quick());
  TEST_ASSERT_EQUAL_UINT32(3u,
                           hal_mock_wireguard_get_peer_up_quick_call_count());
}

void test_kick_handshake_requires_initialization_and_returns_driver_result(
    void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 54u};
  const uint8_t probe_ip[4] = {1u, 1u, 1u, 1u};

  TEST_ASSERT_FALSE(hal_wireguard_kick_handshake(probe_ip, 5353u, 250u));

  TEST_ASSERT_TRUE(hal_wireguard_begin(local_ip, "priv-kick", "wg.kick.example",
                                       "pub-kick", 51820u));

  hal_mock_wireguard_set_kick_result(false);
  TEST_ASSERT_FALSE(hal_wireguard_kick_handshake(probe_ip, 5353u, 250u));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(probe_ip,
                                hal_mock_wireguard_get_last_probe_ip(), 4);
  TEST_ASSERT_EQUAL_UINT16(5353u, hal_mock_wireguard_get_last_probe_port());
  TEST_ASSERT_EQUAL_UINT32(250u,
                           hal_mock_wireguard_get_last_probe_min_interval_ms());

  hal_mock_wireguard_set_kick_result(true);
  TEST_ASSERT_TRUE(hal_wireguard_kick_handshake(probe_ip, 5353u, 250u));
}

void test_config_init_sets_defaults_and_rejects_null(void) {
  hal_wireguard_config_t config;
  memset(&config, 0xA5, sizeof(config));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wireguard_config_init(&config));
  TEST_ASSERT_NULL(config.private_key);
  TEST_ASSERT_NULL(config.remote_peer_address);
  TEST_ASSERT_NULL(config.remote_peer_public_key);
  TEST_ASSERT_NULL(config.preshared_key);
  TEST_ASSERT_EQUAL_UINT16(0u, config.remote_peer_port);
  TEST_ASSERT_EQUAL_UINT16(HAL_WIREGUARD_KEEPALIVE_DEFAULT_S,
                           config.keepalive_s);
  TEST_ASSERT_EACH_EQUAL_UINT8(0u, config.local_ip, 4);
  TEST_ASSERT_EACH_EQUAL_UINT8(0u, config.allowed_ip, 4);
  TEST_ASSERT_EACH_EQUAL_UINT8(0u, config.allowed_mask, 4);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wireguard_config_init(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_begin_config_passes_preshared_key_and_keepalive(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 61u};
  const uint8_t allowed_ip[4] = {192u, 168u, 30u, 0u};
  const uint8_t allowed_mask[4] = {255u, 255u, 255u, 0u};
  hal_wireguard_config_t config;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wireguard_config_init(&config));
  memcpy(config.local_ip, local_ip, sizeof(local_ip));
  config.private_key = "priv-cfg";
  config.remote_peer_address = "wg.example.com";
  config.remote_peer_public_key = "pub-cfg";
  config.remote_peer_port = 51820u;
  config.preshared_key = "psk-cfg";
  memcpy(config.allowed_ip, allowed_ip, sizeof(allowed_ip));
  memcpy(config.allowed_mask, allowed_mask, sizeof(allowed_mask));
  config.keepalive_s = 60u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wireguard_begin_config_ex(&config));
  TEST_ASSERT_TRUE(hal_wireguard_is_initialized());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(local_ip,
                                hal_mock_wireguard_get_last_local_ip(), 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(allowed_ip,
                                hal_mock_wireguard_get_last_allowed_ip(), 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(allowed_mask,
                                hal_mock_wireguard_get_last_allowed_mask(), 4);
  TEST_ASSERT_EQUAL_STRING("psk-cfg",
                           hal_mock_wireguard_get_last_preshared_key());
  TEST_ASSERT_EQUAL_UINT16(60u, hal_mock_wireguard_get_last_keepalive_s());
  TEST_ASSERT_TRUE(hal_mock_wireguard_was_begin_advanced());
  hal_wireguard_end();

  // The begin() helpers start without a preshared key and with the default
  // keepalive.
  TEST_ASSERT_TRUE(
      hal_wireguard_begin(local_ip, "priv", "wg.example.com", "pub", 51820u));
  TEST_ASSERT_EQUAL_STRING("", hal_mock_wireguard_get_last_preshared_key());
  TEST_ASSERT_EQUAL_UINT16(HAL_WIREGUARD_KEEPALIVE_DEFAULT_S,
                           hal_mock_wireguard_get_last_keepalive_s());
  TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());
  hal_wireguard_end();

  // The all-zero network is the full tunnel, also through a configuration.
  memset(config.allowed_ip, 0, sizeof(config.allowed_ip));
  memset(config.allowed_mask, 0, sizeof(config.allowed_mask));
  config.keepalive_s = 0u;
  TEST_ASSERT_TRUE(hal_wireguard_begin_config(&config));
  TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());
  TEST_ASSERT_EQUAL_UINT16(0u, hal_mock_wireguard_get_last_keepalive_s());
}

void test_begin_config_checks_the_keepalive_range_and_required_fields(void) {
  const uint8_t local_ip[4] = {10u, 8u, 0u, 62u};
  hal_wireguard_config_t config;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wireguard_config_init(&config));
  memcpy(config.local_ip, local_ip, sizeof(local_ip));
  config.private_key = "priv";
  config.remote_peer_address = "wg.example.com";
  config.remote_peer_public_key = "pub";
  config.remote_peer_port = 51820u;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wireguard_begin_config_ex(NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  config.keepalive_s = HAL_WIREGUARD_KEEPALIVE_MAX_S + 1u;
  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wireguard_begin_config_ex(&config));
  TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

  config.keepalive_s = HAL_WIREGUARD_KEEPALIVE_MAX_S;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wireguard_begin_config_ex(&config));
  TEST_ASSERT_EQUAL_UINT16(HAL_WIREGUARD_KEEPALIVE_MAX_S,
                           hal_mock_wireguard_get_last_keepalive_s());
  hal_wireguard_end();

  config.keepalive_s = HAL_WIREGUARD_KEEPALIVE_DEFAULT_S;
  config.private_key = "";
  hal_mock_serial_reset();
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wireguard_begin_config_ex(&config));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
  config.private_key = "priv";
  config.remote_peer_public_key = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wireguard_begin_config_ex(&config));
  config.remote_peer_public_key = "pub";
  config.remote_peer_port = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_wireguard_begin_config_ex(&config));
  TEST_ASSERT_FALSE(hal_wireguard_is_initialized());

  // The engine's refusal, for example of a preshared key that does not
  // decode, reaches the caller as HAL_EIO.
  config.remote_peer_port = 51820u;
  config.preshared_key = "bad";
  hal_mock_wireguard_set_begin_result(false);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_wireguard_begin_config_ex(&config));
  TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
  TEST_ASSERT_EQUAL_STRING("bad", hal_mock_wireguard_get_last_preshared_key());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_ipv4_helper_validation);
  RUN_TEST(test_text_helpers_begin_begin_advanced_and_kick_success_path);
  RUN_TEST(test_text_helpers_reject_invalid_ipv4_and_preserve_state_on_failure);
  RUN_TEST(test_begin_and_end_full_tunnel_flow);
  RUN_TEST(test_begin_advanced_peer_up_and_kick);
  RUN_TEST(test_invalid_inputs_are_rejected);
  RUN_TEST(test_begin_failure_does_not_initialize_and_recovery_works);
  RUN_TEST(test_config_init_sets_defaults_and_rejects_null);
  RUN_TEST(test_begin_config_passes_preshared_key_and_keepalive);
  RUN_TEST(test_begin_config_checks_the_keepalive_range_and_required_fields);
  RUN_TEST(test_peer_up_output_variants_and_down_state);
  RUN_TEST(
      test_kick_handshake_requires_initialization_and_returns_driver_result);
  return UNITY_END();
}
