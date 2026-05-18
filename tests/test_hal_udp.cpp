#include "utils/unity.h"
#include "hal/hal_udp.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

void setUp(void) {
    hal_mock_serial_reset();
    hal_mock_udp_reset();
}

void tearDown(void) {}

void test_begin_receive_and_remote_endpoint(void) {
    const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t out[8] = {0};
    char remote_ip[HAL_UDP_IP_STR_LEN] = {0};

    TEST_ASSERT_TRUE(hal_udp_begin(12345u));
    TEST_ASSERT_EQUAL_UINT16(12345u, hal_mock_udp_get_local_port());

    hal_mock_udp_inject_packet("192.168.1.50", 4444u, payload, (uint16_t)sizeof(payload));
    TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_parse_packet());

    TEST_ASSERT_TRUE(hal_udp_remote_ip(remote_ip, sizeof(remote_ip)));
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", remote_ip);
    TEST_ASSERT_EQUAL_UINT16(4444u, hal_udp_remote_port());

    TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_read(out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(0, hal_udp_parse_packet());
}

void test_send_to_explicit_host_collects_payload(void) {
    const uint8_t prefix[] = {0xAA, 0xBB};
    const uint8_t expected[] = {0xAA, 0xBB, 'O', 'K'};

    TEST_ASSERT_TRUE(hal_udp_begin(15000u));
    TEST_ASSERT_TRUE(hal_udp_begin_packet("10.0.0.12", 7777u));

    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(prefix), hal_udp_write(prefix, (uint16_t)sizeof(prefix)));
    TEST_ASSERT_EQUAL_UINT16(2u, hal_udp_write_str("OK"));

    TEST_ASSERT_TRUE(hal_udp_end_packet());
    TEST_ASSERT_TRUE(hal_mock_udp_was_end_packet_called());

    TEST_ASSERT_EQUAL_STRING("10.0.0.12", hal_mock_udp_get_last_begin_packet_host());
    TEST_ASSERT_EQUAL_UINT16(7777u, hal_mock_udp_get_last_begin_packet_port());
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(expected), hal_mock_udp_get_last_tx_len());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,
                                  hal_mock_udp_get_last_tx_payload(),
                                  sizeof(expected));
}

void test_send_to_last_remote_sender(void) {
    const uint8_t ping[] = {'p', 'i', 'n', 'g'};
    uint8_t discard[8] = {0};

    TEST_ASSERT_TRUE(hal_udp_begin(9000u));

    hal_mock_udp_inject_packet("172.16.0.9", 5050u, ping, (uint16_t)sizeof(ping));
    TEST_ASSERT_EQUAL_INT((int)sizeof(ping), hal_udp_parse_packet());
    TEST_ASSERT_EQUAL_INT((int)sizeof(ping), hal_udp_read(discard, sizeof(discard)));

    TEST_ASSERT_TRUE(hal_udp_begin_packet_remote());
    TEST_ASSERT_EQUAL_UINT16(4u, hal_udp_write_str("pong"));
    TEST_ASSERT_TRUE(hal_udp_end_packet());

    TEST_ASSERT_EQUAL_STRING("172.16.0.9", hal_mock_udp_get_last_begin_packet_host());
    TEST_ASSERT_EQUAL_UINT16(5050u, hal_mock_udp_get_last_begin_packet_port());
}

