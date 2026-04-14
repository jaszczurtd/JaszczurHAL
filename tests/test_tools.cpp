#include "utils/unity.h"
#include "utils/tools.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_set_millis(0);
    hal_mock_set_micros(0);
    hal_mock_adc_inject(0, 0);
}

void tearDown(void) {}

void test_tools_config_macro_aliases(void) {
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_ADC_BITS, ADC_BITS);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_NUMSAMPLES, NUMSAMPLES);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_TEMPERATURE_TABLES_SIZE, TEMPERATURE_TABLES_SIZE);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_BCOEFFICIENT, BCOEFFICIENT);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_TEMPERATURENOMINAL, TEMPERATURENOMINAL);

    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_PRINTABLE_BUFFER_SIZE, PRINTABLE_BUFFER_SIZE);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_PRINTABLE_PREFIX_SIZE, PRINTABLE_PREFIX_SIZE);

    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_EEPROM_FIRST_ADDR, EEPROM_FIRST_ADDR);
#ifdef SD_LOGGER
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_WRITE_INTERVAL_MS, WRITE_INTERVAL);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_EEPROM_LOGGER_ADDR, EEPROM_LOGGER_ADDR);
    TEST_ASSERT_EQUAL_INT(HAL_TOOLS_EEPROM_CRASH_ADDR, EEPROM_CRASH_ADDR);
    TEST_ASSERT_EQUAL_INT(8, EEPROM_FIRST_ADDR);
#else
    TEST_ASSERT_EQUAL_INT(0, EEPROM_FIRST_ADDR);
#endif
}

/* ── floatToDec / decToFloat ───────────────────────────────────────────── */

void test_floatToDec_positive(void) {
    int hi, lo;
    floatToDec(3.7f, &hi, &lo);
    TEST_ASSERT_EQUAL_INT(3, hi);
    TEST_ASSERT_EQUAL_INT(7, lo);
}

void test_floatToDec_zero(void) {
    int hi, lo;
    floatToDec(0.0f, &hi, &lo);
    TEST_ASSERT_EQUAL_INT(0, hi);
    TEST_ASSERT_EQUAL_INT(0, lo);
}

void test_floatToDec_null_pointers(void) {
    floatToDec(3.7f, NULL, NULL);  /* must not crash */
}

void test_decToFloat_basic(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, decToFloat(1, 2));
}

void test_decToFloat_roundtrip(void) {
    int hi, lo;
    floatToDec(2.5f, &hi, &lo);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, decToFloat(hi, lo));
}

/* ── filter / filterValue ──────────────────────────────────────────────── */

void test_filter_alpha_one(void) {
    /* alpha=1 → output = input */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, filter(1.0f, 5.0f, 100.0f));
}

void test_filter_alpha_zero(void) {
    /* alpha=0 → output = previous */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, filter(0.0f, 5.0f, 100.0f));
}

void test_filter_mid_alpha(void) {
    /* 0.5*3 + 0.5*7 = 5 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, filter(0.5f, 3.0f, 7.0f));
}

void test_filterValue_alpha_one(void) {
    /* alpha=1 → output = newValue */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.0f, filterValue(3.0f, 9.0f, 1.0f));
}

void test_filterValue_mid_alpha(void) {
    /* 0.5*10 + 0.5*20 = 15 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.0f, filterValue(20.0f, 10.0f, 0.5f));
}

/* ── adcCompe ──────────────────────────────────────────────────────────── */

void test_adcCompe_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, adcCompe(0));
}

void test_adcCompe_boundary_512(void) {
    /* 512 is not > 512, not 511, not 510 → falls through to else */
    TEST_ASSERT_EQUAL_INT(512, adcCompe(512));
}

void test_adcCompe_510(void) {
    TEST_ASSERT_EQUAL_INT(513, adcCompe(510));   /* 510 + 3 */
}

void test_adcCompe_511(void) {
    TEST_ASSERT_EQUAL_INT(516, adcCompe(511));   /* 511 + 5 */
}

void test_adcCompe_above_512(void) {
    TEST_ASSERT_EQUAL_INT(521, adcCompe(513));   /* 513 + 8 */
}

void test_adcCompe_above_1536(void) {
    TEST_ASSERT_EQUAL_INT(1553, adcCompe(1537)); /* 1537 + 16 */
}

