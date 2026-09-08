#include "hal/impl/stm32g474/stm32g474_adc_shared.h"
#include "utils/unity.h"

void setUp(void) { stm32g474_adc_release_dma(); }
void tearDown(void) { stm32g474_adc_release_dma(); }

static void test_dma_reservation_is_exclusive_and_reusable(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, stm32g474_adc_acquire_dma());
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, stm32g474_adc_acquire_dma());
  TEST_ASSERT_EQUAL_INT(0, stm32g474_adc_read_gpio(0u));
  TEST_ASSERT_EQUAL_UINT16(0u, stm32g474_adc_read_temp_sensor_raw());
  TEST_ASSERT_EQUAL_UINT16(0u, stm32g474_adc_read_vrefint_raw());
  uint16_t temp_raw = 123u;
  uint16_t vref_raw = 456u;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        stm32g474_adc_read_internal_pair(&temp_raw, &vref_raw));
  TEST_ASSERT_EQUAL_UINT16(123u, temp_raw);
  TEST_ASSERT_EQUAL_UINT16(456u, vref_raw);

  stm32g474_adc_release_dma();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        stm32g474_adc_read_internal_pair(&temp_raw, &vref_raw));
  TEST_ASSERT_EQUAL_UINT16(0u, temp_raw);
  TEST_ASSERT_EQUAL_UINT16(0u, vref_raw);
  TEST_ASSERT_EQUAL_INT(HAL_OK, stm32g474_adc_acquire_dma());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dma_reservation_is_exclusive_and_reusable);
  return UNITY_END();
}
