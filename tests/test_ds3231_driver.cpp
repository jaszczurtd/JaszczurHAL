/**
 * @file test_ds3231_driver.cpp
 * @brief Register-level host tests for the DS3231 RTC driver.
 *
 * These exercise the real ds3231.cpp I2C transfer path against the mock I2C
 * backend (not the hal_rtc facade), so they pin the on-the-wire register
 * encoding the chip actually sees - not a driver-internal round-trip.
 *
 * Ground truth: Maxim/Analog DS3231 datasheet, "Timekeeping Registers":
 *   - 0x00 seconds (BCD, bit7 reserved)
 *   - 0x02 hours: bit6 = 12/24h mode (1 = 12h), bit5 = AM/PM (12h) / 20h (24h)
 *   - 0x05 month: bit7 = century, bits[6:0] = BCD month
 *   - 0x11/0x12 temperature: signed MSB integer, LSB[7:6] = 0.25 C fraction
 */

#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/drivers/ds3231/ds3231.h"
#include "utils/unity.h"

#include <math.h>
#include <time.h>

/* Datasheet anchors used by these tests (DS3231):
 * - Timekeeping registers 0x00..0x06: BCD fields, 12/24h bit and month century
 * bit
 * - Alarm 1/2 register maps (A1Mx/A2Mx, DY/DT) and match-mask semantics
 * - Control register 0x0E: EOSC/BBSQW/RS2/RS1/INTCN/A2IE/A1IE
 * - Status register 0x0F: OSF/EN32kHz/A2F/A1F
 * - Temperature registers 0x11/0x12: signed quarter-degree format
 */

#define DS3231_I2C_ADDR 0x68u

static DS3231 *s_rtc = nullptr;

void setUp(void) {
  hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
  hal_mock_i2c_set_busy(false);
  hal_mock_i2c_reset_write_log();
  s_rtc = new DS3231(0u, DS3231_I2C_ADDR);
}

void tearDown(void) {
  delete s_rtc;
  s_rtc = nullptr;
}

/* Last logged write frame (a getter logs a [reg] pointer frame before its
 * read, so a setter that reads-modifies-writes leaves the real write last). */
static int last_write_frame(uint8_t *buf, int max) {
  int count = hal_mock_i2c_get_write_frame_count();
  TEST_ASSERT_TRUE_MESSAGE(count > 0, "no write frame logged");
  int n = hal_mock_i2c_get_write_frame(count - 1, buf, max);
  TEST_ASSERT_TRUE_MESSAGE(n >= 0, "missing write frame");
  return n;
}

static int get_frame(int idx, uint8_t *buf, int max) {
  int n = hal_mock_i2c_get_write_frame(idx, buf, max);
  TEST_ASSERT_TRUE_MESSAGE(n >= 0, "missing write frame");
  return n;
}

static void inject1(uint8_t b) { hal_mock_i2c_inject_rx(&b, 1); }

/* ── Month / century (reg 0x05, bit7) ───────────────────────────────────── */

void test_get_month_decodes_without_century(void) {
  inject1(0x12u); /* BCD 12, century clear */
  bool century = true;
  byte m = s_rtc->getMonth(century);
  TEST_ASSERT_EQUAL_UINT8(12u, m);
  TEST_ASSERT_FALSE(century);
}

void test_get_month_decodes_century_bit(void) {
  inject1((uint8_t)(0x12u | 0x80u)); /* BCD 12, century set */
  bool century = false;
  byte m = s_rtc->getMonth(century);
  TEST_ASSERT_EQUAL_UINT8(12u, m);
  TEST_ASSERT_TRUE(century);
}

void test_set_month_writes_bcd_with_century_clear(void) {
  s_rtc->setMonth(11u);
  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[0]); /* month register */
  TEST_ASSERT_EQUAL_UINT8(0x11u, f[1]); /* BCD 11, bit7 clear */
  TEST_ASSERT_EQUAL_UINT8(0u, f[1] & 0x80u);
}

/* ── Hours 12/24h (reg 0x02, bit6 mode, bit5 AM/PM) ──────────────────────── */

void test_get_hour_24h_mode(void) {
  inject1(0x23u); /* BCD 23, mode bit clear -> 24h */
  bool h12 = true, pm = true;
  byte hour = s_rtc->getHour(h12, pm);
  TEST_ASSERT_EQUAL_UINT8(23u, hour);
  TEST_ASSERT_FALSE(h12);
}

void test_get_hour_12h_pm(void) {
  inject1((uint8_t)(0x07u | 0x40u | 0x20u)); /* BCD 7 | 12h | PM */
  bool h12 = false, pm = false;
  byte hour = s_rtc->getHour(h12, pm);
  TEST_ASSERT_EQUAL_UINT8(7u, hour);
  TEST_ASSERT_TRUE(h12);
  TEST_ASSERT_TRUE(pm);
}

