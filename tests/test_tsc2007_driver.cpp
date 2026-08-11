#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/input/hal_tsc2007.h"
#include "hal/system/hal_system.h"
#include "utils/unity.h"

#include <stdint.h>

static hal_tsc2007_t dev;

static void append_sample(uint8_t *rx, size_t *pos, uint16_t value) {
  rx[(*pos)++] = (uint8_t)(value >> 4u);
  rx[(*pos)++] = (uint8_t)((value & 0x0Fu) << 4u);
}

static void inject_samples(const uint16_t *values, size_t count) {
  uint8_t rx[32] = {};
  size_t pos = 0u;
  for (size_t i = 0u; i < count; ++i) {
    append_sample(rx, &pos, values[i]);
  }
  hal_mock_i2c_inject_rx(rx, (int)pos);
}

static void assert_frame_byte(int index, uint8_t expected) {
  uint8_t frame[2] = {};
  TEST_ASSERT_EQUAL_INT(
      1, hal_mock_i2c_get_write_frame(index, frame, (int)sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(expected, frame[0]);
}

static void assert_frame_byte_bus(uint8_t bus, int index, uint8_t expected) {
  uint8_t frame[2] = {};
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_bus(
                               bus, index, frame, (int)sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(expected, frame[0]);
}

void setUp(void) {
  dev = {};
  hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_i2c_init_bus(1, 6, 7, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_set_busy_bus(1, false);
  hal_mock_i2c_reset_write_log();
  hal_mock_i2c_reset_write_log_bus(1);
  hal_mock_mutex_stats_reset();
  hal_mock_set_micros(0u);
}

void tearDown(void) {
  hal_tsc2007_deinit(&dev);
  hal_mock_mutex_stats_reset();
}

void test_default_config_matches_source_driver_address(void) {
  hal_tsc2007_config_t cfg = hal_tsc2007_default_config();

  TEST_ASSERT_EQUAL_UINT8(0u, cfg.i2c_bus);
  TEST_ASSERT_EQUAL_UINT8(HAL_TSC2007_I2C_ADDR_DEFAULT, cfg.i2c_addr);
}

void test_init_probes_address_and_sends_powerdown_temp0_command(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_tsc2007_init_ex(&dev, NULL));
  TEST_ASSERT_TRUE(dev.initialized);
  TEST_ASSERT_NOT_NULL(dev.mutex);
  TEST_ASSERT_EQUAL_UINT8(HAL_TSC2007_I2C_ADDR_DEFAULT,
                          hal_mock_i2c_get_last_addr());
  TEST_ASSERT_EQUAL_UINT32(500u, hal_micros());

  uint8_t frame[2] = {};
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
  TEST_ASSERT_EQUAL_INT(
      0, hal_mock_i2c_get_write_frame(0, frame, (int)sizeof(frame)));
  assert_frame_byte(1, 0x00u);
}

void test_init_failure_releases_mutex_and_does_not_wait(void) {
  hal_mock_i2c_set_busy(true);

  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_tsc2007_init_ex(&dev, NULL));
  TEST_ASSERT_FALSE(dev.initialized);
  TEST_ASSERT_NULL(dev.mutex);
  TEST_ASSERT_EQUAL_UINT32(0u, hal_micros());
}

void test_init_ex_rejects_null_driver(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_tsc2007_init_ex(NULL, NULL));
}

void test_command_builds_byte_waits_and_decodes_twelve_bit_reply(void) {
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, NULL));
  hal_mock_i2c_reset_write_log();
  hal_mock_set_micros(0u);
  const uint16_t samples[] = {0xABCu};
  inject_samples(samples, 1u);

  uint16_t value =
      hal_tsc2007_command(&dev, HAL_TSC2007_MEASURE_X, HAL_TSC2007_ADON_IRQOFF,
                          HAL_TSC2007_ADC_12BIT);

  TEST_ASSERT_EQUAL_UINT16(0xABCu, value);

  const uint16_t samples2[] = {0x123u};
  inject_samples(samples2, 1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tsc2007_command_ex(&dev, HAL_TSC2007_MEASURE_Y,
                                               HAL_TSC2007_ADON_IRQOFF,
                                               HAL_TSC2007_ADC_12BIT, &value));
  TEST_ASSERT_EQUAL_UINT16(0x123u, value);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tsc2007_command_ex(&dev, HAL_TSC2007_MEASURE_Y,
                                               HAL_TSC2007_ADON_IRQOFF,
                                               HAL_TSC2007_ADC_12BIT, NULL));
  TEST_ASSERT_EQUAL_UINT32(1000u, hal_micros());
  TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
  assert_frame_byte(0, 0xC4u);
}

