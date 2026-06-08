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
#define PCF8563_REG_CS2 0x01u
#define PCF8563_REG_CLKOUT 0x0Du
#define PCF8563_REG_TIMER_CONTROL 0x0Eu
#define PCF8563_REG_TIMER_VALUE 0x0Fu

/* Datasheet anchors used by these tests (PCF8563):
 * - Table 6: Control_status_2 (AF/TF flag clear behavior, AIE/TIE, TI_TP)
 * - Table 8 + VL description: VL_seconds bit7 meaning
 * - Table 15/16: Century_months register (C=0 => 20xx, C=1 => 19xx)
 * - Tables 18..21: Alarm AE_x bits and BCD field limits
 * - Table 22: CLKOUT_control FE/FD mapping (32.768k/1.024k/32/1 Hz)
 * - Table 23/24: Timer_control TE/TD and timer value register
 */

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

void test_probe_reads_control_status_1(void) {
    const uint8_t probe_rx[] = {0x00u};
    hal_mock_i2c_inject_rx(probe_rx, (int)sizeof(probe_rx));

    TEST_ASSERT_TRUE(pcf8563_probe(&s_dev));

    uint8_t f[2] = {};
    TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
}

void test_probe_rejects_null(void) {
    TEST_ASSERT_FALSE(pcf8563_probe(NULL));
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

void test_get_datetime_returns_false_for_invalid_calendar_values(void) {
    const uint8_t regs[] = {
        0x00u, /* sec */
        0x00u, /* min */
        0x00u, /* hour */
        0x01u, /* day */
        0x00u, /* weekday */
        0x13u, /* invalid month 13 */
        0x26u,
    };
    hal_mock_i2c_inject_rx(regs, (int)sizeof(regs));

    pcf8563_datetime_t out = {};
    TEST_ASSERT_FALSE(pcf8563_get_datetime(&s_dev, &out));
}

void test_set_datetime_validates_ranges(void) {
    pcf8563_datetime_t dt = {};
    dt.second = 60u;
    dt.minute = 0u;
    dt.hour = 0u;
    dt.day = 1u;
    dt.weekday = 0u;
    dt.month = 1u;
    dt.year = 2026u;

    TEST_ASSERT_FALSE(pcf8563_set_datetime(&s_dev, &dt));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_write_frame_count());
}

void test_get_clock_integrity_reads_vl_bit(void) {
    bool ok = false;

    const uint8_t good_rx[] = {0x00u};
    hal_mock_i2c_inject_rx(good_rx, (int)sizeof(good_rx));
    TEST_ASSERT_TRUE(pcf8563_get_clock_integrity(&s_dev, &ok));
    TEST_ASSERT_TRUE(ok);

    const uint8_t bad_rx[] = {0x80u};
    hal_mock_i2c_inject_rx(bad_rx, (int)sizeof(bad_rx));
    TEST_ASSERT_TRUE(pcf8563_get_clock_integrity(&s_dev, &ok));
    TEST_ASSERT_FALSE(ok);
}

void test_interrupt_enable_preserves_unrelated_bits_and_sets_aie_tie(void) {
    const uint8_t cs2_rx[] = {0x1Cu}; /* TI_TP|AF|TF set, AIE/TIE clear */
    hal_mock_i2c_inject_rx(cs2_rx, (int)sizeof(cs2_rx));
    hal_mock_i2c_reset_write_log();

    TEST_ASSERT_TRUE(pcf8563_set_interrupt_enable(&s_dev, true, false));

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
    TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(PCF8563_REG_CS2, f[0]);
    TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(PCF8563_REG_CS2, f[0]);
    TEST_ASSERT_EQUAL_UINT8(0x1Eu, f[1]); /* preserve bit4/3/2, set AIE only */
}