void test_adcCompe_above_3584(void) {
    TEST_ASSERT_EQUAL_INT(3617, adcCompe(3585)); /* 3585 + 32 */
}

/* ── percentToGivenVal / percentFrom ───────────────────────────────────── */

void test_percentToGivenVal_50(void) {
    TEST_ASSERT_EQUAL_INT(50, percentToGivenVal(50.0f, 100));
}

void test_percentToGivenVal_100(void) {
    TEST_ASSERT_EQUAL_INT(100, percentToGivenVal(100.0f, 100));
}

void test_percentToGivenVal_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, percentToGivenVal(0.0f, 200));
}

void test_percentFrom_basic(void) {
    TEST_ASSERT_EQUAL_INT(50, percentFrom(50, 100));
}

void test_percentFrom_zero_max(void) {
    TEST_ASSERT_EQUAL_INT(0, percentFrom(50, 0));
}

/* ── getAverageFrom / getMinimumFrom / getHalfwayBetweenMinMax ─────────── */

void test_getAverageFrom_basic(void) {
    int t[] = {2, 4, 6};
    TEST_ASSERT_EQUAL_INT(4, getAverageFrom(t, 3));
}

void test_getAverageFrom_size_zero(void) {
    int t[] = {5};
    TEST_ASSERT_EQUAL_INT(0, getAverageFrom(t, 0));
}

void test_getMinimumFrom_basic(void) {
    int t[] = {5, 3, 8, 1, 7};
    TEST_ASSERT_EQUAL_INT(1, getMinimumFrom(t, 5));
}

void test_getMinimumFrom_size_zero(void) {
    int t[] = {5};
    TEST_ASSERT_EQUAL_INT(-1, getMinimumFrom(t, 0));
}

void test_getHalfwayBetweenMinMax_basic(void) {
    int a[] = {2, 8, 5};
    TEST_ASSERT_EQUAL_INT(5, getHalfwayBetweenMinMax(a, 3)); /* (2+8)/2 */
}

void test_getHalfwayBetweenMinMax_n_zero(void) {
    int a[] = {5};
    TEST_ASSERT_EQUAL_INT(-1, getHalfwayBetweenMinMax(a, 0));
}

/* ── getAverageForTable ────────────────────────────────────────────────── */

void test_getAverageForTable_accumulates(void) {
    float table[5] = {};
    int idx = 0, overall = 0;
    getAverageForTable(&idx, &overall, 4.0f, table);
    getAverageForTable(&idx, &overall, 6.0f, table);
    float avg = getAverageForTable(&idx, &overall, 8.0f, table);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, avg); /* (4+6+8)/3 */
}

void test_getAverageForTable_wraps_index(void) {
    float table[5] = {};
    int idx = 0, overall = 0;
    for (int i = 0; i < 5; i++)
        getAverageForTable(&idx, &overall, 10.0f, table);
    /* idx wrapped to 0; overwrite slot 0 with 0 */
    float avg = getAverageForTable(&idx, &overall, 0.0f, table);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, avg); /* (0+10+10+10+10)/5 */
}

/* ── isDaylightSavingTime ──────────────────────────────────────────────── */

void test_dst_january(void) {
    TEST_ASSERT_FALSE(isDaylightSavingTime(2024, 1, 15));
}

void test_dst_july(void) {
    TEST_ASSERT_TRUE(isDaylightSavingTime(2024, 7, 15));
}

void test_dst_november(void) {
    TEST_ASSERT_FALSE(isDaylightSavingTime(2024, 11, 15));
}

void test_dst_march_2024_before_last_sunday(void) {
    /* Last Sunday in March 2024 = March 31 */
    TEST_ASSERT_FALSE(isDaylightSavingTime(2024, 3, 30));
}

void test_dst_march_2024_on_last_sunday(void) {
    TEST_ASSERT_TRUE(isDaylightSavingTime(2024, 3, 31));
}

void test_dst_october_2024_before_last_sunday(void) {
    /* Last Sunday in October 2024 = October 27 */
    TEST_ASSERT_TRUE(isDaylightSavingTime(2024, 10, 26));
}

void test_dst_october_2024_on_last_sunday(void) {
    TEST_ASSERT_FALSE(isDaylightSavingTime(2024, 10, 27));
}

