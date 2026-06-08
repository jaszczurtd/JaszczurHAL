/**
 * @file test_pcf8563_driver.cpp
 * @brief Register-level host tests for the PCF8563 RTC driver.
 *
 * These exercise the real pcf8563.cpp I2C transfer path against the mock I2C
 * backend (not the hal_rtc facade), so they pin the on-the-wire register
 * encoding the chip actually sees.
 *
 * Regression focus: the century bit in the Months/century register (0x07,
 * bit 7). Per the NXP PCF8563 datasheet (Table 13): C=0 -> 20xx, C=1 -> 19xx.
 */

#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/pcf8563/pcf8563.h"
#include "utils/unity.h"

#define PCF8563_I2C_ADDR 0x51u
#define PCF8563_CENTURY_BIT 0x80u

/* Write-frame layout for a SECONDS-based block write:
 *   frame[0] = register pointer (0x02)
 *   frame[1..7] = seconds, minutes, hours, day, weekday, month/century, year
 * so the month/century byte is frame[6] and the year byte is frame[7]. */
#define FRAME_MONTH_CENTURY 6
#define FRAME_YEAR 7

static pcf8563_t s_dev;

void setUp(void) {
    hal_i2c_init_bus(0, 4, 5, HAL_I2C_CLOCK_STANDARD_HZ);
    hal_mock_i2c_set_busy(false);
    hal_mock_i2c_reset_write_log();
    s_dev.i2c_bus = 0u;
    s_dev.i2c_addr = PCF8563_I2C_ADDR;
}

void tearDown(void) {
}

static int get_frame(int idx, uint8_t *buf, int max) {
    int n = hal_mock_i2c_get_write_frame(idx, buf, max);
    TEST_ASSERT_TRUE_MESSAGE(n >= 0, "expected write frame is missing");
    return n;
}

/* ── Write path ─────────────────────────────────────────────────────────── */

void test_set_datetime_2000s_leaves_century_bit_clear(void) {
    pcf8563_datetime_t dt = {};
    dt.second = 12u;
    dt.minute = 34u;
    dt.hour = 5u;
    dt.day = 24u;
    dt.weekday = 0u;
    dt.month = 5u;
    dt.year = 2026u;
    dt.clock_integrity = true;

    TEST_ASSERT_TRUE(pcf8563_set_datetime(&s_dev, &dt));

    uint8_t f[8] = {};
    TEST_ASSERT_EQUAL_INT(8, get_frame(0, f, sizeof(f)));
    /* C=0 for 20xx. */
    TEST_ASSERT_EQUAL_UINT8(0u, f[FRAME_MONTH_CENTURY] & PCF8563_CENTURY_BIT);
    /* Month nibble preserved, year stored as the 2-digit offset from 2000. */
    TEST_ASSERT_EQUAL_UINT8(0x05u, f[FRAME_MONTH_CENTURY] & 0x1Fu);
    TEST_ASSERT_EQUAL_UINT8(0x26u, f[FRAME_YEAR]); /* BCD 26 */
}

void test_set_datetime_1900s_sets_century_bit(void) {
    pcf8563_datetime_t dt = {};
    dt.second = 0u;
    dt.minute = 0u;
    dt.hour = 0u;
    dt.day = 1u;
    dt.weekday = 2u;
    dt.month = 12u;
    dt.year = 1985u;
    dt.clock_integrity = true;

    TEST_ASSERT_TRUE(pcf8563_set_datetime(&s_dev, &dt));

    uint8_t f[8] = {};
    TEST_ASSERT_EQUAL_INT(8, get_frame(0, f, sizeof(f)));
    /* C=1 for 19xx. */
    TEST_ASSERT_EQUAL_UINT8(PCF8563_CENTURY_BIT,
                            f[FRAME_MONTH_CENTURY] & PCF8563_CENTURY_BIT);
    TEST_ASSERT_EQUAL_UINT8(0x12u, f[FRAME_MONTH_CENTURY] & 0x1Fu); /* BCD 12 */
    TEST_ASSERT_EQUAL_UINT8(0x85u, f[FRAME_YEAR]);                  /* BCD 85 */
}