void test_get_interrupt_enable_decodes_aie_and_tie(void) {
    const uint8_t cs2_rx[] = {0x03u};
    hal_mock_i2c_inject_rx(cs2_rx, (int)sizeof(cs2_rx));

    bool alarm = false;
    bool timer = false;
    TEST_ASSERT_TRUE(pcf8563_get_interrupt_enable(&s_dev, &alarm, &timer));
    TEST_ASSERT_TRUE(alarm);
    TEST_ASSERT_TRUE(timer);
}

void test_get_and_clear_flags_reports_af_tf_and_clears_only_flags(void) {
    const uint8_t cs2_rx[] = {0x1Fu}; /* TI_TP|AF|TF|AIE|TIE */
    hal_mock_i2c_inject_rx(cs2_rx, (int)sizeof(cs2_rx));
    hal_mock_i2c_reset_write_log();

    bool af = false;
    bool tf = false;
    TEST_ASSERT_TRUE(pcf8563_get_and_clear_flags(&s_dev, &af, &tf));
    TEST_ASSERT_TRUE(af);
    TEST_ASSERT_TRUE(tf);

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(2, get_frame(1, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(PCF8563_REG_CS2, f[0]);
    TEST_ASSERT_EQUAL_UINT8(0x13u, f[1]); /* keep TI_TP|AIE|TIE, clear AF|TF */
}

void test_clkout_set_and_get_modes_cover_all_enums(void) {
    struct {
        pcf8563_clkout_mode_t mode;
        uint8_t reg;
    } const vectors[] = {
        {PCF8563_CLKOUT_DISABLED, 0x00u},
        {PCF8563_CLKOUT_1_HZ, 0x83u},
        {PCF8563_CLKOUT_32_HZ, 0x82u},
        {PCF8563_CLKOUT_1024_HZ, 0x81u},
        {PCF8563_CLKOUT_32768_HZ, 0x80u},
    };

    for (size_t i = 0; i < (sizeof(vectors) / sizeof(vectors[0])); ++i) {
        hal_mock_i2c_reset_write_log();
        TEST_ASSERT_TRUE(pcf8563_set_clkout_mode(&s_dev, vectors[i].mode));

        uint8_t f[4] = {};
        TEST_ASSERT_EQUAL_INT(2, get_frame(0, f, sizeof(f)));
        TEST_ASSERT_EQUAL_UINT8(PCF8563_REG_CLKOUT, f[0]);
        TEST_ASSERT_EQUAL_UINT8(vectors[i].reg, f[1]);

        hal_mock_i2c_inject_rx(&vectors[i].reg, 1);
        pcf8563_clkout_mode_t out = PCF8563_CLKOUT_DISABLED;
        TEST_ASSERT_TRUE(pcf8563_get_clkout_mode(&s_dev, &out));
        TEST_ASSERT_EQUAL_INT(vectors[i].mode, out);
    }
}

void test_get_clkout_mode_rejects_invalid_encoding(void) {
    const uint8_t invalid = 0x01u; /* FE=0 with nonzero FD pattern is invalid for this decoder */
    hal_mock_i2c_inject_rx(&invalid, 1);

    pcf8563_clkout_mode_t out = PCF8563_CLKOUT_DISABLED;
    TEST_ASSERT_FALSE(pcf8563_get_clkout_mode(&s_dev, &out));
}

void test_timer_set_and_get_cover_all_modes(void) {
    struct {
        pcf8563_timer_clock_t mode;
        uint8_t reg;
    } const vectors[] = {
        {PCF8563_TIMER_DISABLED, 0x03u},
        {PCF8563_TIMER_1_60_HZ, 0x83u},
        {PCF8563_TIMER_1_HZ, 0x82u},
        {PCF8563_TIMER_64_HZ, 0x81u},
        {PCF8563_TIMER_4096_HZ, 0x80u},
    };

    for (size_t i = 0; i < (sizeof(vectors) / sizeof(vectors[0])); ++i) {
        hal_mock_i2c_reset_write_log();
        TEST_ASSERT_TRUE(pcf8563_set_timer(&s_dev, vectors[i].mode, 0xA5u));

        uint8_t f[4] = {};
        TEST_ASSERT_EQUAL_INT(3, get_frame(0, f, sizeof(f)));
        TEST_ASSERT_EQUAL_UINT8(PCF8563_REG_TIMER_CONTROL, f[0]);
        TEST_ASSERT_EQUAL_UINT8(vectors[i].reg, f[1]);
        TEST_ASSERT_EQUAL_UINT8(0xA5u, f[2]);

        const uint8_t rx[] = {vectors[i].reg, 0x5Au};
        hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));
        pcf8563_timer_clock_t out_mode = 0xFFu;
        uint8_t out_count = 0u;
        TEST_ASSERT_TRUE(pcf8563_get_timer(&s_dev, &out_mode, &out_count));
        TEST_ASSERT_EQUAL_UINT8(vectors[i].mode, out_mode);
        TEST_ASSERT_EQUAL_UINT8(0x5Au, out_count);
    }
}

