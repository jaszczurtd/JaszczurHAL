#include "hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.h"
#include "hal/impl/stm32g474/port/stm32g474_fdcan_timing.h"
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "hal/impl/stm32g474/port/stm32g474_time.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

extern "C" void hal_stm32g474_fault_test_reset(void);
extern "C" void hal_stm32g474_fault_test_set_rcc_csr(uint32_t csr);
extern "C" void hal_stm32g474_fault_test_set_fault_frame(uint32_t pc,
                                                         uint32_t lr,
                                                         uint32_t psr);
extern "C" void hal_stm32g474_fault_test_set_alive_marker(bool marked);
extern "C" void hal_stm32g474_fault_test_set_stack_guard_fault(void);
extern "C" void
hal_stm32g474_fault_test_set_escalated_guard_fault(bool data_access_violation);
extern "C" void
hal_stm32g474_fault_test_set_stack_guard_stacking_fault(bool near_guard);
extern "C" void hal_stm32g474_fault_test_set_stacking_fault_at(
    uint32_t raw_sp, bool extended_frame, bool lazy_fp_fault);
extern "C" uint32_t hal_stm32g474_fault_test_stack_guard_base(void);
extern "C" void hal_stm32g474_fault_test_set_retained_stack_overflow(void);
extern "C" void hal_stm32g474_fault_test_corrupt_stack_guard(void);

void setUp(void) {
  hal_stm32g474_fault_test_reset();
  stm32g474_system_test_reset_watchdog();
}

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
  TEST_ASSERT_TRUE(hal_watchdog_caused_reboot());
}

void test_stm32_non_watchdog_reset_does_not_report_watchdog_reboot(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);

  hal_fault_subsystem_init();

  TEST_ASSERT_FALSE(hal_watchdog_caused_reboot());
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

  hal_fault_info_t info = {};
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

void test_stm32_stack_guard_fault_overrides_reason(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);
  hal_stm32g474_fault_test_set_stack_guard_fault();

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_STACK_OVERFLOW,
                        (int)hal_get_reset_reason());

  hal_fault_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_get_last_fault_ex(&info));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_HEX32(0x0800DEADu, info.pc);
}

void test_stm32_escalated_stack_guard_fault_is_still_overflow(void) {
  hal_stm32g474_fault_test_set_escalated_guard_fault(true);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_STACK_OVERFLOW,
                        (int)hal_get_reset_reason());
  hal_fault_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_get_last_fault_ex(&info));
  TEST_ASSERT_EQUAL_HEX32(SCB_HFSR_FORCED, info.hfsr);
}

void test_stm32_stale_mmfar_without_access_violation_is_not_overflow(void) {
  hal_stm32g474_fault_test_set_escalated_guard_fault(false);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_HARDFAULT,
                        (int)hal_get_reset_reason());
}

void test_stm32_stack_guard_stacking_fault_without_mmfar_is_detected(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);
  hal_stm32g474_fault_test_set_stack_guard_stacking_fault(true);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_STACK_OVERFLOW,
                        (int)hal_get_reset_reason());
  hal_fault_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_get_last_fault_ex(&info));
  TEST_ASSERT_EQUAL_HEX32(SCB_CFSR_MMFSR_MSTKERR, info.cfsr);
}

void test_stm32_unrelated_stacking_fault_remains_hardfault(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_SFTRSTF);
  hal_stm32g474_fault_test_set_stack_guard_stacking_fault(false);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_HARDFAULT,
                        (int)hal_get_reset_reason());
}

static void assert_stacking_fault_reason(uint32_t raw_sp, bool extended_frame,
                                         hal_reset_reason_t expected) {
  hal_stm32g474_fault_test_set_stacking_fault_at(raw_sp, extended_frame, false);
  hal_fault_subsystem_init();
  TEST_ASSERT_EQUAL_INT(expected, (int)hal_get_reset_reason());
}

