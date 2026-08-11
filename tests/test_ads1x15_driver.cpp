#include "hal/analog/ads1x15/ads1x15_driver.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

/* Datasheet anchors used by these tests (ADS111x/ADS101x):
 * - Config register bitfields: OS, MUX, PGA, MODE, DR, COMP_MODE, COMP_POL,
 *   COMP_LAT, COMP_QUE
 * - Conversion register semantics: ADS1115 uses full 16-bit result,
 *   ADS1015 left-justifies the 12-bit result
 * - Threshold registers are 16-bit big-endian registers
 * - ADS1113/ADS1013 do not expose programmable PGA/comparator behavior;
 *   ADS1114/ADS1014 add PGA/comparator; ADS1115/ADS1015 add MUXed inputs
 */

static void inject_ready_and_value(uint16_t value) {
  const uint8_t rx[] = {0x80u, 0x00u, (uint8_t)(value >> 8),
                        (uint8_t)(value & 0xFFu)};
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
}

static void assert_write_frame(int index, uint8_t b0, uint8_t b1, uint8_t b2) {
  uint8_t frame[4] = {};
  TEST_ASSERT_EQUAL_INT(3, hal_mock_i2c_get_write_frame(index, frame, 4));
  TEST_ASSERT_EQUAL_UINT8(b0, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(b1, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(b2, frame[2]);
}

void setUp(void) {
  hal_i2c_init_bus(0, 0, 0, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_reset_write_log();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_begin_validates_address_and_ack(void) {
  ADS1115 ads(0x48);
  TEST_ASSERT_TRUE(ads.begin());

  ADS1115 invalid(0x47);
  TEST_ASSERT_FALSE(invalid.begin());

  hal_mock_i2c_set_busy(true);
  ADS1115 absent(0x48);
  TEST_ASSERT_FALSE(absent.begin());
}

void test_ads1115_single_ended_read_writes_expected_config(void) {
  ADS1115 ads(0x48);
  inject_ready_and_value(0x1234u);

  TEST_ASSERT_EQUAL_INT16(0x1234, ads.readADC(2));
  TEST_ASSERT_EQUAL_INT8(ADS1X15_OK, ads.getError());
  TEST_ASSERT_EQUAL_UINT8(0x02, ads.lastRequest());

  assert_write_frame(0, 0x01u, 0xE1u, 0x8Bu);
}

void test_gain_mode_data_rate_and_voltage_mapping_match_ads1x15(void) {
  ADS1115 ads(0x48);

  ads.setGain(ADS1X15_GAIN_2048MV);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_2048MV, ads.getGain());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.048f, ads.getMaxVoltage());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.048f, ads.toVoltage(32767.0f));

  ads.setMode(ADS1X15_MODE_CONTINUOUS);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_MODE_CONTINUOUS, ads.getMode());

  ads.setDataRate(ADS1X15_DATARATE_7);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_DATARATE_7, ads.getDataRate());

  ads.setGain(123);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_6144MV, ads.getGain());
}

void test_invalid_pin_read_and_request_are_ignored(void) {
  ADS1115 ads(0x48);

  TEST_ASSERT_EQUAL_INT16(0, ads.readADC(9));
  ads.requestADC(9);
  TEST_ASSERT_EQUAL_UINT8(0xFFu, ads.lastRequest());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_write_frame_count());
}

void test_ads1015_read_shifts_12_bit_result(void) {
  ADS1015 ads(0x48);
  inject_ready_and_value(0x7FF0u);

  TEST_ASSERT_EQUAL_INT16(2047, ads.readADC(0));
  assert_write_frame(0, 0x01u, 0xC1u, 0x8Bu);
}

void test_async_differential_request_tracks_last_request(void) {
  ADS1115 ads(0x48);
  ads.requestADC_Differential_1_3();

  TEST_ASSERT_EQUAL_UINT8(0x31, ads.lastRequest());
  assert_write_frame(0, 0x01u, 0xA1u, 0x8Bu);
}

void test_is_ready_and_is_busy_follow_os_bit(void) {
  ADS1115 ads(0x48);

  const uint8_t busy_rx[] = {0x00u, 0x00u};
  hal_mock_i2c_inject_rx(busy_rx, (int)sizeof(busy_rx));
  TEST_ASSERT_FALSE(ads.isReady());

  const uint8_t ready_rx[] = {0x80u, 0x00u};
  hal_mock_i2c_inject_rx(ready_rx, (int)sizeof(ready_rx));
  TEST_ASSERT_FALSE(ads.isBusy());
}

void test_comparator_config_bits_roundtrip_through_getters(void) {
  ADS1115 ads(0x48);

  ads.setComparatorMode(ADS1x15_COMP_MODE_WINDOW);
  ads.setComparatorPolarity(ADS1x15_COMP_POL_RISING_EDGE);
  ads.setComparatorLatch(ADS1x15_COMP_POL_LATCH);
  ads.setComparatorQueConvert(ADS1x15_COMP_QUE_CONV_TRIGGER_4);

  TEST_ASSERT_EQUAL_UINT8(ADS1x15_COMP_MODE_WINDOW, ads.getComparatorMode());
  TEST_ASSERT_EQUAL_UINT8(ADS1x15_COMP_POL_RISING_EDGE,
                          ads.getComparatorPolarity());
  TEST_ASSERT_EQUAL_UINT8(ADS1x15_COMP_POL_LATCH, ads.getComparatorLatch());
  TEST_ASSERT_EQUAL_UINT8(ADS1x15_COMP_QUE_CONV_TRIGGER_4,
                          ads.getComparatorQueConvert());

  /* Out-of-range queue mode clamps to disable (3). */
  ads.setComparatorQueConvert(99u);
  TEST_ASSERT_EQUAL_UINT8(ADS1x15_COMP_QUE_CONV_DISABLE,
                          ads.getComparatorQueConvert());
}