void test_set_timer_rejects_invalid_mode(void) {
    TEST_ASSERT_FALSE(pcf8563_set_timer(&s_dev, (pcf8563_timer_clock_t)0x99u, 0x10u));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_write_frame_count());
}

void test_set_and_get_alarm_full_encoding(void) {
    pcf8563_alarm_t alarm = {};
    alarm.minute_enabled = true;
    alarm.minute = 59u;
    alarm.hour_enabled = true;
    alarm.hour = 23u;
    alarm.day_enabled = true;
    alarm.day = 31u;
    alarm.weekday_enabled = false;
    alarm.weekday = 6u;

    TEST_ASSERT_TRUE(pcf8563_set_alarm(&s_dev, &alarm));

    uint8_t f[8] = {};
    TEST_ASSERT_EQUAL_INT(5, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x09u, f[0]);
    TEST_ASSERT_EQUAL_UINT8(0x59u, f[1]);
    TEST_ASSERT_EQUAL_UINT8(0x23u, f[2]);
    TEST_ASSERT_EQUAL_UINT8(0x31u, f[3]);
    TEST_ASSERT_EQUAL_UINT8(0x80u, f[4]); /* weekday disabled */

    const uint8_t rx[] = {0x80u, 0x80u, 0x01u, 0x05u};
    hal_mock_i2c_inject_rx(rx, (int)sizeof(rx));

    pcf8563_alarm_t out = {};
    TEST_ASSERT_TRUE(pcf8563_get_alarm(&s_dev, &out));
    TEST_ASSERT_FALSE(out.minute_enabled);
    TEST_ASSERT_FALSE(out.hour_enabled);
    TEST_ASSERT_TRUE(out.day_enabled);
    TEST_ASSERT_TRUE(out.weekday_enabled);
    TEST_ASSERT_EQUAL_UINT8(1u, out.day);
    TEST_ASSERT_EQUAL_UINT8(5u, out.weekday);
}

void test_alarm_validation_rejects_invalid_ranges(void) {
    pcf8563_alarm_t alarm = {};
    alarm.minute_enabled = true;
    alarm.minute = 60u;
    TEST_ASSERT_FALSE(pcf8563_set_alarm(&s_dev, &alarm));

    alarm.minute = 0u;
    alarm.minute_enabled = false;
    alarm.hour_enabled = true;
    alarm.hour = 24u;
    TEST_ASSERT_FALSE(pcf8563_set_alarm(&s_dev, &alarm));
}