void test_stm32_lazy_fp_fault_does_not_use_exception_sp_heuristic(void) {
  const uint32_t guard = hal_stm32g474_fault_test_stack_guard_base();
  hal_stm32g474_fault_test_set_stacking_fault_at(guard + 31u, true, true);

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_HARDFAULT,
                        (int)hal_get_reset_reason());
}

void test_stm32_stacking_guard_boundaries_are_half_open(void) {
  const uint32_t guard = hal_stm32g474_fault_test_stack_guard_base();

  /* A basic frame starting one byte inside the guard overlaps it. */
  assert_stacking_fault_reason(guard + 31u, false,
                               HAL_RESET_REASON_STACK_OVERFLOW);

  hal_stm32g474_fault_test_reset();
  /* A frame starting exactly above the guard is legal. */
  assert_stacking_fault_reason(guard + 32u, false, HAL_RESET_REASON_HARDFAULT);

  hal_stm32g474_fault_test_reset();
  /* Include the maximum one-word STKALIGN padding when xPSR is unavailable. */
  assert_stacking_fault_reason(guard - 35u, false,
                               HAL_RESET_REASON_STACK_OVERFLOW);

  hal_stm32g474_fault_test_reset();
  assert_stacking_fault_reason(guard - 36u, false, HAL_RESET_REASON_HARDFAULT);

  hal_stm32g474_fault_test_reset();
  assert_stacking_fault_reason(guard - 107u, true,
                               HAL_RESET_REASON_STACK_OVERFLOW);

  hal_stm32g474_fault_test_reset();
  assert_stacking_fault_reason(guard - 108u, true, HAL_RESET_REASON_HARDFAULT);
}

void test_stm32_software_stack_overflow_marker_survives_reset(void) {
  hal_stm32g474_fault_test_set_rcc_csr(RCC_CSR_IWDGRSTF);
  hal_stm32g474_fault_test_set_retained_stack_overflow();

  hal_fault_subsystem_init();

  TEST_ASSERT_EQUAL_INT(HAL_RESET_REASON_STACK_OVERFLOW,
                        (int)hal_get_reset_reason());
  hal_fault_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_get_last_fault_ex(&info));
}

void test_stm32_stack_guard_api_reports_active_protection(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_stack_guard_init_ex());
  TEST_ASSERT_TRUE(hal_stack_guard_init());
  hal_stack_guard_check();
}

void test_stm32_stack_guard_api_detects_runtime_corruption(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_stack_guard_init_ex());
  hal_stm32g474_fault_test_corrupt_stack_guard();

  TEST_ASSERT_EQUAL_INT(HAL_EHW, hal_stack_guard_init_ex());
  TEST_ASSERT_FALSE(hal_stack_guard_init());
}

void test_stm32_system_status_reports_unsupported_services(void) {
  float temperature = 123.0f;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_read_chip_temp_ex(&temperature));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 123.0f, temperature);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_read_chip_temp_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_enter_bootloader());
}

void test_stm32_watchdog_rejects_out_of_range_timeouts(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_watchdog_enable(0u, false));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_watchdog_enable(32769u, false));
  TEST_ASSERT_FALSE(stm32g474_system_test_watchdog_enabled());
}

void test_stm32_watchdog_programs_shortest_fitting_prescaler(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_watchdog_enable(512u, true));

  TEST_ASSERT_TRUE(stm32g474_system_test_watchdog_enabled());
  TEST_ASSERT_TRUE(stm32g474_system_test_watchdog_pause_on_debug());
  TEST_ASSERT_EQUAL_UINT32(0u, stm32g474_system_test_watchdog_prescaler());
  TEST_ASSERT_EQUAL_UINT32(4095u, stm32g474_system_test_watchdog_reload());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_watchdog_enable(513u, false));
  TEST_ASSERT_FALSE(stm32g474_system_test_watchdog_pause_on_debug());
  TEST_ASSERT_EQUAL_UINT32(1u, stm32g474_system_test_watchdog_prescaler());
  TEST_ASSERT_EQUAL_UINT32(2051u, stm32g474_system_test_watchdog_reload());
}

