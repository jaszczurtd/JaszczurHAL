#include "utils/unity.h"

#include "hal/hal_config.h"

#ifndef HAL_ENABLE_SDLOGGER
#error "test_hal_config_storage_flags must be built with HAL_ENABLE_SDLOGGER"
#endif

#ifndef HAL_ENABLE_FAT
#error "HAL_ENABLE_SDLOGGER must propagate HAL_ENABLE_FAT"
#endif

#ifndef HAL_ENABLE_EEPROM
#error "HAL_ENABLE_SDLOGGER must propagate HAL_ENABLE_EEPROM"
#endif

#ifndef HAL_ENABLE_SPI
#error "HAL_ENABLE_SDLOGGER must propagate HAL_ENABLE_SPI"
#endif

#if HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256 && !defined(HAL_ENABLE_I2C)
#error "AT24C256 EEPROM must propagate HAL_ENABLE_I2C"
#endif

void setUp(void) {}
void tearDown(void) {}

void test_storage_flag_propagation_compiles(void) { TEST_ASSERT_TRUE(true); }

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_storage_flag_propagation_compiles);
  return UNITY_END();
}
