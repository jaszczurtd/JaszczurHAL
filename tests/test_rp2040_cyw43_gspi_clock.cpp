#include "hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_gspi_clock.h"
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_gspi_clock_tracks_rp2040_clk_sys(void) {
  jh_rp2040_cyw43_gspi_clock_t config{};
  TEST_ASSERT_TRUE(
      jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 31250000u, 0u, &config));
  TEST_ASSERT_EQUAL_UINT16(2u, config.divider_int);
  TEST_ASSERT_EQUAL_UINT8(0u, config.divider_frac8);
  TEST_ASSERT_EQUAL_UINT32(31250000u, config.actual_gspi_hz);
  TEST_ASSERT_EQUAL_INT(JH_RP2040_CYW43_PIO_PROGRAM_HIGH_SPEED, config.program);
}

void test_gspi_clock_tracks_rp2350_clk_sys_without_exceeding_target(void) {
  jh_rp2040_cyw43_gspi_clock_t config{};
  TEST_ASSERT_TRUE(
      jh_rp2040_cyw43_gspi_clock_calculate(150000000u, 31250000u, 0u, &config));
  TEST_ASSERT_EQUAL_UINT16(2u, config.divider_int);
  TEST_ASSERT_EQUAL_UINT8(103u, config.divider_frac8);
  TEST_ASSERT_EQUAL_UINT32(31219512u, config.actual_gspi_hz);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(31250000u, config.actual_gspi_hz);
  TEST_ASSERT_EQUAL_INT(JH_RP2040_CYW43_PIO_PROGRAM_HIGH_SPEED, config.program);
}

void test_gspi_clock_tracks_overclocked_clk_sys(void) {
  jh_rp2040_cyw43_gspi_clock_t config{};
  TEST_ASSERT_TRUE(
      jh_rp2040_cyw43_gspi_clock_calculate(200000000u, 31250000u, 0u, &config));
  TEST_ASSERT_EQUAL_UINT16(3u, config.divider_int);
  TEST_ASSERT_EQUAL_UINT8(52u, config.divider_frac8);
  TEST_ASSERT_EQUAL_UINT32(31219512u, config.actual_gspi_hz);
}

void test_gspi_clock_selects_low_speed_sampling_for_slow_bus(void) {
  jh_rp2040_cyw43_gspi_clock_t config{};
  TEST_ASSERT_TRUE(
      jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 15625000u, 0u, &config));
  TEST_ASSERT_EQUAL_UINT16(4u, config.divider_int);
  TEST_ASSERT_EQUAL_UINT8(0u, config.divider_frac8);
  TEST_ASSERT_EQUAL_UINT32(15625000u, config.actual_gspi_hz);
  TEST_ASSERT_EQUAL_INT(JH_RP2040_CYW43_PIO_PROGRAM_LOW_SPEED, config.program);
}

void test_gspi_clock_preserves_explicit_diagnostic_override(void) {
  jh_rp2040_cyw43_gspi_clock_t config{};
  TEST_ASSERT_TRUE(jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 31250000u,
                                                        4u * 256u, &config));
  TEST_ASSERT_EQUAL_UINT16(4u, config.divider_int);
  TEST_ASSERT_EQUAL_UINT8(0u, config.divider_frac8);
  TEST_ASSERT_EQUAL_UINT32(15625000u, config.actual_gspi_hz);
  TEST_ASSERT_EQUAL_INT(JH_RP2040_CYW43_PIO_PROGRAM_LOW_SPEED, config.program);
}

void test_gspi_clock_rejects_unrepresentable_configuration(void) {
  jh_rp2040_cyw43_gspi_clock_t config{};
  TEST_ASSERT_FALSE(
      jh_rp2040_cyw43_gspi_clock_calculate(0u, 31250000u, 0u, &config));
  TEST_ASSERT_FALSE(
      jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 0u, 0u, &config));
  TEST_ASSERT_FALSE(
      jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 70000000u, 0u, &config));
  TEST_ASSERT_FALSE(jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 31250000u,
                                                         255u, &config));
  TEST_ASSERT_FALSE(
      jh_rp2040_cyw43_gspi_clock_calculate(125000000u, 31250000u, 0u, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gspi_clock_tracks_rp2040_clk_sys);
  RUN_TEST(test_gspi_clock_tracks_rp2350_clk_sys_without_exceeding_target);
  RUN_TEST(test_gspi_clock_tracks_overclocked_clk_sys);
  RUN_TEST(test_gspi_clock_selects_low_speed_sampling_for_slow_bus);
  RUN_TEST(test_gspi_clock_preserves_explicit_diagnostic_override);
  RUN_TEST(test_gspi_clock_rejects_unrepresentable_configuration);
  return UNITY_END();
}
