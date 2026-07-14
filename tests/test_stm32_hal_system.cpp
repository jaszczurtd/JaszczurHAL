#include "hal/hal_system.h"
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "utils/unity.h"

extern "C" void hal_stm32g474_fault_test_reset(void);
extern "C" void hal_stm32g474_fault_test_set_rcc_csr(uint32_t csr);
extern "C" void hal_stm32g474_fault_test_set_fault_frame(uint32_t pc,
                                                         uint32_t lr,
                                                         uint32_t psr);
extern "C" void hal_stm32g474_fault_test_set_alive_marker(bool marked);
extern "C" void hal_stm32g474_fault_test_set_stack_overflow_marker(bool set);

void setUp(void) { hal_stm32g474_fault_test_reset(); }

void tearDown(void) {}

void test_stm32_reset_reason_soft_reset_maps_to_soft(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_SOFT, (int)hal_get_reset_reason());
  TEST_ASSERT_FALSE(hal_last_boot_was_brownout());
}

void test_stm32_reset_reason_watchdog_flags_map_to_watchdog(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_IWDGRSTF);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_WATCHDOG, (int)hal_get_reset_reason());
}

void test_stm32_reset_reason_bor_pin_without_alive_maps_to_power_on(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_BORRSTF | RCC_CSR_PINRSTF);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_POWER_ON, (int)hal_get_reset_reason());
  TEST_ASSERT_FALSE(hal_last_boot_was_brownout());
}

void test_stm32_reset_reason_bor_with_alive_maps_to_brownout(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_BORRSTF | RCC_CSR_PINRSTF);
  hal_stm32g474_fault_test_set_alive_marker(true);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_BROWNOUT, (int)hal_get_reset_reason());
  TEST_ASSERT_TRUE(hal_last_boot_was_brownout());
}

void test_stm32_fault_frame_overrides_rcc_reason(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);
  hal_stm32g474_fault_test_set_fault_frame(0x08001234u, 0xDEADBEEFu,
                                           0x21000000u);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_HARDFAULT,
                        (int)hal_get_reset_reason());

  hal_fault_info_t info = {false, 0u, 0u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_get_last_fault_ex(&info));
  TEST_ASSERT_TRUE(hal_get_last_fault(&info));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_HEX32(0x08001234u, info.pc);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, info.lr);
  TEST_ASSERT_EQUAL_HEX32(0x21000000u, info.psr);

  hal_clear_last_fault();
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_get_last_fault_ex(&info));
  TEST_ASSERT_FALSE(hal_get_last_fault(&info));
}

void test_stm32_stack_overflow_marker_overrides_reason(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);
  hal_stm32g474_fault_test_set_stack_overflow_marker(true);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_STACK_OVERFLOW,
                        (int)hal_get_reset_reason());

  hal_fault_info_t info = {false, 0u, 0u, 0u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_get_last_fault_ex(&info));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_HEX32(0xDEADD000u, info.pc);
}

void test_stm32_system_status_reports_unsupported_services(void) {
  float temperature = 123.0f;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_read_chip_temp_ex(&temperature));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 123.0f, temperature);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_read_chip_temp_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_watchdog_enable(1000u, false));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_enter_bootloader());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stm32_reset_reason_soft_reset_maps_to_soft);
  RUN_TEST(test_stm32_reset_reason_watchdog_flags_map_to_watchdog);
  RUN_TEST(test_stm32_reset_reason_bor_pin_without_alive_maps_to_power_on);
  RUN_TEST(test_stm32_reset_reason_bor_with_alive_maps_to_brownout);
  RUN_TEST(test_stm32_fault_frame_overrides_rcc_reason);
  RUN_TEST(test_stm32_stack_overflow_marker_overrides_reason);
  RUN_TEST(test_stm32_system_status_reports_unsupported_services);
  return UNITY_END();
}
