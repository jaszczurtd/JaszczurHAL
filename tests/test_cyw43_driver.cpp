#include "utils/unity.h"

#include <hal/impl/shared/drivers/cyw43-driver/jh_cyw43_driver.h>

void setUp(void) {}
void tearDown(void) {}

static void test_stage_vocabulary_is_bounded(void) {
  TEST_ASSERT_EQUAL_STRING(
      "none", jh_cyw43_driver_stage_string(JH_CYW43_DRIVER_STAGE_NONE));
  TEST_ASSERT_EQUAL_STRING("low-level", jh_cyw43_driver_stage_string(
                                            JH_CYW43_DRIVER_STAGE_LOW_LEVEL));
  TEST_ASSERT_EQUAL_STRING(
      "bus", jh_cyw43_driver_stage_string(JH_CYW43_DRIVER_STAGE_BUS));
  TEST_ASSERT_EQUAL_STRING(
      "ready", jh_cyw43_driver_stage_string(JH_CYW43_DRIVER_STAGE_READY));
}

static void test_non_stm32_build_has_no_driver_instance(void) {
  jh_cyw43_driver_result_t result{};
  uint8_t mac[6] = {0x02u, 0u, 0u, 0u, 0u, 1u};

  TEST_ASSERT_FALSE(jh_cyw43_driver_is_ready());
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_cyw43_driver_start(nullptr, mac, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_driver_restart(&result));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_cyw43_driver_stop());
  TEST_ASSERT_FALSE(jh_cyw43_driver_is_ready());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stage_vocabulary_is_bounded);
  RUN_TEST(test_non_stm32_build_has_no_driver_instance);
  return UNITY_END();
}
