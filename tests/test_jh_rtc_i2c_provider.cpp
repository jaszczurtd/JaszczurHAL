#include "hal/i2c/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/rtc/jh_rtc_provider.h"
#include "utils/unity.h"

#include <cstddef>
#include <cstring>

alignas(
    std::max_align_t) static uint8_t s_context[JH_RTC_PROVIDER_STORAGE_SIZE];

void setUp(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_init_bus(0u, 4u, 5u, 400000u));
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_reset_write_log();
  std::memset(s_context, 0, sizeof(s_context));
}

void tearDown(void) {}

static hal_rtc_config_t config_for(hal_rtc_chip_t chip, uint8_t address) {
  hal_rtc_config_t config = {};
  config.chip = chip;
  config.bus.i2c.sda_pin = 4u;
  config.bus.i2c.scl_pin = 5u;
  config.bus.i2c.clock_hz = 400000u;
  config.bus.i2c.i2c_bus = 0u;
  config.bus.i2c.i2c_addr = address;
  return config;
}

static int last_write_frame(uint8_t *buffer, int capacity) {
  const int count = hal_mock_i2c_get_write_frame_count();
  TEST_ASSERT_TRUE_MESSAGE(count > 0, "no write frame logged");
  return hal_mock_i2c_get_write_frame(count - 1, buffer, capacity);
}

void test_provider_selection_reports_chip_metadata(void) {
  const jh_rtc_provider_ops_t *pcf =
      jh_rtc_i2c_provider_get_ops(HAL_RTC_CHIP_PCF8563);
  const jh_rtc_provider_ops_t *ds =
      jh_rtc_i2c_provider_get_ops(HAL_RTC_CHIP_DS3231);

  TEST_ASSERT_NOT_NULL(pcf);
  TEST_ASSERT_EQUAL_INT(JH_RTC_PROVIDER_BUS_I2C, pcf->bus);
  TEST_ASSERT_EQUAL_HEX8(HAL_RTC_PCF8563_DEFAULT_I2C_ADDR,
                         pcf->default_i2c_addr);
  TEST_ASSERT_FALSE(pcf->fixed_i2c_addr);
  TEST_ASSERT_NOT_NULL(ds);
  TEST_ASSERT_EQUAL_INT(JH_RTC_PROVIDER_BUS_I2C, ds->bus);
  TEST_ASSERT_EQUAL_HEX8(HAL_RTC_DS3231_DEFAULT_I2C_ADDR, ds->default_i2c_addr);
  TEST_ASSERT_TRUE(ds->fixed_i2c_addr);

  hal_rtc_clock_source_t source = HAL_RTC_CLOCK_SOURCE_AUTO;
  TEST_ASSERT_EQUAL_INT(HAL_OK, pcf->get_clock_source(s_context, &source));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLOCK_SOURCE_EXTERNAL, source);
  TEST_ASSERT_EQUAL_INT(HAL_OK, ds->get_clock_source(s_context, &source));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLOCK_SOURCE_EXTERNAL, source);
}

void test_pcf8563_provider_translates_datetime_and_flags(void) {
  const jh_rtc_provider_ops_t *provider =
      jh_rtc_i2c_provider_get_ops(HAL_RTC_CHIP_PCF8563);
  hal_rtc_config_t config = config_for(
      HAL_RTC_CHIP_PCF8563, (uint8_t)HAL_RTC_PCF8563_DEFAULT_I2C_ADDR);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->initialize(s_context, &config));

  const uint8_t datetime_registers[] = {
      0x56u, 0x34u, 0x12u, 0x24u, 0x00u, 0x05u, 0x26u,
  };
  hal_mock_i2c_inject_rx(datetime_registers, (int)sizeof(datetime_registers));
  hal_rtc_datetime_t datetime = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->get_datetime(s_context, &datetime));
  TEST_ASSERT_EQUAL_UINT16(2026u, datetime.year);
  TEST_ASSERT_EQUAL_UINT8(5u, datetime.month);
  TEST_ASSERT_EQUAL_UINT8(24u, datetime.day);
  TEST_ASSERT_EQUAL_UINT8(12u, datetime.hour);
  TEST_ASSERT_EQUAL_UINT8(34u, datetime.minute);
  TEST_ASSERT_EQUAL_UINT8(56u, datetime.second);

  const uint8_t flag_register[] = {0x0Cu};
  hal_mock_i2c_inject_rx(flag_register, (int)sizeof(flag_register));
  uint8_t flags = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->get_and_clear_flags(s_context, &flags));
  TEST_ASSERT_EQUAL_HEX8(HAL_RTC_FLAG_ALARM | HAL_RTC_FLAG_TIMER, flags);

  provider->deinitialize(s_context);
}