/* ── Read path ──────────────────────────────────────────────────────────── */

void test_get_datetime_century_bit_clear_reads_2000s(void) {
    /* seconds, minutes, hours, day, weekday, month(+C), year */
    const uint8_t regs[] = {
        0x12u, /* 12 s */
        0x34u, /* 34 min */
        0x05u, /* 5 h */
        0x24u, /* day 24 */
        0x00u, /* weekday 0 */
        0x05u, /* month 5, century bit clear -> 20xx */
        0x26u, /* year 26 */
    };
    hal_mock_i2c_inject_rx(regs, (int)sizeof(regs));

    pcf8563_datetime_t out = {};
    TEST_ASSERT_TRUE(pcf8563_get_datetime(&s_dev, &out));
    TEST_ASSERT_EQUAL_UINT16(2026u, out.year);
    TEST_ASSERT_EQUAL_UINT8(5u, out.month);
    TEST_ASSERT_EQUAL_UINT8(24u, out.day);
    TEST_ASSERT_TRUE(out.clock_integrity);
}

void test_get_datetime_century_bit_set_reads_1900s(void) {
    const uint8_t regs[] = {
        0x00u,                         /* 0 s, VL clear */
        0x00u,                         /* 0 min */
        0x00u,                         /* 0 h */
        0x01u,                         /* day 1 */
        0x02u,                         /* weekday 2 */
        (uint8_t)(0x12u | PCF8563_CENTURY_BIT), /* month 12, century set -> 19xx */
        0x85u,                         /* year 85 */
    };
    hal_mock_i2c_inject_rx(regs, (int)sizeof(regs));

    pcf8563_datetime_t out = {};
    TEST_ASSERT_TRUE(pcf8563_get_datetime(&s_dev, &out));
    TEST_ASSERT_EQUAL_UINT16(1985u, out.year);
    TEST_ASSERT_EQUAL_UINT8(12u, out.month);
    TEST_ASSERT_EQUAL_UINT8(1u, out.day);
}

/* Round-trip: a 2000s timestamp written and read back through the driver must
 * survive (guards against re-inverting both ends together). */
void test_set_then_get_roundtrip_preserves_2000s_year(void) {
    pcf8563_datetime_t in = {};
    in.second = 45u;
    in.minute = 30u;
    in.hour = 23u;
    in.day = 31u;
    in.weekday = 6u;
    in.month = 7u;
    in.year = 2099u;
    in.clock_integrity = true;

    TEST_ASSERT_TRUE(pcf8563_set_datetime(&s_dev, &in));

    uint8_t f[8] = {};
    TEST_ASSERT_EQUAL_INT(8, get_frame(0, f, sizeof(f)));
    /* Feed the written register block (frame minus the reg pointer) back. */
    hal_mock_i2c_inject_rx(&f[1], 7);

    pcf8563_datetime_t out = {};
    TEST_ASSERT_TRUE(pcf8563_get_datetime(&s_dev, &out));
    TEST_ASSERT_EQUAL_UINT16(in.year, out.year);
    TEST_ASSERT_EQUAL_UINT8(in.month, out.month);
    TEST_ASSERT_EQUAL_UINT8(in.day, out.day);
    TEST_ASSERT_EQUAL_UINT8(in.hour, out.hour);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_datetime_2000s_leaves_century_bit_clear);
    RUN_TEST(test_set_datetime_1900s_sets_century_bit);
    RUN_TEST(test_get_datetime_century_bit_clear_reads_2000s);
    RUN_TEST(test_get_datetime_century_bit_set_reads_1900s);
    RUN_TEST(test_set_then_get_roundtrip_preserves_2000s_year);
    return UNITY_END();
}