void test_read_touch_uses_source_sequence_and_accepts_stable_sample(void) {
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, NULL));
  hal_mock_i2c_reset_write_log();
  hal_mock_set_micros(0u);
  const uint16_t samples[] = {
      100u, 200u, 1234u, 2345u, 1250u, 2360u, 0u,
  };
  inject_samples(samples, sizeof(samples) / sizeof(samples[0]));

  uint16_t x = 0u;
  uint16_t y = 0u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_tsc2007_read_touch_ex(&dev, &x, &y, &z1, &z2));

  TEST_ASSERT_EQUAL_UINT16(1234u, x);
  TEST_ASSERT_EQUAL_UINT16(2345u, y);
  TEST_ASSERT_EQUAL_UINT16(100u, z1);
  TEST_ASSERT_EQUAL_UINT16(200u, z2);
  TEST_ASSERT_EQUAL_UINT32(7u * 500u, hal_micros());
  TEST_ASSERT_EQUAL_INT(7, hal_mock_i2c_get_write_frame_count());
  assert_frame_byte(0, 0xE4u);
  assert_frame_byte(1, 0xF4u);
  assert_frame_byte(2, 0xC4u);
  assert_frame_byte(3, 0xD4u);
  assert_frame_byte(4, 0xC4u);
  assert_frame_byte(5, 0xD4u);
  assert_frame_byte(6, 0x00u);
}

void test_read_touch_rejects_unstable_duplicate_samples(void) {
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, NULL));
  hal_mock_set_micros(0u);
  uint16_t samples[] = {
      100u, 200u, 1000u, 2200u, 1101u, 2200u, 0u,
  };
  inject_samples(samples, sizeof(samples) / sizeof(samples[0]));

  uint16_t x = 55u;
  uint16_t y = 66u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;
  TEST_ASSERT_FALSE(hal_tsc2007_read_touch(&dev, &x, &y, &z1, &z2));
  inject_samples(samples, sizeof(samples) / sizeof(samples[0]));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_tsc2007_read_touch_ex(&dev, &x, &y, &z1, &z2));
  TEST_ASSERT_EQUAL_UINT16(55u, x);
  TEST_ASSERT_EQUAL_UINT16(66u, y);

  const uint16_t y_unstable[] = {
      100u, 200u, 1000u, 2200u, 1000u, 2301u, 0u,
  };
  inject_samples(y_unstable, sizeof(y_unstable) / sizeof(y_unstable[0]));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        hal_tsc2007_read_touch_ex(&dev, &x, &y, &z1, &z2));
}

void test_read_touch_rejects_4095_coordinates_after_assigning_xy(void) {
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, NULL));
  const uint16_t samples[] = {
      100u, 200u, 4095u, 1200u, 4095u, 1200u, 0u,
  };
  inject_samples(samples, sizeof(samples) / sizeof(samples[0]));

  uint16_t x = 0u;
  uint16_t y = 0u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;
  TEST_ASSERT_FALSE(hal_tsc2007_read_touch(&dev, &x, &y, &z1, &z2));
  TEST_ASSERT_EQUAL_UINT16(4095u, x);
  TEST_ASSERT_EQUAL_UINT16(1200u, y);
}

void test_status_api_reports_invalid_and_uninitialized_touch_reads(void) {
  uint16_t x = 0u;
  uint16_t y = 0u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_tsc2007_read_touch_ex(&dev, &x, &y, &z1, &z2));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_tsc2007_read_touch_ex(&dev, NULL, &y, &z1, &z2));
}