void test_ds3231_provider_translates_datetime_and_capabilities(void) {
  const jh_rtc_provider_ops_t *provider =
      jh_rtc_i2c_provider_get_ops(HAL_RTC_CHIP_DS3231);
  hal_rtc_config_t config =
      config_for(HAL_RTC_CHIP_DS3231, (uint8_t)HAL_RTC_DS3231_DEFAULT_I2C_ADDR);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->initialize(s_context, &config));

  const uint8_t datetime_and_status[] = {
      0x56u, 0x34u, 0x12u, 0x02u, 0x24u, 0x05u, 0x26u, 0x00u,
  };
  hal_mock_i2c_inject_rx(datetime_and_status, (int)sizeof(datetime_and_status));
  hal_rtc_datetime_t datetime = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->get_datetime(s_context, &datetime));
  TEST_ASSERT_EQUAL_UINT16(2026u, datetime.year);
  TEST_ASSERT_EQUAL_UINT8(5u, datetime.month);
  TEST_ASSERT_EQUAL_UINT8(24u, datetime.day);
  TEST_ASSERT_EQUAL_UINT8(12u, datetime.hour);
  TEST_ASSERT_TRUE(datetime.clock_integrity);

  hal_mock_i2c_reset_write_log();
  const uint8_t disabled_clkout_registers[] = {
      0x08u, /* status: EN32kHz */
      0xC0u, /* control: EOSC + BBSQW */
  };
  hal_mock_i2c_inject_rx(disabled_clkout_registers,
                         (int)sizeof(disabled_clkout_registers));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, provider->set_clkout_mode(s_context, HAL_RTC_CLKOUT_DISABLED));
  uint8_t frame[2] = {};
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x0Eu, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0x04u, frame[1]);

  hal_rtc_clkout_mode_t clkout_mode = HAL_RTC_CLKOUT_1_HZ;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->get_clkout_mode(s_context, &clkout_mode));
  TEST_ASSERT_EQUAL_INT(HAL_RTC_CLKOUT_DISABLED, clkout_mode);

  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, provider->set_interrupt_enable(
                                              s_context, HAL_RTC_IRQ_TIMER));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, provider->set_clkout_mode(
                                              s_context, HAL_RTC_CLKOUT_32_HZ));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        provider->set_timer(s_context, HAL_RTC_TIMER_1_HZ, 1u));

  hal_rtc_alarm_t alarm = {};
  alarm.day_enabled = true;
  alarm.day = 1u;
  alarm.weekday_enabled = true;
  alarm.weekday = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        provider->set_alarm(s_context, &alarm));

  provider->deinitialize(s_context);
}

void test_ds3231_provider_propagates_i2c_failures(void) {
  const jh_rtc_provider_ops_t *provider =
      jh_rtc_i2c_provider_get_ops(HAL_RTC_CHIP_DS3231);
  hal_rtc_config_t config =
      config_for(HAL_RTC_CHIP_DS3231, (uint8_t)HAL_RTC_DS3231_DEFAULT_I2C_ADDR);
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->initialize(s_context, &config));

  hal_mock_i2c_set_busy(true);
  hal_rtc_datetime_t datetime = {};
  float temperature_c = 0.0f;
  bool integrity = false;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->get_datetime(s_context, &datetime));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->set_datetime(s_context, &datetime));
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        provider->get_temperature(s_context, &temperature_c));
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        provider->get_clock_integrity(s_context, &integrity));
  TEST_ASSERT_EQUAL_INT(
      HAL_EIO, provider->set_clkout_mode(s_context, HAL_RTC_CLKOUT_DISABLED));
  hal_mock_i2c_set_busy(false);

  provider->deinitialize(s_context);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_provider_selection_reports_chip_metadata);
  RUN_TEST(test_pcf8563_provider_translates_datetime_and_flags);
  RUN_TEST(test_ds3231_provider_translates_datetime_and_capabilities);
  RUN_TEST(test_ds3231_provider_propagates_i2c_failures);
  return UNITY_END();
}