void test_null_argument_guards_for_remaining_api(void) {
    bool b0 = false;
    bool b1 = false;
    uint8_t count = 0;
    pcf8563_clkout_mode_t clk = PCF8563_CLKOUT_DISABLED;
    pcf8563_timer_clock_t tmode = PCF8563_TIMER_DISABLED;
    pcf8563_datetime_t dt = {};
    pcf8563_alarm_t alarm = {};

    TEST_ASSERT_FALSE(pcf8563_get_datetime(NULL, &dt));
    TEST_ASSERT_FALSE(pcf8563_get_datetime(&s_dev, NULL));
    TEST_ASSERT_FALSE(pcf8563_set_datetime(NULL, &dt));
    TEST_ASSERT_FALSE(pcf8563_set_datetime(&s_dev, NULL));
    TEST_ASSERT_FALSE(pcf8563_get_clock_integrity(NULL, &b0));
    TEST_ASSERT_FALSE(pcf8563_get_clock_integrity(&s_dev, NULL));
    TEST_ASSERT_FALSE(pcf8563_set_interrupt_enable(NULL, true, true));
    TEST_ASSERT_FALSE(pcf8563_get_interrupt_enable(NULL, &b0, &b1));
    TEST_ASSERT_FALSE(pcf8563_get_interrupt_enable(&s_dev, NULL, &b1));
    TEST_ASSERT_FALSE(pcf8563_get_and_clear_flags(NULL, &b0, &b1));
    TEST_ASSERT_FALSE(pcf8563_get_and_clear_flags(&s_dev, NULL, &b1));
    TEST_ASSERT_FALSE(pcf8563_set_clkout_mode(NULL, PCF8563_CLKOUT_1_HZ));
    TEST_ASSERT_FALSE(pcf8563_get_clkout_mode(NULL, &clk));
    TEST_ASSERT_FALSE(pcf8563_get_clkout_mode(&s_dev, NULL));
    TEST_ASSERT_FALSE(pcf8563_set_timer(NULL, PCF8563_TIMER_1_HZ, 1u));
    TEST_ASSERT_FALSE(pcf8563_get_timer(NULL, &tmode, &count));
    TEST_ASSERT_FALSE(pcf8563_get_timer(&s_dev, NULL, &count));
    TEST_ASSERT_FALSE(pcf8563_get_timer(&s_dev, &tmode, NULL));
    TEST_ASSERT_FALSE(pcf8563_set_alarm(NULL, &alarm));
    TEST_ASSERT_FALSE(pcf8563_set_alarm(&s_dev, NULL));
    TEST_ASSERT_FALSE(pcf8563_get_alarm(NULL, &alarm));
    TEST_ASSERT_FALSE(pcf8563_get_alarm(&s_dev, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_probe_reads_control_status_1);
    RUN_TEST(test_probe_rejects_null);
    RUN_TEST(test_set_datetime_2000s_leaves_century_bit_clear);
    RUN_TEST(test_set_datetime_1900s_sets_century_bit);
    RUN_TEST(test_get_datetime_century_bit_clear_reads_2000s);
    RUN_TEST(test_get_datetime_century_bit_set_reads_1900s);
    RUN_TEST(test_set_then_get_roundtrip_preserves_2000s_year);
    RUN_TEST(test_get_datetime_returns_false_for_invalid_calendar_values);
    RUN_TEST(test_set_datetime_validates_ranges);
    RUN_TEST(test_get_clock_integrity_reads_vl_bit);
    RUN_TEST(test_interrupt_enable_preserves_unrelated_bits_and_sets_aie_tie);
    RUN_TEST(test_get_interrupt_enable_decodes_aie_and_tie);
    RUN_TEST(test_get_and_clear_flags_reports_af_tf_and_clears_only_flags);
    RUN_TEST(test_clkout_set_and_get_modes_cover_all_enums);
    RUN_TEST(test_get_clkout_mode_rejects_invalid_encoding);
    RUN_TEST(test_timer_set_and_get_cover_all_modes);
    RUN_TEST(test_set_timer_rejects_invalid_mode);
    RUN_TEST(test_set_and_get_alarm_full_encoding);
    RUN_TEST(test_alarm_validation_rejects_invalid_ranges);
    RUN_TEST(test_null_argument_guards_for_remaining_api);
    return UNITY_END();
}
