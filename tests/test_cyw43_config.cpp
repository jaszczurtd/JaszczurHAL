#include "hal/impl/shared/network/jh_cyw43_config.h"
#include "hal/impl/shared/network/jh_cyw43_scan.h"
#include "hal/impl/shared/network/jh_dns_request_state.h"
#include "hal/impl/shared/network/jh_icmp_echo.h"
#include "utils/unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static jh_cyw43_bus_config_t valid_config(void) {
  const jh_cyw43_bus_config_t config = {
      2u, 4u, 4u, 4u, 5u, 3u, 30u, 4u, 0u,
  };
  return config;
}

void test_valid_shared_data_profile_is_accepted(void) {
  const jh_cyw43_bus_config_t config = valid_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_bus_config_validate(&config));
}

void test_null_and_incomplete_profiles_are_rejected(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_cyw43_bus_config_validate(nullptr));

  jh_cyw43_bus_config_t config = valid_config();
  config.gpio_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, jh_cyw43_bus_config_validate(&config));

  config = valid_config();
  config.pio_clock_div_int = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, jh_cyw43_bus_config_validate(&config));
}

void test_out_of_range_pin_is_rejected(void) {
  jh_cyw43_bus_config_t config = valid_config();
  config.pin_clock = config.gpio_count;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_cyw43_bus_config_validate(&config));
}

void test_scan_security_bits_are_mapped(void) {
  TEST_ASSERT_EQUAL_INT(HAL_WIFI_ENC_NONE, jh_cyw43_scan_auth_to_hal(0u));
  TEST_ASSERT_EQUAL_INT(HAL_WIFI_ENC_WPA, jh_cyw43_scan_auth_to_hal(3u));
  TEST_ASSERT_EQUAL_INT(HAL_WIFI_ENC_WPA2, jh_cyw43_scan_auth_to_hal(5u));
  TEST_ASSERT_EQUAL_INT(HAL_WIFI_ENC_AUTO, jh_cyw43_scan_auth_to_hal(7u));
  TEST_ASSERT_EQUAL_INT(HAL_WIFI_ENC_UNKNOWN, jh_cyw43_scan_auth_to_hal(1u));
}

static void make_icmp_reply(uint8_t packet[28], uint16_t identifier,
                            uint16_t sequence) {
  memset(packet, 0, 28u);
  packet[0] = 0x45u;
  packet[8] = 57u;
  packet[9] = 1u;
  packet[20] = 0u;
  packet[21] = 0u;
  packet[24] = (uint8_t)(identifier >> 8u);
  packet[25] = (uint8_t)identifier;
  packet[26] = (uint8_t)(sequence >> 8u);
  packet[27] = (uint8_t)sequence;
}

void test_icmp_echo_reply_parser_accepts_matching_reply(void) {
  uint8_t packet[28];
  make_icmp_reply(packet, 0x4a48u, 7u);
  int ttl = -1;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_icmp_echo_reply_parse(packet, sizeof(packet),
                                                         0x4a48u, 7u, &ttl));
  TEST_ASSERT_EQUAL_INT(57, ttl);
}

void test_icmp_echo_reply_parser_rejects_unrelated_and_truncated_packets(void) {
  uint8_t packet[28];
  make_icmp_reply(packet, 0x4a48u, 7u);
  int ttl = -1;
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT,
      jh_icmp_echo_reply_parse(packet, sizeof(packet), 0x4a48u, 8u, &ttl));
  packet[20] = 8u;
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT,
      jh_icmp_echo_reply_parse(packet, sizeof(packet), 0x4a48u, 7u, &ttl));
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, jh_icmp_echo_reply_parse(packet, 19u, 0x4a48u, 7u, &ttl));
}

void test_dns_timeout_releases_request_for_next_resolve(void) {
  jh_dns_ipv4_request_state_t state = {};
  const uint32_t timed_out_generation = jh_dns_ipv4_request_begin(&state);
  TEST_ASSERT_NOT_EQUAL(0u, timed_out_generation);
  TEST_ASSERT_TRUE(state.active);
  TEST_ASSERT_TRUE(jh_dns_ipv4_request_cancel(&state, timed_out_generation));
  TEST_ASSERT_FALSE(state.active);
  TEST_ASSERT_FALSE(state.completed);

  const uint32_t next_generation = jh_dns_ipv4_request_begin(&state);
  TEST_ASSERT_NOT_EQUAL(0u, next_generation);
  TEST_ASSERT_NOT_EQUAL(timed_out_generation, next_generation);
  TEST_ASSERT_TRUE(state.active);
}

void test_dns_late_callback_cannot_complete_new_request(void) {
  jh_dns_ipv4_request_state_t state = {};
  const uint32_t old_generation = jh_dns_ipv4_request_begin(&state);
  TEST_ASSERT_TRUE(jh_dns_ipv4_request_cancel(&state, old_generation));
  const uint32_t current_generation = jh_dns_ipv4_request_begin(&state);

  const uint8_t stale_address[4] = {192u, 0u, 2u, 10u};
  TEST_ASSERT_FALSE(jh_dns_ipv4_request_complete(&state, old_generation, true,
                                                 stale_address));
  TEST_ASSERT_TRUE(state.active);
  TEST_ASSERT_FALSE(state.completed);

  const uint8_t current_address[4] = {198u, 51u, 100u, 20u};
  TEST_ASSERT_TRUE(jh_dns_ipv4_request_complete(&state, current_generation,
                                                true, current_address));
  TEST_ASSERT_FALSE(state.active);
  TEST_ASSERT_TRUE(state.completed);
  TEST_ASSERT_TRUE(state.found);
  TEST_ASSERT_EQUAL_MEMORY(current_address, state.address,
                           sizeof(current_address));
}

void test_dns_negative_completion_releases_request(void) {
  jh_dns_ipv4_request_state_t state = {};
  const uint32_t generation = jh_dns_ipv4_request_begin(&state);
  TEST_ASSERT_TRUE(
      jh_dns_ipv4_request_complete(&state, generation, false, nullptr));
  TEST_ASSERT_FALSE(state.active);
  TEST_ASSERT_TRUE(state.completed);
  TEST_ASSERT_FALSE(state.found);
  TEST_ASSERT_NOT_EQUAL(0u, jh_dns_ipv4_request_begin(&state));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_shared_data_profile_is_accepted);
  RUN_TEST(test_null_and_incomplete_profiles_are_rejected);
  RUN_TEST(test_out_of_range_pin_is_rejected);
  RUN_TEST(test_scan_security_bits_are_mapped);
  RUN_TEST(test_icmp_echo_reply_parser_accepts_matching_reply);
  RUN_TEST(test_icmp_echo_reply_parser_rejects_unrelated_and_truncated_packets);
  RUN_TEST(test_dns_timeout_releases_request_for_next_resolve);
  RUN_TEST(test_dns_late_callback_cannot_complete_new_request);
  RUN_TEST(test_dns_negative_completion_releases_request);
  return UNITY_END();
}