/* ── adjustTime ────────────────────────────────────────────────────────── */

void test_adjustTime_dst_plus2(void) {
    int y=2024, mo=7, d=15, h=10, mi=0;
    adjustTime(&y, &mo, &d, &h, &mi);
    TEST_ASSERT_EQUAL_INT(12, h);  /* CEST = UTC+2 */
}

void test_adjustTime_no_dst_plus1(void) {
    int y=2024, mo=1, d=15, h=10, mi=0;
    adjustTime(&y, &mo, &d, &h, &mi);
    TEST_ASSERT_EQUAL_INT(11, h);  /* CET = UTC+1 */
}

void test_adjustTime_midnight_overflow(void) {
    int y=2024, mo=7, d=15, h=23, mi=0;
    adjustTime(&y, &mo, &d, &h, &mi);
    TEST_ASSERT_EQUAL_INT(16, d);
    TEST_ASSERT_EQUAL_INT(1, h);
}

void test_adjustTime_null_safe(void) {
    adjustTime(NULL, NULL, NULL, NULL, NULL); /* must not crash */
}

/* ── MSB / LSB / MsbLsbToInt ───────────────────────────────────────────── */

void test_MSB(void) {
    TEST_ASSERT_EQUAL_INT(0xAB, MSB(0xABCD));
}

void test_LSB(void) {
    TEST_ASSERT_EQUAL_INT(0xCD, LSB(0xABCD));
}

void test_MsbLsbToInt(void) {
    TEST_ASSERT_EQUAL_INT(0xABCD, MsbLsbToInt(0xAB, 0xCD));
}

/* ── byteArrayToWord / wordToByteArray ─────────────────────────────────── */

void test_byteArrayToWord(void) {
    unsigned char b[] = {0x12, 0x34};
    TEST_ASSERT_EQUAL_INT(0x1234, byteArrayToWord(b));
}

void test_wordToByteArray(void) {
    unsigned char b[2];
    wordToByteArray(0x5678, b);
    TEST_ASSERT_EQUAL_INT(0x56, b[0]);
    TEST_ASSERT_EQUAL_INT(0x78, b[1]);
}

void test_word_roundtrip(void) {
    unsigned char b[2];
    wordToByteArray(0x1234, b);
    TEST_ASSERT_EQUAL_INT(0x1234, byteArrayToWord(b));
}

/* ── rroundf / roundfWithPrecisionTo ───────────────────────────────────── */

void test_rroundf_rounds_down(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rroundf(1.04f));
}

void test_rroundf_rounds_up(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, rroundf(1.45f));
}

void test_roundfWithPrecisionTo_2dp(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.24f, roundfWithPrecisionTo(1.235f, 2));
}

void test_roundfWithPrecisionTo_0dp(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, roundfWithPrecisionTo(1.5f, 0));
}

/* ── isValidString ─────────────────────────────────────────────────────── */

void test_isValidString_valid(void) {
    TEST_ASSERT_TRUE(isValidString("hello", 10));
}

void test_isValidString_null(void) {
    TEST_ASSERT_FALSE(isValidString(NULL, 10));
}

void test_isValidString_empty(void) {
    TEST_ASSERT_FALSE(isValidString("", 10));
}

void test_isValidString_zero_size(void) {
    TEST_ASSERT_FALSE(isValidString("hello", 0));
}

void test_isValidString_non_printable(void) {
    TEST_ASSERT_FALSE(isValidString("\x01hello", 10));
}

void test_isValidString_with_punctuation(void) {
    TEST_ASSERT_TRUE(isValidString("hello, world!", 20));
}

/* ── rgbToRgb565 ───────────────────────────────────────────────────────── */

void test_rgbToRgb565_red(void) {
    TEST_ASSERT_EQUAL_INT(0xF800, rgbToRgb565(255, 0, 0));
}

void test_rgbToRgb565_green(void) {
    TEST_ASSERT_EQUAL_INT(0x07E0, rgbToRgb565(0, 255, 0));
}

void test_rgbToRgb565_blue(void) {
    TEST_ASSERT_EQUAL_INT(0x001F, rgbToRgb565(0, 0, 255));
}