void test_read_in_chunks_consumes_packet(void) {
    const uint8_t payload[] = {'A', 'B', 'C', 'D', 'E'};
    uint8_t chunk_a[2] = {0};
    uint8_t chunk_b[8] = {0};

    TEST_ASSERT_TRUE(hal_udp_begin(9100u));
    hal_mock_udp_inject_packet("10.1.2.3", 6000u, payload, (uint16_t)sizeof(payload));

    TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_parse_packet());

    TEST_ASSERT_EQUAL_INT(2, hal_udp_read(chunk_a, (uint16_t)sizeof(chunk_a)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, chunk_a, sizeof(chunk_a));

    TEST_ASSERT_EQUAL_INT(3, hal_udp_read(chunk_b, (uint16_t)sizeof(chunk_b)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload + sizeof(chunk_a), chunk_b, 3);

    TEST_ASSERT_EQUAL_INT(0, hal_udp_read(chunk_b, (uint16_t)sizeof(chunk_b)));
    TEST_ASSERT_EQUAL_INT(0, hal_udp_parse_packet());
}

void test_stop_clears_remote_and_packet_state(void) {
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    char out_ip[HAL_UDP_IP_STR_LEN] = {0};

    TEST_ASSERT_TRUE(hal_udp_begin(9200u));

    hal_mock_udp_inject_packet("203.0.113.10", 6500u, payload, (uint16_t)sizeof(payload));
    TEST_ASSERT_EQUAL_INT((int)sizeof(payload), hal_udp_parse_packet());
    TEST_ASSERT_TRUE(hal_udp_remote_ip(out_ip, sizeof(out_ip)));
    TEST_ASSERT_EQUAL_UINT16(6500u, hal_udp_remote_port());

    TEST_ASSERT_TRUE(hal_udp_begin_packet("example.local", 7001u));
    TEST_ASSERT_EQUAL_UINT16(1u, hal_udp_write(payload, 1u));

    hal_udp_stop();

    memset(out_ip, 0, sizeof(out_ip));
    TEST_ASSERT_FALSE(hal_udp_remote_ip(out_ip, sizeof(out_ip)));
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", out_ip);
    TEST_ASSERT_EQUAL_UINT16(0u, hal_udp_remote_port());
    TEST_ASSERT_FALSE(hal_udp_begin_packet_remote());
    TEST_ASSERT_FALSE(hal_udp_end_packet());
}

void test_invalid_inputs_are_rejected(void) {
    char out_ip[HAL_UDP_IP_STR_LEN] = {0};

    TEST_ASSERT_FALSE(hal_udp_begin(0u));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    TEST_ASSERT_TRUE(hal_udp_begin(12000u));

    hal_mock_serial_reset();
    TEST_ASSERT_EQUAL_INT(-1, hal_udp_read(NULL, 1u));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_serial_reset();
    TEST_ASSERT_FALSE(hal_udp_remote_ip(NULL, sizeof(out_ip)));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    TEST_ASSERT_FALSE(hal_udp_remote_ip(out_ip, sizeof(out_ip)));
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", out_ip);

    hal_mock_serial_reset();
    TEST_ASSERT_FALSE(hal_udp_begin_packet(NULL, 7000u));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_serial_reset();
    TEST_ASSERT_FALSE(hal_udp_begin_packet("host", 0u));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_serial_reset();
    TEST_ASSERT_FALSE(hal_udp_begin_packet_remote());
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_serial_reset();
    TEST_ASSERT_EQUAL_UINT16(0u, hal_udp_write(NULL, 1u));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    hal_mock_serial_reset();
    TEST_ASSERT_EQUAL_UINT16(0u, hal_udp_write_str(NULL));
    TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);

    TEST_ASSERT_FALSE(hal_udp_end_packet());
}

void test_end_packet_failure_is_propagated(void) {
    const uint8_t data[] = {0xEF};

    TEST_ASSERT_TRUE(hal_udp_begin(13000u));
    TEST_ASSERT_TRUE(hal_udp_begin_packet("192.168.10.2", 6060u));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(data), hal_udp_write(data, (uint16_t)sizeof(data)));

    hal_mock_udp_set_end_packet_result(false);
    TEST_ASSERT_FALSE(hal_udp_end_packet());
    TEST_ASSERT_TRUE(hal_mock_udp_was_end_packet_called());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_receive_and_remote_endpoint);
    RUN_TEST(test_send_to_explicit_host_collects_payload);
    RUN_TEST(test_send_to_last_remote_sender);
    RUN_TEST(test_read_in_chunks_consumes_packet);
    RUN_TEST(test_stop_clears_remote_and_packet_state);
    RUN_TEST(test_invalid_inputs_are_rejected);
    RUN_TEST(test_end_packet_failure_is_propagated);
    return UNITY_END();
}
