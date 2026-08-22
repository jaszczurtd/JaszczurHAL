#include "utils/unity.h"

#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "hal/network/hal_wifi.h"
#include "hal/network/jh_network_backend.h"
#include "hal/network/jh_network_runtime.h"
#include "hal/system/hal_board.h"
#include "hal/system/hal_sync.h"
#include "hal/system/jh_board_runtime.h"

#include <string.h>

struct hal_mutex_impl_t {
  bool locked;
};

namespace {

hal_mutex_impl_t s_mutexes[8]{};
size_t s_mutex_count;
bool s_fail_mutex_allocations;
unsigned s_wifi_calls;
unsigned s_tcp_open_calls;
unsigned s_tcp_connect_calls;
unsigned s_udp_open_calls;
unsigned s_udp_bind_calls;
unsigned s_resolve_calls;
hal_status_t s_join_status = HAL_OK;
int s_tcp_token;
int s_udp_token;
hal_tcp_socket_t s_tcp_handle;
hal_udp_socket_t s_udp_handle;
bool s_close_during_tcp_send;
bool s_close_during_udp_send;
bool s_tcp_send_active;
bool s_udp_send_active;
unsigned s_tcp_close_calls;
unsigned s_udp_close_calls;

hal_status_t service_ok(void) { return HAL_OK; }
hal_status_t stack_enter(bool) { return HAL_OK; }
void stack_leave(void) {}

hal_status_t wifi_set_mode(hal_wifi_mode_t mode) {
  ++s_wifi_calls;
  if (mode == HAL_WIFI_MODE_STA) {
    return jh_board_runtime_set_available(
        jh_network_required_board_capabilities());
  }
  return mode == HAL_WIFI_MODE_OFF ? HAL_OK : HAL_EUNSUPPORTED;
}

hal_status_t wifi_disconnect(bool) {
  ++s_wifi_calls;
  return HAL_OK;
}

hal_status_t wifi_set_hostname(const char *) {
  ++s_wifi_calls;
  return HAL_OK;
}

hal_status_t wifi_join(const char *, const char *, bool, uint32_t) {
  ++s_wifi_calls;
  if (s_join_status != HAL_OK) {
    (void)jh_board_runtime_set_failed(jh_network_required_board_capabilities());
    return s_join_status;
  }
  return jh_board_runtime_set_available(
      jh_network_required_board_capabilities());
}

hal_status_t wifi_get_state(hal_wifi_state_t *out_state) {
  ++s_wifi_calls;
  *out_state = HAL_WIFI_STATE_IDLE;
  return HAL_OK;
}

hal_status_t wifi_get_address(hal_net_endpoint_t *out_address) {
  ++s_wifi_calls;
  memset(out_address, 0, sizeof(*out_address));
  out_address->family = HAL_NET_AF_INET;
  out_address->addr_len = HAL_NET_IPV4_ADDR_LEN;
  out_address->addr[0] = 192u;
  out_address->addr[1] = 0u;
  out_address->addr[2] = 2u;
  out_address->addr[3] = 10u;
  return HAL_OK;
}

hal_status_t wifi_get_mac(uint8_t out_mac[HAL_WIFI_BSSID_LEN]) {
  ++s_wifi_calls;
  memset(out_mac, 0, HAL_WIFI_BSSID_LEN);
  return HAL_OK;
}

hal_status_t wifi_get_rssi(int32_t *out_rssi) {
  ++s_wifi_calls;
  *out_rssi = -50;
  return HAL_OK;
}

hal_status_t wifi_ping(const hal_net_endpoint_t *, uint32_t, int *out_result) {
  ++s_wifi_calls;
  *out_result = 1;
  return HAL_OK;
}

hal_status_t wifi_scan(uint32_t, int *out_count) {
  ++s_wifi_calls;
  *out_count = 0;
  return HAL_OK;
}

hal_status_t wifi_scan_result(size_t, hal_wifi_scan_result_t *) {
  ++s_wifi_calls;
  return HAL_ENOENT;
}

hal_status_t resolve(const char *, hal_net_family_t,
                     hal_net_endpoint_t *results, size_t capacity,
                     size_t *out_count) {
  ++s_resolve_calls;
  *out_count = 1u;
  if (capacity == 0u) {
    return HAL_EOVERFLOW;
  }
  return wifi_get_address(&results[0]);
}

hal_status_t tcp_open(void **out_socket) {
  ++s_tcp_open_calls;
  *out_socket = &s_tcp_token;
  return HAL_OK;
}

hal_status_t tcp_ok(void *, const hal_net_endpoint_t *, uint32_t) {
  ++s_tcp_connect_calls;
  return HAL_OK;
}

hal_status_t tcp_send(void *, const void *, size_t len, size_t *out_sent) {
  s_tcp_send_active = true;
  if (s_close_during_tcp_send) {
    hal_tcp_socket_close(s_tcp_handle);
    TEST_ASSERT_EQUAL_UINT32(0u, s_tcp_close_calls);
  }
  s_tcp_send_active = false;
  *out_sent = len;
  return HAL_OK;
}

hal_status_t tcp_recv(void *, void *, size_t, uint32_t, size_t *out_received) {
  *out_received = 0u;
  return HAL_EAGAIN;
}

bool socket_true(void *) { return true; }
void socket_noop(void *) {}

void tcp_close(void *) {
  TEST_ASSERT_FALSE(s_tcp_send_active);
  ++s_tcp_close_calls;
}

void udp_close(void *) {
  TEST_ASSERT_FALSE(s_udp_send_active);
  ++s_udp_close_calls;
}

hal_status_t listener_open(void **out_listener) {
  *out_listener = &s_tcp_token;
  return HAL_OK;
}

hal_status_t listener_bind(void *, const hal_net_endpoint_t *) {
  return HAL_OK;
}

hal_status_t listener_listen(void *, uint8_t) { return HAL_OK; }

hal_status_t listener_accept(void *, hal_net_endpoint_t *, uint32_t,
                             void **out_socket) {
  *out_socket = &s_tcp_token;
  return HAL_OK;
}

hal_status_t udp_open(void **out_socket) {
  ++s_udp_open_calls;
  *out_socket = &s_udp_token;
  return HAL_OK;
}

hal_status_t udp_bind(void *, const hal_net_endpoint_t *) {
  ++s_udp_bind_calls;
  return HAL_OK;
}

hal_status_t udp_send(void *, const void *, size_t len,
                      const hal_net_endpoint_t *, size_t *out_sent) {
  s_udp_send_active = true;
  if (s_close_during_udp_send) {
    hal_udp_socket_close(s_udp_handle);
    TEST_ASSERT_EQUAL_UINT32(0u, s_udp_close_calls);
  }
  s_udp_send_active = false;
  *out_sent = len;
  return HAL_OK;
}

hal_status_t udp_recv(void *, void *, size_t, hal_net_endpoint_t *, uint32_t,
                      size_t *out_received) {
  *out_received = 0u;
  return HAL_EAGAIN;
}

const jh_network_service_ops_t kServiceOps = {
    service_ok, service_ok, service_ok, stack_enter, stack_leave,
};

const jh_network_wifi_ops_t kWifiOps = {
    wifi_set_mode,  wifi_disconnect,  wifi_set_hostname, wifi_join,
    wifi_get_state, wifi_get_address, wifi_get_address,  wifi_get_mac,
    wifi_get_rssi,  wifi_ping,        wifi_scan,         wifi_scan_result,
};

const jh_network_resolver_ops_t kResolverOps = {resolve};

const jh_network_tcp_ops_t kTcpOps = {
    tcp_open,      tcp_ok,          tcp_send,        tcp_recv,    socket_true,
    socket_true,   socket_true,     socket_noop,     tcp_close,   listener_open,
    listener_bind, listener_listen, listener_accept, socket_true, socket_noop,
};

const jh_network_udp_ops_t kUdpOps = {
    udp_open, udp_bind, udp_send, udp_recv, socket_true, socket_true, udp_close,
};

} // namespace