void test_get_hour_12h_am(void) {
  inject1((uint8_t)(0x11u | 0x40u)); /* BCD 11 | 12h, AM */
  bool h12 = false, pm = true;
  byte hour = s_rtc->getHour(h12, pm);
  TEST_ASSERT_EQUAL_UINT8(11u, hour);
  TEST_ASSERT_TRUE(h12);
  TEST_ASSERT_FALSE(pm);
}

void test_set_hour_24h_writes_plain_bcd(void) {
  inject1(0x00u); /* current reg 0x02: 24h mode */
  s_rtc->setHour(23u);
  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x02u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x23u, f[1]); /* BCD 23, mode bit clear */
  TEST_ASSERT_EQUAL_UINT8(0u, f[1] & 0x40u);
}

void test_set_hour_12h_pm_sets_mode_and_ampm_bits(void) {
  inject1(0x40u);      /* current reg 0x02: 12h mode */
  s_rtc->setHour(13u); /* 1 PM */
  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x02u, f[0]);
  /* BCD 1 | 12h(0x40) | PM(0x20) */
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x01u | 0x40u | 0x20u), f[1]);
}

/* ── Seconds BCD (reg 0x00) ─────────────────────────────────────────────── */

void test_get_second_masks_and_decodes_bcd(void) {
  inject1((uint8_t)(0x59u | 0x80u)); /* BCD 59, bit7 must be masked off */
  TEST_ASSERT_EQUAL_UINT8(59u, s_rtc->getSecond());
}

void test_get_minute_date_dow_year_decode_bcd(void) {
  inject1(0x58u);
  TEST_ASSERT_EQUAL_UINT8(58u, s_rtc->getMinute());

  inject1(0x07u);
  TEST_ASSERT_EQUAL_UINT8(7u, s_rtc->getDoW());

  inject1(0x31u);
  TEST_ASSERT_EQUAL_UINT8(31u, s_rtc->getDate());

  inject1(0x26u);
  TEST_ASSERT_EQUAL_UINT8(26u, s_rtc->getYear());
}

void test_setters_write_expected_bcd_register_pairs(void) {
  /* setSecond additionally clears OSF in status register 0x0F. */
  inject1(0x80u);
  s_rtc->setSecond(59u);

  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x59u, f[1]);
  TEST_ASSERT_EQUAL_INT(1, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Fu, f[0]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(2, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Fu, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[1]);

  hal_mock_i2c_reset_write_log();
  s_rtc->setMinute(58u);
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x01u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x58u, f[1]);

  hal_mock_i2c_reset_write_log();
  s_rtc->setDoW(7u);
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x03u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x07u, f[1]);

  hal_mock_i2c_reset_write_log();
  s_rtc->setDate(31u);
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x04u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x31u, f[1]);

  hal_mock_i2c_reset_write_log();
  s_rtc->setYear(26u);
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x06u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x26u, f[1]);
}

void test_set_clock_mode_sets_and_clears_12h_bit(void) {
  inject1(0x23u); /* 24h, 23:00 */
  s_rtc->setClockMode(true);

  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x02u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x63u, f[1]); /* bit6 set, hour bits preserved */

  hal_mock_i2c_reset_write_log();
  inject1(0x63u);
  s_rtc->setClockMode(false);
  TEST_ASSERT_EQUAL_INT(2, last_write_frame(f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x02u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x23u, f[1]);
}

void test_adjust_writes_burst_starting_at_seconds_register(void) {
  DateTime dt(2026u, 5u, 24u, 13u, 14u, 15u);
  s_rtc->adjust(dt);

  uint8_t f[16] = {};
  TEST_ASSERT_EQUAL_INT(8, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x15u, f[1]);
  TEST_ASSERT_EQUAL_UINT8(0x14u, f[2]);
  TEST_ASSERT_EQUAL_UINT8(0x13u, f[3]);
  TEST_ASSERT_EQUAL_UINT8(0x07u, f[4]); /* dow Sunday=7 (DS3231 convention) */
  TEST_ASSERT_EQUAL_UINT8(0x24u, f[5]);
  TEST_ASSERT_EQUAL_UINT8(0x26u, f[6]);
}

void test_rtclib_now_decodes_timekeeping_register_block(void) {
  /* sec, min, hour, dow, date, month, year */
  const uint8_t regs[] = {0x56u, 0x34u, 0x12u, 0x02u, 0x24u, 0x05u, 0x26u};
  hal_mock_i2c_inject_rx(regs, (int)sizeof(regs));

  DateTime now = RTClib::now(*s_rtc);
  TEST_ASSERT_EQUAL_UINT16(2026u, now.year());
  TEST_ASSERT_EQUAL_UINT8(5u, now.month());
  TEST_ASSERT_EQUAL_UINT8(24u, now.day());
  TEST_ASSERT_EQUAL_UINT8(12u, now.hour());
  TEST_ASSERT_EQUAL_UINT8(34u, now.minute());
  TEST_ASSERT_EQUAL_UINT8(56u, now.second());
}

