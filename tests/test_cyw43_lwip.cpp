#include "utils/unity.h"

#include <hal/impl/shared/drivers/cyw43-driver/jh_cyw43_lwip.h>

void setUp(void) {}
void tearDown(void) {}

static void test_non_stm32_lwip_port_is_not_instantiated(void) {
  jh_cyw43_lwip_snapshot_t snapshot{};
  uint32_t address = 0u;
  int ttl = -1;
  uint32_t rtt = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_lwip_service());
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_cyw43_lwip_join_start("ssid", "password", 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_cyw43_lwip_join("ssid", "password", 0u, 1000u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_lwip_resolve_ipv4(
                                              "example.test", &address, 1000u));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_cyw43_lwip_ping_ipv4(1u, 1000u, &ttl, &rtt));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_lwip_leave());
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_cyw43_lwip_get_snapshot(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_cyw43_lwip_get_snapshot(&snapshot));
  TEST_ASSERT_FALSE(snapshot.initialized);
  TEST_ASSERT_FALSE(snapshot.netif_present);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_non_stm32_lwip_port_is_not_instantiated);
  return UNITY_END();
}