hal_mutex_t hal_mutex_create(void) {
  if (s_fail_mutex_allocations) {
    return nullptr;
  }
  TEST_ASSERT_LESS_THAN(sizeof(s_mutexes) / sizeof(s_mutexes[0]),
                        s_mutex_count);
  return &s_mutexes[s_mutex_count++];
}

void hal_mutex_lock(hal_mutex_t mutex) {
  TEST_ASSERT_NOT_NULL(mutex);
  TEST_ASSERT_FALSE(mutex->locked);
  mutex->locked = true;
}

void hal_mutex_unlock(hal_mutex_t mutex) {
  TEST_ASSERT_NOT_NULL(mutex);
  TEST_ASSERT_TRUE(mutex->locked);
  mutex->locked = false;
}

void hal_mutex_destroy(hal_mutex_t) {}
void hal_critical_section_enter(void) {}
void hal_critical_section_exit(void) {}

extern "C" const jh_network_backend_descriptor_t *
jh_network_backend_selected(void) {
  static const jh_network_backend_descriptor_t backend = {
      JH_NETWORK_BACKEND_ABI_VERSION,
      "runtime-test",
      JH_NET_CAP_WIFI_STA | JH_NET_CAP_WIFI_SCAN | JH_NET_CAP_DNS |
          JH_NET_CAP_PING | JH_NET_CAP_TCP_CLIENT | JH_NET_CAP_TCP_LISTENER |
          JH_NET_CAP_UDP | JH_NET_CAP_IPV4,
      JH_NETWORK_EXECUTION_POLL,
      &kServiceOps,
      &kWifiOps,
      &kResolverOps,
      &kTcpOps,
      &kUdpOps,
  };
  return &backend;
}