void test_alarm1_set_and_get_preserve_masks_and_day_mode(void) {
  s_rtc->setA1Time(17u, 13u, 25u, 40u, 0x05u, true, true, true);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x07u, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x40u | 0x80u), f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x08u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x25u, f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(2, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x09u, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x61u | 0x80u), f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(3, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Au, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x17u | 0x40u), f[1]);

  hal_mock_i2c_reset_write_log();
  const uint8_t a1_regs[] = {0xC0u, 0x59u, 0x65u, 0x45u};
  hal_mock_i2c_inject_rx(a1_regs, (int)sizeof(a1_regs));
  byte d = 0, h = 0, m = 0, s = 0, bits = 0;
  bool dy = false, h12 = false, pm = false;
  s_rtc->getA1Time(d, h, m, s, bits, dy, h12, pm, true);
  TEST_ASSERT_EQUAL_UINT8(40u, s);
  TEST_ASSERT_EQUAL_UINT8(59u, m);
  TEST_ASSERT_EQUAL_UINT8(5u, h);
  TEST_ASSERT_EQUAL_UINT8(5u, d);
  TEST_ASSERT_EQUAL_UINT8(0x01u, bits);
  TEST_ASSERT_TRUE(dy);
  TEST_ASSERT_TRUE(h12);
  TEST_ASSERT_TRUE(pm);
}

void test_alarm2_set_and_get_preserve_masks_and_day_mode(void) {
  s_rtc->setA2Time(7u, 23u, 59u, 0x70u, false, false, false);

  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Bu, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x59u | 0x80u), f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Cu, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x23u | 0x80u), f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(2, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Du, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x07u | 0x80u), f[1]);

  hal_mock_i2c_reset_write_log();
  const uint8_t a2_regs[] = {0xD9u, 0x65u, 0x47u};
  hal_mock_i2c_inject_rx(a2_regs, (int)sizeof(a2_regs));
  byte d = 0, h = 0, m = 0, bits = 0;
  bool dy = false, h12 = false, pm = false;
  s_rtc->getA2Time(d, h, m, bits, dy, h12, pm, true);
  TEST_ASSERT_EQUAL_UINT8(59u, m);
  TEST_ASSERT_EQUAL_UINT8(5u, h);
  TEST_ASSERT_EQUAL_UINT8(7u, d);
  TEST_ASSERT_EQUAL_UINT8(0x10u, bits);
  TEST_ASSERT_TRUE(dy);
  TEST_ASSERT_TRUE(h12);
  TEST_ASSERT_TRUE(pm);
}

void test_simple_alarm_helpers_program_expected_masks(void) {
  s_rtc->setAlarm1Simple(6u, 30u);
  uint8_t f[4] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x07u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x08u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x30u, f[1]);

  hal_mock_i2c_reset_write_log();
  s_rtc->setAlarm2Simple(7u, 45u);
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Bu, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x45u, f[1]);
  TEST_ASSERT_EQUAL_INT(2, get_frame(2, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Du, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x81u, f[1]);
}

void test_alarm_enable_disable_and_flags_paths(void) {
  /* turnOnAlarm(1): set INTCN|A1IE in control register 0x0E. */
  inject1(0x00u);
  s_rtc->turnOnAlarm(1u);
  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Eu, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[1]);

  hal_mock_i2c_reset_write_log();
  inject1(0x07u);
  s_rtc->turnOffAlarm(2u);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Eu, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x05u, f[1]);

  hal_mock_i2c_reset_write_log();
  inject1(0x01u);
  TEST_ASSERT_TRUE(s_rtc->checkAlarmEnabled(1u));
  inject1(0x02u);
  TEST_ASSERT_TRUE(s_rtc->checkAlarmEnabled(2u));

  hal_mock_i2c_reset_write_log();
  inject1(0x03u);
  TEST_ASSERT_TRUE(s_rtc->checkIfAlarm(1u, false));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());

  hal_mock_i2c_reset_write_log();
  inject1(0x03u);
  TEST_ASSERT_TRUE(s_rtc->checkIfAlarm(2u));
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Fu, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01u, f[1]);
}