void test_stm32_watchdog_rounds_up_and_accepts_max_timeout(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_watchdog_enable(1u, false));
  TEST_ASSERT_EQUAL_UINT32(0u, stm32g474_system_test_watchdog_prescaler());
  TEST_ASSERT_EQUAL_UINT32(7u, stm32g474_system_test_watchdog_reload());

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_watchdog_enable(32768u, false));
  TEST_ASSERT_EQUAL_UINT32(6u, stm32g474_system_test_watchdog_prescaler());
  TEST_ASSERT_EQUAL_UINT32(4095u, stm32g474_system_test_watchdog_reload());
}

void test_stm32_micros64_remains_monotonic_across_micros32_wrap(void) {
  constexpr uint64_t kBeforeWrap = UINT64_C(0x00000000FFFFFFF0);
  stm32g474_system_test_set_micros64(kBeforeWrap);

  TEST_ASSERT_EQUAL_UINT64(kBeforeWrap, hal_micros64());
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF0u, hal_micros());

  hal_delay_us(32u);

  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x0000000100000010), hal_micros64());
  TEST_ASSERT_EQUAL_HEX32(0x00000010u, hal_micros());
}

void test_stm32_hardware_time_composition_crosses_micros32_wrap(void) {
  const uint64_t last_32_bit_microsecond =
      jh_stm32g474_compose_micros(0u, 4294967u, 295u);
  const uint64_t first_64_bit_only_microsecond =
      jh_stm32g474_compose_micros(0u, 4294967u, 296u);

  TEST_ASSERT_EQUAL_UINT64(UINT32_MAX, last_32_bit_microsecond);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x100000000),
                           first_64_bit_only_microsecond);
  TEST_ASSERT_GREATER_THAN_UINT64(last_32_bit_microsecond,
                                  first_64_bit_only_microsecond);
}

void test_stm32_fdcan_timing_is_exact_at_170mhz(void) {
  jh_stm32g474_fdcan_timing_t nominal = {};
  jh_stm32g474_fdcan_timing_t data = {};

  TEST_ASSERT_TRUE(jh_stm32g474_fdcan_compute_timing(JH_G474_FDCAN_CLOCK_HZ,
                                                     500000u, false, &nominal));
  TEST_ASSERT_TRUE(jh_stm32g474_fdcan_compute_timing(JH_G474_FDCAN_CLOCK_HZ,
                                                     2000000u, true, &data));

  TEST_ASSERT_EQUAL_UINT32(500000u, nominal.actual_bitrate_hz);
  TEST_ASSERT_EQUAL_UINT32(340u,
                           (uint32_t)nominal.prescaler *
                               (1u + nominal.segment1 + nominal.segment2));
  TEST_ASSERT_EQUAL_UINT32(2000000u, data.actual_bitrate_hz);
  TEST_ASSERT_EQUAL_UINT32(85u, (uint32_t)data.prescaler *
                                    (1u + data.segment1 + data.segment2));
  TEST_ASSERT_NOT_EQUAL(0u, jh_stm32g474_fdcan_encode_nbtp(&nominal));
  TEST_ASSERT_NOT_EQUAL(0u, jh_stm32g474_fdcan_encode_dbtp(&data));
}

void test_stm32_fdcan_timing_rejects_unreachable_bitrate(void) {
  jh_stm32g474_fdcan_timing_t timing = {};
  TEST_ASSERT_FALSE(jh_stm32g474_fdcan_compute_timing(
      JH_G474_FDCAN_CLOCK_HZ, JH_G474_FDCAN_CLOCK_HZ, true, &timing));
}