void test_rgbToRgb565_black(void) {
    TEST_ASSERT_EQUAL_INT(0x0000, rgbToRgb565(0, 0, 0));
}

/* ── macToString ───────────────────────────────────────────────────────── */

void test_macToString(void) {
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC};
    char buf[20];
    macToString(mac, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("11:22:33:AA:BB:CC", buf);
}

/* ── hexToChar ─────────────────────────────────────────────────────────── */

void test_hexToChar_letter_A(void) {
    TEST_ASSERT_EQUAL_INT(0x41, (unsigned char)hexToChar('4', '1'));
}

void test_hexToChar_space(void) {
    TEST_ASSERT_EQUAL_INT(0x20, (unsigned char)hexToChar('2', '0'));
}

void test_hexToChar_max(void) {
    TEST_ASSERT_EQUAL_INT(0xFF, (unsigned char)hexToChar('F', 'F'));
}

/* ── urlDecode ─────────────────────────────────────────────────────────── */

void test_urlDecode_percent_encoding(void) {
    char buf[16];
    urlDecode("%41%42%43", buf);
    TEST_ASSERT_EQUAL_STRING("ABC", buf);
}

void test_urlDecode_plus_to_space(void) {
    char buf[16];
    urlDecode("hello+world", buf);
    TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

void test_urlDecode_percent_space(void) {
    char buf[16];
    urlDecode("hello%20world", buf);
    TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

/* ── removeSpaces ──────────────────────────────────────────────────────── */

void test_removeSpaces_middle(void) {
    char s[] = "hello world";
    removeSpaces(s);
    TEST_ASSERT_EQUAL_STRING("helloworld", s);
}

void test_removeSpaces_leading_trailing(void) {
    char s[] = "  abc  ";
    removeSpaces(s);
    TEST_ASSERT_EQUAL_STRING("abc", s);
}

void test_removeSpaces_no_spaces(void) {
    char s[] = "abc";
    removeSpaces(s);
    TEST_ASSERT_EQUAL_STRING("abc", s);
}

/* ── startsWith ────────────────────────────────────────────────────────── */

void test_startsWith_true(void) {
    TEST_ASSERT_TRUE(startsWith("hello world", "hello"));
}

void test_startsWith_false(void) {
    TEST_ASSERT_FALSE(startsWith("hello world", "world"));
}

void test_startsWith_exact(void) {
    TEST_ASSERT_TRUE(startsWith("abc", "abc"));
}

/* ── parseNumber ───────────────────────────────────────────────────────── */

void test_parseNumber_basic(void) {
    const char *s = "123abc";
    int n = parseNumber(&s);
    TEST_ASSERT_EQUAL_INT(123, n);
    TEST_ASSERT_EQUAL_CHAR('a', *s);
}

void test_parseNumber_no_digits(void) {
    const char *s = "abc";
    int n = parseNumber(&s);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_CHAR('a', *s);
}

void test_parseNumber_zero(void) {
    const char *s = "0end";
    int n = parseNumber(&s);
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_CHAR('e', *s);
}

/* ── is_time_in_range ──────────────────────────────────────────────────── */

void test_is_time_in_range_inside(void) {
    TEST_ASSERT_TRUE(is_time_in_range(5, 0, 10));
}

void test_is_time_in_range_at_start(void) {
    TEST_ASSERT_TRUE(is_time_in_range(0, 0, 10));
}

void test_is_time_in_range_at_end_exclusive(void) {
    TEST_ASSERT_FALSE(is_time_in_range(10, 0, 10));
}

void test_is_time_in_range_below(void) {
    TEST_ASSERT_FALSE(is_time_in_range(-1, 0, 10));
}

/* ── extract_time ──────────────────────────────────────────────────────── */

void test_extract_time_basic(void) {
    int h, m;
    extract_time(125, &h, &m);
    TEST_ASSERT_EQUAL_INT(2, h);
    TEST_ASSERT_EQUAL_INT(5, m);
}

void test_extract_time_zero(void) {
    int h, m;
    extract_time(0, &h, &m);
    TEST_ASSERT_EQUAL_INT(0, h);
    TEST_ASSERT_EQUAL_INT(0, m);
}

void test_extract_time_exact_hour(void) {
    int h, m;
    extract_time(60, &h, &m);
    TEST_ASSERT_EQUAL_INT(1, h);
    TEST_ASSERT_EQUAL_INT(0, m);
}

/* ── mapfloat ──────────────────────────────────────────────────────────── */

void test_mapfloat_midpoint(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, mapfloat(5.0f, 0.0f, 10.0f, 0.0f, 100.0f));
}

void test_mapfloat_min(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mapfloat(0.0f, 0.0f, 10.0f, 0.0f, 100.0f));
}

