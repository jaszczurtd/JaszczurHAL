#include "utils/unity.h"
#include "hal/hal_wireguard.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

void setUp(void) {
    hal_mock_serial_reset();
    hal_mock_wireguard_reset();
}

void tearDown(void) {}

void test_begin_and_end_full_tunnel_flow(void) {
    const uint8_t local_ip[4] = {10u, 8u, 0u, 50u};

    TEST_ASSERT_TRUE(hal_wireguard_begin(local_ip,
                                         "priv-base64",
                                         "vpn.example.com",
                                         "pub-base64",
                                         51820u));
    TEST_ASSERT_TRUE(hal_wireguard_is_initialized());

    const uint8_t *saved_local = hal_mock_wireguard_get_last_local_ip();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(local_ip, saved_local, 4);
    TEST_ASSERT_EQUAL_STRING("vpn.example.com", hal_mock_wireguard_get_last_remote_peer_address());
    TEST_ASSERT_EQUAL_UINT16(51820u, hal_mock_wireguard_get_last_remote_peer_port());
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

    TEST_ASSERT_TRUE(hal_wireguard_begin_advanced(local_ip,
                                                  "priv-2",
                                                  "203.0.113.10",
                                                  "pub-2",
                                                  51820u,
                                                  allowed_ip,
                                                  allowed_mask));

    TEST_ASSERT_TRUE(hal_mock_wireguard_was_begin_advanced());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(allowed_ip, hal_mock_wireguard_get_last_allowed_ip(), 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(allowed_mask, hal_mock_wireguard_get_last_allowed_mask(), 4);

    char endpoint_ip[HAL_WIREGUARD_IP_STR_LEN] = {0};
    uint16_t endpoint_port = 0u;
    TEST_ASSERT_TRUE(hal_wireguard_peer_up(endpoint_ip, sizeof(endpoint_ip), &endpoint_port));
    TEST_ASSERT_EQUAL_STRING("203.0.113.10", endpoint_ip);
    TEST_ASSERT_EQUAL_UINT16(42123u, endpoint_port);

    TEST_ASSERT_TRUE(hal_wireguard_kick_handshake(probe_ip, 33434u, 333u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(probe_ip, hal_mock_wireguard_get_last_probe_ip(), 4);
    TEST_ASSERT_EQUAL_UINT16(33434u, hal_mock_wireguard_get_last_probe_port());
    TEST_ASSERT_EQUAL_UINT32(333u, hal_mock_wireguard_get_last_probe_min_interval_ms());
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
    TEST_ASSERT_FALSE(hal_wireguard_begin_advanced(ip,
                                                   "priv",
                                                   "host",
                                                   "pub",
                                                   51820u,
                                                   NULL,
                                                   ip));
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
    TEST_ASSERT_FALSE(hal_wireguard_begin(local_ip,
                                          "priv-fail",
                                          "wg.fail.example",
                                          "pub-fail",
                                          51820u));
    TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
    TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());

    TEST_ASSERT_FALSE(hal_wireguard_begin_advanced(local_ip,
                                                   "priv-fail",
                                                   "wg.fail.example",
                                                   "pub-fail",
                                                   51820u,
                                                   allowed_ip,
                                                   allowed_mask));
    TEST_ASSERT_FALSE(hal_wireguard_is_initialized());
    TEST_ASSERT_FALSE(hal_mock_wireguard_was_begin_advanced());

    hal_mock_wireguard_set_begin_result(true);
    TEST_ASSERT_TRUE(hal_wireguard_begin_advanced(local_ip,
                                                  "priv-ok",
                                                  "wg.ok.example",
                                                  "pub-ok",
                                                  51820u,
                                                  allowed_ip,
                                                  allowed_mask));
    TEST_ASSERT_TRUE(hal_wireguard_is_initialized());
    TEST_ASSERT_TRUE(hal_mock_wireguard_was_begin_advanced());
}

void test_peer_up_output_variants_and_down_state(void) {
    const uint8_t local_ip[4] = {10u, 8u, 0u, 53u};
    const uint8_t peer_ip[4] = {198u, 51u, 100u, 20u};

    TEST_ASSERT_TRUE(hal_wireguard_begin(local_ip,
                                         "priv-peer",
                                         "wg.peer.example",
                                         "pub-peer",
                                         51820u));

    hal_mock_wireguard_set_peer_up_result(true);
    hal_mock_wireguard_set_peer_endpoint(peer_ip, 41000u);

    uint16_t endpoint_port = 0u;
    TEST_ASSERT_TRUE(hal_wireguard_peer_up(NULL, 0u, &endpoint_port));
    TEST_ASSERT_EQUAL_UINT16(41000u, endpoint_port);

    char endpoint_ip[HAL_WIREGUARD_IP_STR_LEN] = {0};
    TEST_ASSERT_TRUE(hal_wireguard_peer_up(endpoint_ip, sizeof(endpoint_ip), NULL));
    TEST_ASSERT_EQUAL_STRING("198.51.100.20", endpoint_ip);

    hal_mock_wireguard_set_peer_up_result(false);
    endpoint_port = 11111u;
    TEST_ASSERT_FALSE(hal_wireguard_peer_up(NULL, 0u, &endpoint_port));
    TEST_ASSERT_EQUAL_UINT16(11111u, endpoint_port);

    hal_wireguard_end();
    hal_mock_wireguard_set_peer_up_result(true);
    TEST_ASSERT_FALSE(hal_wireguard_peer_up(NULL, 0u, &endpoint_port));
}

void test_kick_handshake_requires_initialization_and_returns_driver_result(void) {
    const uint8_t local_ip[4] = {10u, 8u, 0u, 54u};
    const uint8_t probe_ip[4] = {1u, 1u, 1u, 1u};

    TEST_ASSERT_FALSE(hal_wireguard_kick_handshake(probe_ip, 5353u, 250u));

    TEST_ASSERT_TRUE(hal_wireguard_begin(local_ip,
                                         "priv-kick",
                                         "wg.kick.example",
                                         "pub-kick",
                                         51820u));

    hal_mock_wireguard_set_kick_result(false);
    TEST_ASSERT_FALSE(hal_wireguard_kick_handshake(probe_ip, 5353u, 250u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(probe_ip, hal_mock_wireguard_get_last_probe_ip(), 4);
    TEST_ASSERT_EQUAL_UINT16(5353u, hal_mock_wireguard_get_last_probe_port());
    TEST_ASSERT_EQUAL_UINT32(250u, hal_mock_wireguard_get_last_probe_min_interval_ms());

    hal_mock_wireguard_set_kick_result(true);
    TEST_ASSERT_TRUE(hal_wireguard_kick_handshake(probe_ip, 5353u, 250u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_and_end_full_tunnel_flow);
    RUN_TEST(test_begin_advanced_peer_up_and_kick);
    RUN_TEST(test_invalid_inputs_are_rejected);
    RUN_TEST(test_begin_failure_does_not_initialize_and_recovery_works);
    RUN_TEST(test_peer_up_output_variants_and_down_state);
    RUN_TEST(test_kick_handshake_requires_initialization_and_returns_driver_result);
    return UNITY_END();
}
