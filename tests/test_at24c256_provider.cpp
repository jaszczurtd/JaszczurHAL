#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/jh_eeprom_provider.h"
#include "utils/unity.h"

namespace {

uint32_t s_progress_calls = 0u;

void progress(void *ctx) {
  TEST_ASSERT_EQUAL_PTR(&s_progress_calls, ctx);
  ++s_progress_calls;
}

const jh_eeprom_provider_ops_t *provider() {
  return jh_at24c256_provider_get_ops();
}

void initialize_provider() {
  const jh_eeprom_provider_config_t config = {HAL_EEPROM_AT24C256, 0u, 0x52u};
  jh_eeprom_provider_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider()->initialize(&config, &info));
  TEST_ASSERT_EQUAL_INT(HAL_EEPROM_AT24C256, info.type);
  TEST_ASSERT_EQUAL_UINT16(32768u, info.size);
}

} // namespace

void setUp(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_init(4u, 5u, HAL_I2C_CLOCK_FAST_HZ));
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_reset_write_log();
  s_progress_calls = 0u;
  initialize_provider();
}

void tearDown(void) { hal_i2c_deinit(); }

void test_write_splits_at_page_boundary_and_reports_progress(void) {
  const uint8_t data[] = {0x11u, 0x22u, 0x33u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider()->write(63u, data, sizeof(data),
                                                  progress, &s_progress_calls));
  TEST_ASSERT_EQUAL_UINT32(2u, s_progress_calls);
  TEST_ASSERT_EQUAL_INT(4, hal_mock_i2c_get_write_frame_count());

  uint8_t frame[8] = {};
  TEST_ASSERT_EQUAL_INT(3,
                        hal_mock_i2c_get_write_frame(0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x00u, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x3fu, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0x11u, frame[2]);
  TEST_ASSERT_EQUAL_INT(4,
                        hal_mock_i2c_get_write_frame(2, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x00u, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x40u, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0x22u, frame[2]);
  TEST_ASSERT_EQUAL_HEX8(0x33u, frame[3]);
}

void test_read_uses_big_endian_address_and_propagates_data(void) {
  const uint8_t expected[] = {0xa1u, 0xb2u, 0xc3u};
  uint8_t actual[sizeof(expected)] = {};
  hal_mock_i2c_inject_rx(expected, sizeof(expected));

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider()->read(0x1234u, actual, sizeof(actual)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(actual));
  TEST_ASSERT_EQUAL_UINT8(0x52u, hal_mock_i2c_get_last_addr());
  uint8_t frame[8] = {};
  TEST_ASSERT_EQUAL_INT(2,
                        hal_mock_i2c_get_write_frame(0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x12u, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34u, frame[1]);
}

void test_i2c_write_and_read_failures_are_reported(void) {
  const uint8_t value = 0x5au;
  uint8_t out = 0u;
  hal_mock_i2c_set_busy(true);

  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        provider()->write(0u, &value, 1u, nullptr, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider()->read(0u, &out, 1u));
}

void test_provider_rejects_invalid_ranges_and_wrong_type(void) {
  const uint8_t value = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, provider()->write(32768u, &value, 1u, nullptr, nullptr));

  const jh_eeprom_provider_config_t config = {HAL_EEPROM_FLASH, 16u, 0u};
  jh_eeprom_provider_info_t info = {};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, provider()->initialize(&config, &info));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_write_splits_at_page_boundary_and_reports_progress);
  RUN_TEST(test_read_uses_big_endian_address_and_propagates_data);
  RUN_TEST(test_i2c_write_and_read_failures_are_reported);
  RUN_TEST(test_provider_rejects_invalid_ranges_and_wrong_type);
  return UNITY_END();
}