void test_set_comparator_off_clears_queue_bits_in_config_register(void) {
  ADS1115 ads(0x48);

  const uint8_t config_rx[] = {0xC1u, 0x8Bu};
  hal_mock_i2c_inject_rx(config_rx, (int)sizeof(config_rx));
  TEST_ASSERT_TRUE(ads.setComparatorOff());

  assert_write_frame(1, 0x01u, 0xC1u, 0x83u);
}

void test_get_threshold_registers_decode_big_endian_words(void) {
  ADS1115 ads(0x48);

  const uint8_t lo_rx[] = {0x12u, 0x34u};
  hal_mock_i2c_inject_rx(lo_rx, (int)sizeof(lo_rx));
  TEST_ASSERT_EQUAL_INT16(0x1234, ads.getComparatorThresholdLow());

  const uint8_t hi_rx[] = {0x56u, 0x78u};
  hal_mock_i2c_inject_rx(hi_rx, (int)sizeof(hi_rx));
  TEST_ASSERT_EQUAL_INT16(0x5678, ads.getComparatorThresholdHigh());
}

void test_class_variants_expose_expected_gain_capabilities(void) {
  ADS1013 ads1013(0x48);
  ads1013.setGain(ADS1X15_GAIN_0256MV);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_2048MV, ads1013.getGain());

  ADS1113 ads1113(0x48);
  ads1113.setGain(ADS1X15_GAIN_0256MV);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_2048MV, ads1113.getGain());

  ADS1014 ads1014(0x48);
  ads1014.setGain(ADS1X15_GAIN_0256MV);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_0256MV, ads1014.getGain());

  ADS1114 ads1114(0x48);
  ads1114.setGain(ADS1X15_GAIN_0256MV);
  TEST_ASSERT_EQUAL_UINT8(ADS1X15_GAIN_0256MV, ads1114.getGain());
}

void test_ads1015_pseudo_differential_paths_subtract_single_ended_reads(void) {
  ADS1015 ads(0x48);
  /* readADC(0) -> 0x640 = 100, readADC(2) -> 0x1F40 = 500 after >>4 */
  const uint8_t rx[] = {
      0x80u, 0x00u, 0x06u, 0x40u, 0x80u, 0x00u, 0x1Fu, 0x40u,
  };
  hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

  TEST_ASSERT_EQUAL_INT16(-400, ads.readADC_Differential_0_2());
}

void test_i2c_failure_sets_error_code(void) {
  ADS1115 ads(0x48);
  hal_mock_i2c_set_busy(true);

  TEST_ASSERT_FALSE(ads.setComparatorOff());
  TEST_ASSERT_EQUAL_INT8(ADS1X15_ERROR_I2C, ads.getError());
  hal_mock_i2c_set_busy(false);
}

void test_comparator_thresholds_are_big_endian_register_writes(void) {
  ADS1115 ads(0x48);

  ads.setComparatorThresholdLow((int16_t)0x1234);
  ads.setComparatorThresholdHigh((int16_t)0x5678);

  assert_write_frame(0, 0x02u, 0x12u, 0x34u);
  assert_write_frame(1, 0x03u, 0x56u, 0x78u);
}

void test_set_wire_clock_uses_hal_i2c_clock(void) {
  ADS1115 ads(0x48, 1);

  ads.setWireClock(HAL_I2C_CLOCK_FAST_HZ);

  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ, ads.getWireClock());
  TEST_ASSERT_EQUAL_UINT32(HAL_I2C_CLOCK_FAST_HZ,
                           hal_mock_i2c_get_clock_hz_bus(1));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_validates_address_and_ack);
  RUN_TEST(test_ads1115_single_ended_read_writes_expected_config);
  RUN_TEST(test_gain_mode_data_rate_and_voltage_mapping_match_ads1x15);
  RUN_TEST(test_invalid_pin_read_and_request_are_ignored);
  RUN_TEST(test_ads1015_read_shifts_12_bit_result);
  RUN_TEST(test_async_differential_request_tracks_last_request);
  RUN_TEST(test_is_ready_and_is_busy_follow_os_bit);
  RUN_TEST(test_comparator_config_bits_roundtrip_through_getters);
  RUN_TEST(test_set_comparator_off_clears_queue_bits_in_config_register);
  RUN_TEST(test_comparator_thresholds_are_big_endian_register_writes);
  RUN_TEST(test_get_threshold_registers_decode_big_endian_words);
  RUN_TEST(test_class_variants_expose_expected_gain_capabilities);
  RUN_TEST(test_ads1015_pseudo_differential_paths_subtract_single_ended_reads);
  RUN_TEST(test_i2c_failure_sets_error_code);
  RUN_TEST(test_set_wire_clock_uses_hal_i2c_clock);
  return UNITY_END();
}