void test_mapfloat_max(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, mapfloat(10.0f, 0.0f, 10.0f, 0.0f, 100.0f));
}

void test_mapfloat_equal_in_range(void) {
    /* in_max == in_min → returns out_min */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mapfloat(5.0f, 5.0f, 5.0f, 0.0f, 100.0f));
}

/* ── remove_non_ascii ──────────────────────────────────────────────────── */

void test_remove_non_ascii_plain(void) {
    char buf[32];
    remove_non_ascii("hello", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("hello", buf);
}

void test_remove_non_ascii_polish_l(void) {
    /* Unicode l-stroke: 0xC5 0x82 -> 'l' */
    const char in[] = {(char)0xC5, (char)0x82, '\0'};
    char buf[8];
    remove_non_ascii(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("l", buf);
}

void test_remove_non_ascii_mixed_word(void) {
    /* "Lodz" with Polish diacritics encoded in UTF-8 should normalize to "Lodz" */
    const char in[] = {(char)0xC5, (char)0x81,
                       (char)0xC3, (char)0xB3,
                       'd',
                       (char)0xC5, (char)0xBA,
                       '\0'};
    char buf[16];
    remove_non_ascii(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Lodz", buf);
}

void test_remove_non_ascii_strips_unknown(void) {
    /* 0x80 is not a supported UTF-8 prefix -> stripped */
    const char in[] = {(char)0x80, 'a', '\0'};
    char buf[8];
    remove_non_ascii(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("a", buf);
}

void test_remove_non_ascii_null_input(void) {
    char buf[8] = "x";
    remove_non_ascii(NULL, buf, sizeof(buf)); /* must not crash */
}

/* ── printBinaryAndSize ────────────────────────────────────────────────── */

void test_printBinaryAndSize_zero(void) {
    char buf[64];
    printBinaryAndSize(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00000000", buf);
}

void test_printBinaryAndSize_one(void) {
    char buf[64];
    printBinaryAndSize(1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00000001", buf);
}

void test_printBinaryAndSize_0xFF(void) {
    char buf[64];
    printBinaryAndSize(0xFF, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("11111111", buf);
}

void test_printBinaryAndSize_16bit(void) {
    char buf[64];
    printBinaryAndSize(0x100, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0000000100000000", buf);
}

void test_printBinaryAndSize_null_buf(void) {
    char *r = printBinaryAndSize(42, NULL, 0);
    TEST_ASSERT_NULL(r);
}

/* ── HAL-dependent: getAverageValueFrom, getSeconds ───────────────────── */

void test_getAverageValueFrom(void) {
    hal_mock_adc_inject(0, 513);
    float avg = getAverageValueFrom(0);
    /* adcCompe(513) = 521; all NUMSAMPLES samples identical → avg = 521 */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 521.0f, avg);
}

void test_getSeconds(void) {
    hal_mock_set_millis(5500);
    TEST_ASSERT_EQUAL_UINT32(6, getSeconds()); /* (5500 + 500) / 1000 */
}

/* ── adcToVolt ─────────────────────────────────────────────────────────── */

void test_adcToVolt_zero_adc(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, adcToVolt(0, 0.0f, 1.0f));
}

void test_adcToVolt_full_scale_no_divider(void) {
    /* ADC = 4096, r1=0 (no high-side resistor), r2=1 → scale = 1.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.3f, adcToVolt(4096, 0.0f, 1.0f));
}

void test_adcToVolt_half_scale_no_divider(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.65f, adcToVolt(2048, 0.0f, 1.0f));
}

void test_adcToVolt_equal_divider(void) {
    /* r1=r2 → scale = 2.0; full-scale ADC → 2 * 3.3 V */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.6f, adcToVolt(4096, 10000.0f, 10000.0f));
}

/* ── steinhart ─────────────────────────────────────────────────────────── */

void test_steinhart_nominal_temp(void) {
    /* When NTC resistance == thermistor nominal (Ro) the output
     * must equal TEMPERATURENOMINAL (21 °C by default).
     * Pass val=1.0 so that (r / val) = r = thermistor nominal. */
    float result = steinhart(1.0f, 10000, 10000, true);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, (float)HAL_TOOLS_TEMPERATURENOMINAL, result);
}

void test_steinhart_warm_temp(void) {
    /* NTC ≈ 3333 Ω → ~50 °C with B=3600, To=21 °C, Ro=10000. */
    float result = steinhart(1.0f, 10000, 3333, true);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, result);
}

/* ── hal_pack_field / hal_pack_field_pad ───────────────────────────────── */

void test_hal_pack_field_exact_fit(void) {
    uint8_t buf[4];
    hal_pack_field(buf, "ABC", 3);
    TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('C', buf[2]);
}

void test_hal_pack_field_shorter_pads_zero(void) {
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    hal_pack_field(buf, "AB", 4);
    TEST_ASSERT_EQUAL_UINT8('A',  buf[0]);
    TEST_ASSERT_EQUAL_UINT8('B',  buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);
}

void test_hal_pack_field_pad_custom_byte(void) {
    uint8_t buf[4] = {0};
    hal_pack_field_pad(buf, "A", 4, 0x20);
    TEST_ASSERT_EQUAL_UINT8('A',  buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x20, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x20, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x20, buf[3]);
}

void test_hal_pack_field_truncates_long_string(void) {
    uint8_t buf[3] = {0};
    hal_pack_field(buf, "ABCDEF", 3);
    TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('C', buf[2]);
}

void test_hal_pack_field_null_inputs_no_crash(void) {
    uint8_t buf[4] = {0};
    hal_pack_field(NULL, "AB", 4);   /* must not crash */
    hal_pack_field(buf,  NULL, 4);   /* must not crash */
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_tools_config_macro_aliases);
    RUN_TEST(test_floatToDec_positive);
    RUN_TEST(test_floatToDec_zero);
    RUN_TEST(test_floatToDec_null_pointers);
    RUN_TEST(test_decToFloat_basic);
    RUN_TEST(test_decToFloat_roundtrip);

    RUN_TEST(test_filter_alpha_one);
    RUN_TEST(test_filter_alpha_zero);
    RUN_TEST(test_filter_mid_alpha);
    RUN_TEST(test_filterValue_alpha_one);
    RUN_TEST(test_filterValue_mid_alpha);

    RUN_TEST(test_adcCompe_zero);
    RUN_TEST(test_adcCompe_boundary_512);
    RUN_TEST(test_adcCompe_510);
    RUN_TEST(test_adcCompe_511);
    RUN_TEST(test_adcCompe_above_512);
    RUN_TEST(test_adcCompe_above_1536);
    RUN_TEST(test_adcCompe_above_3584);

    RUN_TEST(test_percentToGivenVal_50);
    RUN_TEST(test_percentToGivenVal_100);
    RUN_TEST(test_percentToGivenVal_zero);
    RUN_TEST(test_percentFrom_basic);
    RUN_TEST(test_percentFrom_zero_max);

    RUN_TEST(test_getAverageFrom_basic);
    RUN_TEST(test_getAverageFrom_size_zero);
    RUN_TEST(test_getMinimumFrom_basic);
    RUN_TEST(test_getMinimumFrom_size_zero);
    RUN_TEST(test_getHalfwayBetweenMinMax_basic);
    RUN_TEST(test_getHalfwayBetweenMinMax_n_zero);
    RUN_TEST(test_getAverageForTable_accumulates);
    RUN_TEST(test_getAverageForTable_wraps_index);

    RUN_TEST(test_dst_january);
    RUN_TEST(test_dst_july);
    RUN_TEST(test_dst_november);
    RUN_TEST(test_dst_march_2024_before_last_sunday);
    RUN_TEST(test_dst_march_2024_on_last_sunday);
    RUN_TEST(test_dst_october_2024_before_last_sunday);
    RUN_TEST(test_dst_october_2024_on_last_sunday);
    RUN_TEST(test_adjustTime_dst_plus2);
    RUN_TEST(test_adjustTime_no_dst_plus1);
    RUN_TEST(test_adjustTime_midnight_overflow);
    RUN_TEST(test_adjustTime_null_safe);

    RUN_TEST(test_MSB);
    RUN_TEST(test_LSB);
    RUN_TEST(test_MsbLsbToInt);
    RUN_TEST(test_byteArrayToWord);
    RUN_TEST(test_wordToByteArray);
    RUN_TEST(test_word_roundtrip);

    RUN_TEST(test_rroundf_rounds_down);
    RUN_TEST(test_rroundf_rounds_up);
    RUN_TEST(test_roundfWithPrecisionTo_2dp);
    RUN_TEST(test_roundfWithPrecisionTo_0dp);

    RUN_TEST(test_isValidString_valid);
    RUN_TEST(test_isValidString_null);
    RUN_TEST(test_isValidString_empty);
    RUN_TEST(test_isValidString_zero_size);
    RUN_TEST(test_isValidString_non_printable);
    RUN_TEST(test_isValidString_with_punctuation);

    RUN_TEST(test_rgbToRgb565_red);
    RUN_TEST(test_rgbToRgb565_green);
    RUN_TEST(test_rgbToRgb565_blue);
    RUN_TEST(test_rgbToRgb565_black);

    RUN_TEST(test_macToString);

    RUN_TEST(test_hexToChar_letter_A);
    RUN_TEST(test_hexToChar_space);
    RUN_TEST(test_hexToChar_max);

    RUN_TEST(test_urlDecode_percent_encoding);
    RUN_TEST(test_urlDecode_plus_to_space);
    RUN_TEST(test_urlDecode_percent_space);

    RUN_TEST(test_removeSpaces_middle);
    RUN_TEST(test_removeSpaces_leading_trailing);
    RUN_TEST(test_removeSpaces_no_spaces);

    RUN_TEST(test_startsWith_true);
    RUN_TEST(test_startsWith_false);
    RUN_TEST(test_startsWith_exact);

    RUN_TEST(test_parseNumber_basic);
    RUN_TEST(test_parseNumber_no_digits);
    RUN_TEST(test_parseNumber_zero);

    RUN_TEST(test_is_time_in_range_inside);
    RUN_TEST(test_is_time_in_range_at_start);
    RUN_TEST(test_is_time_in_range_at_end_exclusive);
    RUN_TEST(test_is_time_in_range_below);

    RUN_TEST(test_extract_time_basic);
    RUN_TEST(test_extract_time_zero);
    RUN_TEST(test_extract_time_exact_hour);

    RUN_TEST(test_mapfloat_midpoint);
    RUN_TEST(test_mapfloat_min);
    RUN_TEST(test_mapfloat_max);
    RUN_TEST(test_mapfloat_equal_in_range);

    RUN_TEST(test_remove_non_ascii_plain);
    RUN_TEST(test_remove_non_ascii_polish_l);
    RUN_TEST(test_remove_non_ascii_mixed_word);
    RUN_TEST(test_remove_non_ascii_strips_unknown);
    RUN_TEST(test_remove_non_ascii_null_input);

    RUN_TEST(test_printBinaryAndSize_zero);
    RUN_TEST(test_printBinaryAndSize_one);
    RUN_TEST(test_printBinaryAndSize_0xFF);
    RUN_TEST(test_printBinaryAndSize_16bit);
    RUN_TEST(test_printBinaryAndSize_null_buf);

    RUN_TEST(test_getAverageValueFrom);
    RUN_TEST(test_getSeconds);

    RUN_TEST(test_adcToVolt_zero_adc);
    RUN_TEST(test_adcToVolt_full_scale_no_divider);
    RUN_TEST(test_adcToVolt_half_scale_no_divider);
    RUN_TEST(test_adcToVolt_equal_divider);

    RUN_TEST(test_steinhart_nominal_temp);
    RUN_TEST(test_steinhart_warm_temp);

    RUN_TEST(test_hal_pack_field_exact_fit);
    RUN_TEST(test_hal_pack_field_shorter_pads_zero);
    RUN_TEST(test_hal_pack_field_pad_custom_byte);
    RUN_TEST(test_hal_pack_field_truncates_long_string);
    RUN_TEST(test_hal_pack_field_null_inputs_no_crash);

    return UNITY_END();
}