void setUp(void) {
  s_fail_mutex_allocations = false;
  for (auto &mutex : s_mutexes) {
    mutex.locked = false;
  }
  s_wifi_calls = 0u;
  s_tcp_open_calls = 0u;
  s_tcp_connect_calls = 0u;
  s_udp_open_calls = 0u;
  s_udp_bind_calls = 0u;
  s_resolve_calls = 0u;
  s_join_status = HAL_OK;
  s_tcp_handle = nullptr;
  s_udp_handle = nullptr;
  s_close_during_tcp_send = false;
  s_close_during_udp_send = false;
  s_tcp_send_active = false;
  s_udp_send_active = false;
  s_tcp_close_calls = 0u;
  s_udp_close_calls = 0u;
  const hal_board_capabilities_t declared =
      HAL_BOARD_DECLARED_CAPABILITIES &
      (HAL_BOARD_CAP_CYW43 | HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND);
  if (declared != 0u) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_board_runtime_set_inactive(declared));
  }
}

void tearDown(void) {}

void test_network_facades_fail_closed_when_mutex_allocation_fails(void) {
#if defined(JH_TEST_NETWORK_BOARD_ABSENT)
  TEST_IGNORE_MESSAGE("The profile has no network hardware to mark available");
#else
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_board_runtime_set_available(
                                    jh_network_required_board_capabilities()));
  s_fail_mutex_allocations = true;

  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_wifi_set_timeout_ms_ex(100u));
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(-1, hal_wifi_ping("192.0.2.1"));

  hal_tcp_socket_t tcp = nullptr;
  hal_udp_socket_t udp = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_tcp_socket_open_ex(&tcp));
  TEST_ASSERT_NULL(tcp);
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_udp_socket_open_ex(&udp));
  TEST_ASSERT_NULL(udp);
  TEST_ASSERT_EQUAL_UINT32(0u, s_wifi_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_tcp_open_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_udp_open_calls);
#endif
}

void test_profile_and_runtime_state_gate_public_network_api(void) {
  hal_wifi_state_t wifi_state = HAL_WIFI_STATE_FAILED;
  hal_tcp_socket_t tcp = nullptr;
  hal_udp_socket_t udp = nullptr;
  hal_net_endpoint_t result{};
  size_t count = 0u;

#if defined(JH_TEST_NETWORK_BOARD_ABSENT)
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_wifi_get_state_ex(&wifi_state));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_tcp_socket_open_ex(&tcp));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_udp_socket_open_ex(&udp));
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      hal_net_resolve_ex("host.test", HAL_NET_AF_INET, &result, 1u, &count));
  TEST_ASSERT_EQUAL_UINT32(0u, s_wifi_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_tcp_open_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_udp_open_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_resolve_calls);