void test_stm32_architecture_reports_generated_target_metadata(void) {
  hal_system_architecture_t architecture = {};

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_system_get_current_architecture(&architecture));
  TEST_ASSERT_EQUAL_STRING("stm32g474", architecture.target_name);
  TEST_ASSERT_EQUAL_STRING("stm32g474/bare-metal", architecture.backend_name);
  TEST_ASSERT_EQUAL_STRING("STM32G474RE", architecture.mcu);
  TEST_ASSERT_EQUAL_STRING("STM32G474RETx", architecture.mcu_subtype);
  TEST_ASSERT_EQUAL_STRING("ARM Cortex-M4F", architecture.cpu_arch);
  TEST_ASSERT_EQUAL_UINT8(1u, architecture.cpu_cores);
  TEST_ASSERT_TRUE(architecture.has_fpu);
  TEST_ASSERT_EQUAL_UINT32(512u * 1024u, architecture.flash_total_bytes);
  TEST_ASSERT_EQUAL_UINT32(128u * 1024u, architecture.ram_total_bytes);
  TEST_ASSERT_EQUAL_UINT32(96u * 1024u, architecture.ram_usable_bytes);
  TEST_ASSERT_EQUAL_UINT32(170000000u, architecture.cpu_clock_hz);
  TEST_ASSERT_EQUAL_UINT32(170000000u, architecture.peripheral_clock_hz);
  TEST_ASSERT_EQUAL_STRING("none", architecture.network_backend_name);
  TEST_ASSERT_EQUAL_STRING("none", architecture.network_stack_name);
  TEST_ASSERT_EQUAL_INT(HAL_SYSTEM_NETWORK_STACK_TYPE_NONE,
                        architecture.network_stack_type);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stm32_reset_reason_soft_reset_maps_to_soft);
  RUN_TEST(test_stm32_reset_reason_watchdog_flags_map_to_watchdog);
  RUN_TEST(test_stm32_non_watchdog_reset_does_not_report_watchdog_reboot);
  RUN_TEST(test_stm32_reset_reason_bor_pin_without_alive_maps_to_power_on);
  RUN_TEST(test_stm32_reset_reason_bor_with_alive_maps_to_brownout);
  RUN_TEST(test_stm32_fault_frame_overrides_rcc_reason);
  RUN_TEST(test_stm32_stack_guard_fault_overrides_reason);
  RUN_TEST(test_stm32_escalated_stack_guard_fault_is_still_overflow);
  RUN_TEST(test_stm32_stale_mmfar_without_access_violation_is_not_overflow);
  RUN_TEST(test_stm32_stack_guard_stacking_fault_without_mmfar_is_detected);
  RUN_TEST(test_stm32_unrelated_stacking_fault_remains_hardfault);
  RUN_TEST(test_stm32_stacking_guard_boundaries_are_half_open);
  RUN_TEST(test_stm32_lazy_fp_fault_does_not_use_exception_sp_heuristic);
  RUN_TEST(test_stm32_software_stack_overflow_marker_survives_reset);
  RUN_TEST(test_stm32_stack_guard_api_reports_active_protection);
  RUN_TEST(test_stm32_stack_guard_api_detects_runtime_corruption);
  RUN_TEST(test_stm32_system_status_reports_unsupported_services);
  RUN_TEST(test_stm32_watchdog_rejects_out_of_range_timeouts);
  RUN_TEST(test_stm32_watchdog_programs_shortest_fitting_prescaler);
  RUN_TEST(test_stm32_watchdog_rounds_up_and_accepts_max_timeout);
  RUN_TEST(test_stm32_micros64_remains_monotonic_across_micros32_wrap);
  RUN_TEST(test_stm32_hardware_time_composition_crosses_micros32_wrap);
  RUN_TEST(test_stm32_fdcan_timing_is_exact_at_170mhz);
  RUN_TEST(test_stm32_fdcan_timing_rejects_unreachable_bitrate);
  RUN_TEST(test_stm32_architecture_reports_generated_target_metadata);
  return UNITY_END();
}