void test_control_register_helpers_enable_oscillator_and_32khz(void) {
  /* enableOscillator(TF=false,battery=true,frequency=9) => clamp freq=3. */
  inject1(0x00u);
  s_rtc->enableOscillator(false, true, 9u);
  uint8_t f[2] = {};
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Eu, f[0]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x80u | 0x40u | 0x18u), f[1]);

  hal_mock_i2c_reset_write_log();
  inject1(0x00u);
  s_rtc->enable32kHz(true);
  TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x0Fu, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x08u, f[1]);

  hal_mock_i2c_reset_write_log();
  inject1(0x80u);
  TEST_ASSERT_FALSE(s_rtc->oscillatorCheck());
  inject1(0x00u);
  TEST_ASSERT_TRUE(s_rtc->oscillatorCheck());
}

void test_set_epoch_and_datetime_helpers(void) {
  /* 2000-01-01 00:00:00 UTC */
  hal_mock_i2c_reset_write_log();
  s_rtc->setEpoch((time_t)946684800, false);

  uint8_t f[2] = {};
  /* setSecond writes reg 0x00 first. */
  TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00u, f[1]);
}

void test_datetime_helpers_use_full_gregorian_calendar(void) {
  TEST_ASSERT_TRUE(isleapYear(0u)); /* Legacy 2000-based year offset. */
  TEST_ASSERT_FALSE(isleapYear(1900u));
  TEST_ASSERT_TRUE(isleapYear(2000u));

  const DateTime leap_day(2000u, 2u, 29u, 12u, 34u, 56u);
  TEST_ASSERT_EQUAL_UINT32(951827696u, leap_day.unixtime());
  TEST_ASSERT_EQUAL_UINT8(2u, leap_day.dayOfTheWeek());

  const DateTime roundtrip(leap_day.unixtime());
  TEST_ASSERT_EQUAL_UINT16(2000u, roundtrip.year());
  TEST_ASSERT_EQUAL_UINT8(2u, roundtrip.month());
  TEST_ASSERT_EQUAL_UINT8(29u, roundtrip.day());
  TEST_ASSERT_EQUAL_UINT8(12u, roundtrip.hour());
  TEST_ASSERT_EQUAL_UINT8(34u, roundtrip.minute());
  TEST_ASSERT_EQUAL_UINT8(56u, roundtrip.second());
}

void test_temperature_read_failure_returns_sentinel(void) {
  /* Busy bus forces endTransmission != 0, making readBytes() fail. */
  hal_mock_i2c_set_busy(true);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -9999.0f, s_rtc->getTemperature());
  hal_mock_i2c_set_busy(false);
}

/* ── Temperature (reg 0x11/0x12, signed, 0.25 C steps) ──────────────────── */

void test_get_temperature_positive(void) {
  const uint8_t regs[] = {0x19u, 0x40u}; /* 25 + 0.25 */
  hal_mock_i2c_inject_rx(regs, (int)sizeof(regs));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.25f, s_rtc->getTemperature());
}

void test_get_temperature_negative(void) {
  const uint8_t regs[] = {0xF5u, 0xC0u}; /* -10.25 (two's complement) */
  hal_mock_i2c_inject_rx(regs, (int)sizeof(regs));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -10.25f, s_rtc->getTemperature());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_month_decodes_without_century);
  RUN_TEST(test_get_month_decodes_century_bit);
  RUN_TEST(test_set_month_writes_bcd_with_century_clear);
  RUN_TEST(test_get_hour_24h_mode);
  RUN_TEST(test_get_hour_12h_pm);
  RUN_TEST(test_get_hour_12h_am);
  RUN_TEST(test_set_hour_24h_writes_plain_bcd);
  RUN_TEST(test_set_hour_12h_pm_sets_mode_and_ampm_bits);
  RUN_TEST(test_get_second_masks_and_decodes_bcd);
  RUN_TEST(test_get_minute_date_dow_year_decode_bcd);
  RUN_TEST(test_setters_write_expected_bcd_register_pairs);
  RUN_TEST(test_set_clock_mode_sets_and_clears_12h_bit);
  RUN_TEST(test_adjust_writes_burst_starting_at_seconds_register);
  RUN_TEST(test_rtclib_now_decodes_timekeeping_register_block);
  RUN_TEST(test_alarm1_set_and_get_preserve_masks_and_day_mode);
  RUN_TEST(test_alarm2_set_and_get_preserve_masks_and_day_mode);
  RUN_TEST(test_simple_alarm_helpers_program_expected_masks);
  RUN_TEST(test_alarm_enable_disable_and_flags_paths);
  RUN_TEST(test_control_register_helpers_enable_oscillator_and_32khz);
  RUN_TEST(test_set_epoch_and_datetime_helpers);
  RUN_TEST(test_datetime_helpers_use_full_gregorian_calendar);
  RUN_TEST(test_get_temperature_positive);
  RUN_TEST(test_get_temperature_negative);
  RUN_TEST(test_temperature_read_failure_returns_sentinel);
  return UNITY_END();
}