#else
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_wifi_get_state_ex(&wifi_state));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_tcp_socket_open_ex(&tcp));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_udp_socket_open_ex(&udp));
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNINIT,
      hal_net_resolve_ex("host.test", HAL_NET_AF_INET, &result, 1u, &count));
  TEST_ASSERT_EQUAL_UINT32(0u, s_wifi_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_tcp_open_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_udp_open_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_resolve_calls);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wifi_get_state_ex(&wifi_state));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&tcp));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&udp));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_net_resolve_ex("host.test", HAL_NET_AF_INET,
                                                   &result, 1u, &count));
  TEST_ASSERT_EQUAL_size_t(1u, count);
  hal_tcp_socket_close(tcp);
  hal_udp_socket_close(udp);
#endif
}

void test_failed_probe_is_sticky_until_runtime_is_reset(void) {
#if defined(JH_TEST_NETWORK_BOARD_ABSENT)
  TEST_IGNORE_MESSAGE("The profile has no radio to probe");
#else
  hal_wifi_state_t state = HAL_WIFI_STATE_IDLE;
  s_join_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  const unsigned calls_after_failure = s_wifi_calls;
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_wifi_get_state_ex(&state));
  TEST_ASSERT_EQUAL_INT(HAL_EHW,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_UINT32(calls_after_failure, s_wifi_calls);
#endif
}

void test_pim730_requires_the_external_radio_frontend(void) {
#if defined(JH_TEST_NETWORK_BOARD_PIM730)
  hal_wifi_state_t state = HAL_WIFI_STATE_IDLE;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_board_runtime_set_available(HAL_BOARD_CAP_CYW43));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_wifi_get_state_ex(&state));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_board_runtime_set_failed(
                                    HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND));
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_wifi_get_state_ex(&state));
#else
  TEST_IGNORE_MESSAGE("Only the PIM730 profile has an external radio frontend");
#endif
}

void test_failed_hardware_status_precedes_endpoint_capability_checks(void) {
#if defined(JH_TEST_NETWORK_BOARD_ABSENT)
  TEST_IGNORE_MESSAGE("The profile has no radio to transition to failed");
#else
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "password", false));

  hal_tcp_socket_t tcp = nullptr;
  hal_udp_socket_t udp = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&tcp));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&udp));

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_board_runtime_set_failed(
                                    jh_network_required_board_capabilities()));

  hal_net_endpoint_t endpoint{};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr_len = HAL_NET_IPV4_ADDR_LEN;
  endpoint.addr[0] = 192u;
  endpoint.addr[1] = 0u;
  endpoint.addr[2] = 2u;
  endpoint.addr[3] = 1u;
  endpoint.port = 1234u;

  TEST_ASSERT_EQUAL_INT(HAL_EHW,
                        hal_tcp_socket_connect_ex(tcp, &endpoint, 100u));
  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_udp_socket_bind_ex(udp, &endpoint));
  TEST_ASSERT_EQUAL_UINT32(0u, s_tcp_connect_calls);
  TEST_ASSERT_EQUAL_UINT32(0u, s_udp_bind_calls);

  hal_tcp_socket_close(tcp);
  hal_udp_socket_close(udp);
#endif
}

void test_numeric_ipv4_parsing_does_not_require_radio_access(void) {
  hal_net_endpoint_t result{};
  size_t count = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      hal_net_resolve_ex("192.0.2.44", HAL_NET_AF_INET, &result, 1u, &count));
  TEST_ASSERT_EQUAL_size_t(1u, count);
  TEST_ASSERT_EQUAL_UINT8(44u, result.addr[3]);
  TEST_ASSERT_EQUAL_UINT32(0u, s_resolve_calls);
}

void test_close_during_tcp_operation_is_deferred_until_backend_returns(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&s_tcp_handle));
  s_close_during_tcp_send = true;
  size_t sent = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tcp_socket_send_ex(s_tcp_handle, "x", 1u, &sent));
  TEST_ASSERT_EQUAL_size_t(1u, sent);
  TEST_ASSERT_EQUAL_UINT32(1u, s_tcp_close_calls);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tcp_socket_send_ex(s_tcp_handle, "x", 1u, &sent));
}

