#include "utils/unity.h"
#include "hal/hal_wifi.h"
#include "hal/impl/.mock/hal_mock.h"
#include <string.h>

void setUp(void) {
    hal_mock_serial_reset();
    hal_mock_wifi_reset();
}

void tearDown(void) {}

void test_connectivity_and_status(void) {
    TEST_ASSERT_TRUE(hal_wifi_set_mode(HAL_WIFI_MODE_STA));
    hal_mock_wifi_set_connected(true);
    hal_mock_wifi_set_status(3);

    TEST_ASSERT_TRUE(hal_wifi_is_connected());
    TEST_ASSERT_EQUAL_INT(3, hal_wifi_status());
}

void test_getters_return_injected_values(void) {
    char ip[32] = {};
    char dns[32] = {};
    char mac[32] = {};

    hal_mock_wifi_set_local_ip("192.168.1.42");
    hal_mock_wifi_set_dns_ip("1.1.1.1");
    hal_mock_wifi_set_mac("AA:BB:CC:DD:EE:FF");
    hal_mock_wifi_set_rssi(-57);

    TEST_ASSERT_TRUE(hal_wifi_get_local_ip(ip, sizeof(ip)));
    TEST_ASSERT_TRUE(hal_wifi_get_dns_ip(dns, sizeof(dns)));
    TEST_ASSERT_TRUE(hal_wifi_get_mac(mac, sizeof(mac)));
    TEST_ASSERT_EQUAL_STRING("192.168.1.42", ip);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", dns);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", mac);
    TEST_ASSERT_EQUAL_INT32(-57, hal_wifi_rssi());
    TEST_ASSERT_EQUAL_INT(4, hal_wifi_get_strength());
    TEST_ASSERT_TRUE(hal_wifi_has_local_ip());
}

void test_wifi_mode_validation_and_strength_floor(void) {
    TEST_ASSERT_FALSE(hal_wifi_set_mode((hal_wifi_mode_t)99));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_wifi_set_rssi(-120);
    TEST_ASSERT_EQUAL_INT(0, hal_wifi_get_strength());
}

void test_invalid_output_buffer_is_rejected(void) {
    TEST_ASSERT_FALSE(hal_wifi_get_local_ip(NULL, 16));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_serial_reset();
    char out[8];
    TEST_ASSERT_FALSE(hal_wifi_get_local_ip(out, 0));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_hostname_timeout_ping_and_disconnect(void) {
    TEST_ASSERT_TRUE(hal_wifi_set_hostname("node-1"));
    TEST_ASSERT_EQUAL_STRING("node-1", hal_mock_wifi_get_hostname());

    TEST_ASSERT_TRUE(hal_wifi_set_timeout_ms(1234));
    TEST_ASSERT_EQUAL_UINT32(1234, hal_mock_wifi_get_timeout_ms());

    hal_mock_wifi_set_ping_result(17);
    TEST_ASSERT_EQUAL_INT(17, hal_wifi_ping("8.8.8.8"));

    hal_mock_wifi_set_connected(true);
    hal_mock_wifi_set_local_ip("10.0.0.5");
    TEST_ASSERT_TRUE(hal_wifi_disconnect(true));
    TEST_ASSERT_FALSE(hal_wifi_is_connected());
    TEST_ASSERT_FALSE(hal_wifi_has_local_ip());
}

void test_invalid_begin_and_ping_inputs(void) {
    TEST_ASSERT_FALSE(hal_wifi_begin_station(NULL, "pass", true));
    TEST_ASSERT_FALSE(hal_wifi_begin_station("ssid", NULL, true));
    TEST_ASSERT_EQUAL_INT(-1, hal_wifi_ping(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_connectivity_and_status);
    RUN_TEST(test_getters_return_injected_values);
    RUN_TEST(test_wifi_mode_validation_and_strength_floor);
    RUN_TEST(test_invalid_output_buffer_is_rejected);
    RUN_TEST(test_hostname_timeout_ping_and_disconnect);
    RUN_TEST(test_invalid_begin_and_ping_inputs);
    return UNITY_END();
}
