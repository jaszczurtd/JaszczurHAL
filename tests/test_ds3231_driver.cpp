/**
 * @file test_ds3231_driver.cpp
 * @brief Register-level host tests for the DS3231 RTC driver.
 *
 * These exercise the real ds3231.cpp I2C transfer path against the mock I2C
 * backend (not the hal_rtc facade), so they pin the on-the-wire register
 * encoding the chip actually sees — not a driver-internal round-trip.
 *
 * Ground truth: Maxim/Analog DS3231 datasheet, "Timekeeping Registers":
 *   - 0x00 seconds (BCD, bit7 reserved)
 *   - 0x02 hours: bit6 = 12/24h mode (1 = 12h), bit5 = AM/PM (12h) / 20h (24h)
 *   - 0x05 month: bit7 = century, bits[6:0] = BCD month
 *   - 0x11/0x12 temperature: signed MSB integer, LSB[7:6] = 0.25 C fraction
 */

#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/ds3231/ds3231.h"
#include "utils/unity.h"

#include <math.h>

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

static void inject1(uint8_t b) {
    hal_mock_i2c_inject_rx(&b, 1);
}

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
    TEST_ASSERT_EQUAL_UINT8(0x05u, f[0]);          /* month register */
    TEST_ASSERT_EQUAL_UINT8(0x11u, f[1]);          /* BCD 11, bit7 clear */
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
    TEST_ASSERT_EQUAL_UINT8(0x23u, f[1]);          /* BCD 23, mode bit clear */
    TEST_ASSERT_EQUAL_UINT8(0u, f[1] & 0x40u);
}

void test_set_hour_12h_pm_sets_mode_and_ampm_bits(void) {
    inject1(0x40u); /* current reg 0x02: 12h mode */
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
    RUN_TEST(test_get_temperature_positive);
    RUN_TEST(test_get_temperature_negative);
    return UNITY_END();
}