void test_close_during_udp_operation_is_deferred_until_backend_returns(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&s_udp_handle));
  s_close_during_udp_send = true;
  hal_net_endpoint_t remote{};
  remote.family = HAL_NET_AF_INET;
  remote.addr_len = HAL_NET_IPV4_ADDR_LEN;
  remote.addr[0] = 192u;
  remote.addr[1] = 0u;
  remote.addr[2] = 2u;
  remote.addr[3] = 1u;
  remote.port = 1234u;
  size_t sent = 0u;

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_udp_socket_sendto_ex(s_udp_handle, "x", 1u, &remote, &sent));
  TEST_ASSERT_EQUAL_size_t(1u, sent);
  TEST_ASSERT_EQUAL_UINT32(1u, s_udp_close_calls);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_udp_socket_sendto_ex(
                                        s_udp_handle, "x", 1u, &remote, &sent));
}

void test_wifi_mode_off_invalidates_transport_handles(void) {
#if defined(JH_TEST_NETWORK_BOARD_ABSENT)
  TEST_IGNORE_MESSAGE("The profile has no radio transport handles");
#else
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&s_tcp_handle));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&s_udp_handle));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_wifi_set_mode_ex(HAL_WIFI_MODE_OFF));
  TEST_ASSERT_EQUAL_UINT32(1u, s_tcp_close_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, s_udp_close_calls);

  size_t transferred = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_tcp_socket_send_ex(s_tcp_handle, "x", 1u, &transferred));
  hal_net_endpoint_t remote{};
  remote.family = HAL_NET_AF_INET;
  remote.addr_len = HAL_NET_IPV4_ADDR_LEN;
  remote.addr[0] = 192u;
  remote.addr[1] = 0u;
  remote.addr[2] = 2u;
  remote.addr[3] = 1u;
  remote.port = 1234u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_udp_socket_sendto_ex(s_udp_handle, "x", 1u, &remote, &transferred));
#endif
}

void test_failed_wifi_rejoin_invalidates_transport_handles(void) {
#if defined(JH_TEST_NETWORK_BOARD_ABSENT)
  TEST_IGNORE_MESSAGE("The profile has no radio transport handles");
#else
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_wifi_begin_station_ex("ssid", "password", false));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tcp_socket_open_ex(&s_tcp_handle));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_udp_socket_open_ex(&s_udp_handle));

  s_join_status = HAL_ETIMEOUT;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, hal_wifi_begin_station_ex(
                                          "replacement", "password", false));
  TEST_ASSERT_EQUAL_UINT32(1u, s_tcp_close_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, s_udp_close_calls);

  size_t transferred = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EHW, hal_tcp_socket_send_ex(s_tcp_handle, "x", 1u, &transferred));
  hal_net_endpoint_t remote{};
  remote.family = HAL_NET_AF_INET;
  remote.addr_len = HAL_NET_IPV4_ADDR_LEN;
  remote.addr[0] = 192u;
  remote.addr[1] = 0u;
  remote.addr[2] = 2u;
  remote.addr[3] = 1u;
  remote.port = 1234u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EHW,
      hal_udp_socket_sendto_ex(s_udp_handle, "x", 1u, &remote, &transferred));
#endif
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_network_facades_fail_closed_when_mutex_allocation_fails);
  RUN_TEST(test_profile_and_runtime_state_gate_public_network_api);
  RUN_TEST(test_failed_probe_is_sticky_until_runtime_is_reset);
  RUN_TEST(test_pim730_requires_the_external_radio_frontend);
  RUN_TEST(test_failed_hardware_status_precedes_endpoint_capability_checks);
  RUN_TEST(test_numeric_ipv4_parsing_does_not_require_radio_access);
  RUN_TEST(test_close_during_tcp_operation_is_deferred_until_backend_returns);
  RUN_TEST(test_close_during_udp_operation_is_deferred_until_backend_returns);
  RUN_TEST(test_wifi_mode_off_invalidates_transport_handles);
  RUN_TEST(test_failed_wifi_rejoin_invalidates_transport_handles);
  return UNITY_END();
}