void test_get_point_returns_x_y_and_z1_or_zero_point(void) {
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, NULL));
  const uint16_t ok_samples[] = {
      321u, 777u, 1111u, 2222u, 1112u, 2223u, 0u,
  };
  inject_samples(ok_samples, sizeof(ok_samples) / sizeof(ok_samples[0]));

  hal_tsc2007_point_t point = hal_tsc2007_get_point(&dev);
  TEST_ASSERT_EQUAL_INT16(1111, point.x);
  TEST_ASSERT_EQUAL_INT16(2222, point.y);
  TEST_ASSERT_EQUAL_INT16(321, point.z);

  const uint16_t bad_samples[] = {
      100u, 200u, 1000u, 1000u, 1300u, 1000u, 0u,
  };
  inject_samples(bad_samples, sizeof(bad_samples) / sizeof(bad_samples[0]));
  point = hal_tsc2007_get_point(&dev);
  TEST_ASSERT_EQUAL_INT16(0, point.x);
  TEST_ASSERT_EQUAL_INT16(0, point.y);
  TEST_ASSERT_EQUAL_INT16(0, point.z);
}

void test_selected_i2c_bus_and_address_are_used(void) {
  hal_tsc2007_config_t cfg = hal_tsc2007_default_config();
  cfg.i2c_bus = 1u;
  cfg.i2c_addr = 0x49u;
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, &cfg));

  hal_mock_i2c_reset_write_log_bus(1);
  const uint16_t samples[] = {0x123u};
  uint8_t rx[2] = {};
  size_t pos = 0u;
  append_sample(rx, &pos, samples[0]);
  hal_mock_i2c_inject_rx_bus(1, rx, (int)pos);

  TEST_ASSERT_EQUAL_UINT16(0x123u,
                           hal_tsc2007_command(&dev, HAL_TSC2007_MEASURE_Y,
                                               HAL_TSC2007_ADON_IRQOFF,
                                               HAL_TSC2007_ADC_12BIT));
  TEST_ASSERT_EQUAL_UINT8(0x49u, hal_mock_i2c_get_last_addr_bus(1));
  assert_frame_byte_bus(1u, 0, 0xD4u);
}

void test_multi_command_read_takes_one_driver_mutex(void) {
  TEST_ASSERT_TRUE(hal_tsc2007_init(&dev, NULL));
  const uint16_t samples[] = {
      100u, 200u, 1234u, 2345u, 1235u, 2346u, 0u,
  };
  inject_samples(samples, sizeof(samples) / sizeof(samples[0]));

  hal_mock_mutex_stats_reset();
  uint16_t x = 0u;
  uint16_t y = 0u;
  uint16_t z1 = 0u;
  uint16_t z2 = 0u;
  TEST_ASSERT_TRUE(hal_tsc2007_read_touch(&dev, &x, &y, &z1, &z2));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_lock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_unlock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_max_depth());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_matches_source_driver_address);
  RUN_TEST(test_init_probes_address_and_sends_powerdown_temp0_command);
  RUN_TEST(test_init_failure_releases_mutex_and_does_not_wait);
  RUN_TEST(test_init_ex_rejects_null_driver);
  RUN_TEST(test_command_builds_byte_waits_and_decodes_twelve_bit_reply);
  RUN_TEST(test_read_touch_uses_source_sequence_and_accepts_stable_sample);
  RUN_TEST(test_read_touch_rejects_unstable_duplicate_samples);
  RUN_TEST(test_read_touch_rejects_4095_coordinates_after_assigning_xy);
  RUN_TEST(test_status_api_reports_invalid_and_uninitialized_touch_reads);
  RUN_TEST(test_get_point_returns_x_y_and_z1_or_zero_point);
  RUN_TEST(test_selected_i2c_bus_and_address_are_used);
  RUN_TEST(test_multi_command_read_takes_one_driver_mutex);
  return UNITY_END();
}
