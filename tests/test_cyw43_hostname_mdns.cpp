#include "hal/network/cyw43/jh_cyw43_hostname.h"
#include "hal/network/cyw43/jh_cyw43_mdns.h"
#include "lwip/apps/mdns.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "utils/unity.h"

#include <cstring>

namespace {

bool s_dhcp_supplied;
err_t s_dhcp_renew_status;
size_t s_dhcp_renew_calls;
const char *s_netif_hostname;

bool s_mdns_active;
struct netif *s_mdns_netif;
err_t s_mdns_add_status;
err_t s_mdns_remove_status;
err_t s_mdns_rename_status;
size_t s_mdns_init_calls;
size_t s_mdns_add_calls;
size_t s_mdns_remove_calls;
size_t s_mdns_rename_calls;
char s_mdns_hostname[MDNS_LABEL_MAXLEN + 1u];

void copy_mdns_hostname(const char *hostname) {
  std::strncpy(s_mdns_hostname, hostname, sizeof(s_mdns_hostname) - 1u);
  s_mdns_hostname[sizeof(s_mdns_hostname) - 1u] = '\0';
}

} // namespace

extern "C" {

void netif_set_hostname(struct netif *, const char *hostname) {
  s_netif_hostname = hostname;
}

uint8_t dhcp_supplied_address(const struct netif *) {
  return s_dhcp_supplied ? 1u : 0u;
}

err_t dhcp_renew(struct netif *) {
  ++s_dhcp_renew_calls;
  return s_dhcp_renew_status;
}

void mdns_resp_init(void) { ++s_mdns_init_calls; }

err_t mdns_resp_add_netif(struct netif *netif, const char *hostname) {
  ++s_mdns_add_calls;
  copy_mdns_hostname(hostname);
  if (s_mdns_add_status == ERR_OK) {
    s_mdns_active = true;
    s_mdns_netif = netif;
  }
  return s_mdns_add_status;
}

err_t mdns_resp_remove_netif(struct netif *netif) {
  ++s_mdns_remove_calls;
  if (s_mdns_remove_status == ERR_OK && s_mdns_netif == netif) {
    s_mdns_active = false;
    s_mdns_netif = nullptr;
  }
  return s_mdns_remove_status;
}

err_t mdns_resp_rename_netif(struct netif *, const char *hostname) {
  ++s_mdns_rename_calls;
  copy_mdns_hostname(hostname);
  return s_mdns_rename_status;
}

int mdns_resp_netif_active(struct netif *netif) {
  return s_mdns_active && s_mdns_netif == netif ? 1 : 0;
}

} // extern "C"

void setUp(void) {}
void tearDown(void) {}

static void test_hostname_renews_only_an_active_dhcp_lease(void) {
  struct netif interface {};
  s_dhcp_supplied = false;
  s_dhcp_renew_status = ERR_OK;
  s_dhcp_renew_calls = 0u;
  s_netif_hostname = nullptr;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_cyw43_hostname_apply(nullptr, "timer-ntp"));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_cyw43_hostname_apply(&interface, "timer-ntp"));
  TEST_ASSERT_EQUAL_STRING("timer-ntp", s_netif_hostname);
  TEST_ASSERT_EQUAL_UINT32(0u, s_dhcp_renew_calls);

  s_dhcp_supplied = true;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_cyw43_hostname_apply(&interface, "timer-ntp-2"));
  TEST_ASSERT_EQUAL_STRING("timer-ntp-2", s_netif_hostname);
  TEST_ASSERT_EQUAL_UINT32(1u, s_dhcp_renew_calls);

  s_dhcp_renew_status = ERR_MEM;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        jh_cyw43_hostname_apply(&interface, "timer-ntp-3"));
}

static void test_mdns_initializes_once_and_tracks_the_netif(void) {
  struct netif first {};
  struct netif second {};
  s_mdns_active = false;
  s_mdns_netif = nullptr;
  s_mdns_add_status = ERR_OK;
  s_mdns_remove_status = ERR_OK;
  s_mdns_rename_status = ERR_OK;
  s_mdns_init_calls = 0u;
  s_mdns_add_calls = 0u;
  s_mdns_remove_calls = 0u;
  s_mdns_rename_calls = 0u;
  s_mdns_hostname[0] = '\0';

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_cyw43_mdns_publish(nullptr, "timer-ntp"));
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      jh_cyw43_mdns_publish(
          &first,
          "1234567890123456789012345678901234567890123456789012345678901234"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_mdns_publish(&first, "timer-ntp"));
  TEST_ASSERT_EQUAL_UINT32(1u, s_mdns_init_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, s_mdns_add_calls);
  TEST_ASSERT_EQUAL_STRING("timer-ntp", s_mdns_hostname);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_mdns_publish(&first, "timer-ntp-2"));
  TEST_ASSERT_EQUAL_UINT32(1u, s_mdns_init_calls);
  TEST_ASSERT_EQUAL_UINT32(1u, s_mdns_rename_calls);
  TEST_ASSERT_EQUAL_STRING("timer-ntp-2", s_mdns_hostname);

  s_mdns_add_status = ERR_MEM;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, jh_cyw43_mdns_publish(&second, "second"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_mdns_remove(&second));
  TEST_ASSERT_EQUAL_UINT32(0u, s_mdns_remove_calls);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_mdns_remove(&first));
  TEST_ASSERT_EQUAL_UINT32(1u, s_mdns_remove_calls);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_cyw43_mdns_remove(&first));
  TEST_ASSERT_EQUAL_UINT32(1u, s_mdns_remove_calls);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_hostname_renews_only_an_active_dhcp_lease);
  RUN_TEST(test_mdns_initializes_once_and_tracks_the_netif);
  return UNITY_END();
}
