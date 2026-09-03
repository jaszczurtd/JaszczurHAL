#include "hal/impl/.mock/hal_mock.h"
#include "utils/tools.h"
#include "utils/unity.h"

#include <limits.h>

void setUp(void) {
  hal_mock_set_millis(0);
  hal_mock_set_micros(0);
  hal_mock_adc_inject(0, 0);
  hal_mock_wifi_reset();
}

void tearDown(void) {}

void test_tools_config_macro_aliases(void) {
  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_ADC_BITS, ADC_BITS);
  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_NUMSAMPLES, NUMSAMPLES);
  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_TEMPERATURE_TABLES_SIZE,
                        TEMPERATURE_TABLES_SIZE);
  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_BCOEFFICIENT, BCOEFFICIENT);
  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_TEMPERATURENOMINAL, TEMPERATURENOMINAL);

  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_PRINTABLE_BUFFER_SIZE, PRINTABLE_BUFFER_SIZE);
  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_PRINTABLE_PREFIX_SIZE, PRINTABLE_PREFIX_SIZE);

  static const char progmem_value[] PROGMEM = "pmem";
  TEST_ASSERT_EQUAL_STRING("pmem", progmem_value);
  TEST_ASSERT_EQUAL_STRING("flash", F("flash"));

  TEST_ASSERT_EQUAL_INT(HAL_TOOLS_EEPROM_FIRST_ADDR, EEPROM_FIRST_ADDR);
#ifdef HAL_ENABLE_SDLOGGER
  TEST_ASSERT_EQUAL_INT(HAL_SDLOGGER_EEPROM_FIRST_ADDR, EEPROM_FIRST_ADDR);
#else
  TEST_ASSERT_EQUAL_INT(0, EEPROM_FIRST_ADDR);
#endif
}

/* ── hal_math_split_decimal_tenths / hal_math_join_decimal_tenths
 * ───────────────────────────────────────────── */

void test_hal_math_split_decimal_tenths_positive(void) {
  int hi, lo;
  hal_math_split_decimal_tenths(3.7f, &hi, &lo);
  TEST_ASSERT_EQUAL_INT(3, hi);
  TEST_ASSERT_EQUAL_INT(7, lo);
}

void test_hal_math_split_decimal_tenths_zero(void) {
  int hi, lo;
  hal_math_split_decimal_tenths(0.0f, &hi, &lo);
  TEST_ASSERT_EQUAL_INT(0, hi);
  TEST_ASSERT_EQUAL_INT(0, lo);
}

void test_hal_math_split_decimal_tenths_null_pointers(void) {
  hal_math_split_decimal_tenths(3.7f, NULL, NULL); /* must not crash */
}

void test_hal_math_join_decimal_tenths_basic(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, hal_math_join_decimal_tenths(1, 2));
}

void test_hal_math_join_decimal_tenths_roundtrip(void) {
  int hi, lo;
  hal_math_split_decimal_tenths(2.5f, &hi, &lo);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, hal_math_join_decimal_tenths(hi, lo));
}

/* ── filter / hal_math_blend ────────────────────────────────────────────────
 */

void test_hal_math_low_pass_alpha_one(void) {
  /* alpha=1 -> output = input */
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, hal_math_low_pass(1.0f, 5.0f, 100.0f));
}

void test_hal_math_low_pass_alpha_zero(void) {
  /* alpha=0 -> output = previous */
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f,
                           hal_math_low_pass(0.0f, 5.0f, 100.0f));
}

void test_hal_math_low_pass_mid_alpha(void) {
  /* 0.5*3 + 0.5*7 = 5 */
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, hal_math_low_pass(0.5f, 3.0f, 7.0f));
}

void test_hal_math_blend_alpha_one(void) {
  /* alpha=1 -> output = newValue */
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.0f, hal_math_blend(3.0f, 9.0f, 1.0f));
}

void test_hal_math_blend_mid_alpha(void) {
  /* 0.5*10 + 0.5*20 = 15 */
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.0f, hal_math_blend(20.0f, 10.0f, 0.5f));
}

/* ── hal_adc_compensate_rp2040_12bit
 * ──────────────────────────────────────────────────────────── */

void test_hal_adc_compensate_rp2040_12bit_zero(void) {
  TEST_ASSERT_EQUAL_INT(0, hal_adc_compensate_rp2040_12bit(0));
}

void test_hal_adc_compensate_rp2040_12bit_boundary_512(void) {
  /* 512 is not > 512, not 511, not 510 -> falls through to else */
  TEST_ASSERT_EQUAL_INT(512, hal_adc_compensate_rp2040_12bit(512));
}

void test_hal_adc_compensate_rp2040_12bit_510(void) {
  TEST_ASSERT_EQUAL_INT(513,
                        hal_adc_compensate_rp2040_12bit(510)); /* 510 + 3 */
}

void test_hal_adc_compensate_rp2040_12bit_511(void) {
  TEST_ASSERT_EQUAL_INT(516,
                        hal_adc_compensate_rp2040_12bit(511)); /* 511 + 5 */
}

void test_hal_adc_compensate_rp2040_12bit_above_512(void) {
  TEST_ASSERT_EQUAL_INT(521,
                        hal_adc_compensate_rp2040_12bit(513)); /* 513 + 8 */
}

void test_hal_adc_compensate_rp2040_12bit_above_1536(void) {
  TEST_ASSERT_EQUAL_INT(1553,
                        hal_adc_compensate_rp2040_12bit(1537)); /* 1537 + 16 */
}

void test_hal_adc_compensate_rp2040_12bit_above_3584(void) {
  TEST_ASSERT_EQUAL_INT(3617,
                        hal_adc_compensate_rp2040_12bit(3585)); /* 3585 + 32 */
}

/* ── hal_math_percent_to_value / hal_math_percent_from_value
 * ───────────────────────────────────── */

void test_hal_math_percent_to_value_50(void) {
  TEST_ASSERT_EQUAL_INT(50, hal_math_percent_to_value(50.0f, 100));
}

void test_hal_math_percent_to_value_100(void) {
  TEST_ASSERT_EQUAL_INT(100, hal_math_percent_to_value(100.0f, 100));
}

void test_hal_math_percent_to_value_zero(void) {
  TEST_ASSERT_EQUAL_INT(0, hal_math_percent_to_value(0.0f, 200));
}

void test_hal_math_percent_from_value_basic(void) {
  TEST_ASSERT_EQUAL_INT(50, hal_math_percent_from_value(50, 100));
}

void test_hal_math_percent_from_value_zero_max(void) {
  TEST_ASSERT_EQUAL_INT(0, hal_math_percent_from_value(50, 0));
}

/* ── hal_math_nonnegative_average_i32 / hal_math_min_i32 /
 * hal_math_midpoint_min_max_i32 ─────────── */

void test_hal_math_nonnegative_average_i32_basic(void) {
  int t[] = {2, 4, 6};
  TEST_ASSERT_EQUAL_INT(4, hal_math_nonnegative_average_i32(t, 3));
}

void test_hal_math_nonnegative_average_i32_size_zero(void) {
  int t[] = {5};
  TEST_ASSERT_EQUAL_INT(0, hal_math_nonnegative_average_i32(t, 0));
}

void test_hal_math_min_i32_basic(void) {
  int t[] = {5, 3, 8, 1, 7};
  TEST_ASSERT_EQUAL_INT(1, hal_math_min_i32(t, 5));
}

void test_hal_math_min_i32_size_zero(void) {
  int t[] = {5};
  TEST_ASSERT_EQUAL_INT(-1, hal_math_min_i32(t, 0));
}

void test_hal_math_midpoint_min_max_i32_basic(void) {
  int a[] = {2, 8, 5};
  TEST_ASSERT_EQUAL_INT(5, hal_math_midpoint_min_max_i32(a, 3)); /* (2+8)/2 */
}

void test_hal_math_midpoint_min_max_i32_n_zero(void) {
  int a[] = {5};
  TEST_ASSERT_EQUAL_INT(-1, hal_math_midpoint_min_max_i32(a, 0));
}

/* ── hal_math_rolling_average_default_f32
 * ────────────────────────────────────────────────── */

void test_hal_math_rolling_average_default_f32_accumulates(void) {
  float table[5] = {};
  int idx = 0, overall = 0;
  hal_math_rolling_average_default_f32(&idx, &overall, 4.0f, table);
  hal_math_rolling_average_default_f32(&idx, &overall, 6.0f, table);
  float avg = hal_math_rolling_average_default_f32(&idx, &overall, 8.0f, table);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, avg); /* (4+6+8)/3 */
}

void test_hal_math_rolling_average_default_f32_wraps_index(void) {
  float table[5] = {};
  int idx = 0, overall = 0;
  for (int i = 0; i < 5; i++)
    hal_math_rolling_average_default_f32(&idx, &overall, 10.0f, table);
  /* idx wrapped to 0; overwrite slot 0 with 0 */
  float avg = hal_math_rolling_average_default_f32(&idx, &overall, 0.0f, table);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, avg); /* (0+10+10+10+10)/5 */
}

/* ── hal_time_is_daylight_saving_time
 * ──────────────────────────────────────────────── */

void test_dst_january(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 1, 15));
}

void test_dst_july(void) {
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 7, 15));
}

void test_dst_november(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 11, 15));
}

void test_dst_march_2024_before_last_sunday(void) {
  /* Last Sunday in March 2024 = March 31 */
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 3, 30));
}

void test_dst_march_2024_on_last_sunday(void) {
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 3, 31));
}

void test_dst_october_2024_before_last_sunday(void) {
  /* Last Sunday in October 2024 = October 27 */
  TEST_ASSERT_TRUE(hal_time_is_daylight_saving_time(2024, 10, 26));
}

void test_dst_october_2024_on_last_sunday(void) {
  TEST_ASSERT_FALSE(hal_time_is_daylight_saving_time(2024, 10, 27));
}

/* ── hal_time_adjust_cet_cest
 * ────────────────────────────────────────────────────────── */

void test_hal_time_adjust_cet_cest_dst_plus2(void) {
  int y = 2024, mo = 7, d = 15, h = 10, mi = 0;
  hal_time_adjust_cet_cest(&y, &mo, &d, &h, &mi);
  TEST_ASSERT_EQUAL_INT(12, h); /* CEST = UTC+2 */
}

void test_hal_time_adjust_cet_cest_no_dst_plus1(void) {
  int y = 2024, mo = 1, d = 15, h = 10, mi = 0;
  hal_time_adjust_cet_cest(&y, &mo, &d, &h, &mi);
  TEST_ASSERT_EQUAL_INT(11, h); /* CET = UTC+1 */
}

void test_hal_time_adjust_cet_cest_midnight_overflow(void) {
  int y = 2024, mo = 7, d = 15, h = 23, mi = 0;
  hal_time_adjust_cet_cest(&y, &mo, &d, &h, &mi);
  TEST_ASSERT_EQUAL_INT(16, d);
  TEST_ASSERT_EQUAL_INT(1, h);
}

void test_hal_time_adjust_cet_cest_null_safe(void) {
  hal_time_adjust_cet_cest(NULL, NULL, NULL, NULL, NULL); /* must not crash */
}

void test_hal_time_adjust_cet_cest_month_and_year_rollover(void) {
  int y = 2023, mo = 12, d = 31, h = 23, mi = 17;
  hal_time_adjust_cet_cest(&y, &mo, &d, &h, &mi);
  TEST_ASSERT_EQUAL_INT(2024, y);
  TEST_ASSERT_EQUAL_INT(1, mo);
  TEST_ASSERT_EQUAL_INT(1, d);
  TEST_ASSERT_EQUAL_INT(0, h);
  TEST_ASSERT_EQUAL_INT(17, mi);
}

/* ── MSB / LSB / jh_u16_from_bytes
 * ───────────────────────────────────────────── */

void test_MSB(void) { TEST_ASSERT_EQUAL_INT(0xAB, MSB(0xABCD)); }

void test_LSB(void) { TEST_ASSERT_EQUAL_INT(0xCD, LSB(0xABCD)); }

void test_MSB_and_LSB_evaluate_argument_once_and_keep_function_symbols(void) {
  uint16_t value = 0x1234u;
  TEST_ASSERT_EQUAL_HEX8(0x12u, MSB(value++));
  TEST_ASSERT_EQUAL_HEX16(0x1235u, value);
  TEST_ASSERT_EQUAL_HEX8(0x35u, LSB(value++));
  TEST_ASSERT_EQUAL_HEX16(0x1236u, value);

  uint8_t (*msb_function)(unsigned short) = &MSB;
  uint8_t (*lsb_function)(unsigned short) = &LSB;
  TEST_ASSERT_EQUAL_HEX8(0xABu, msb_function(0xABCDu));
  TEST_ASSERT_EQUAL_HEX8(0xCDu, lsb_function(0xABCDu));
}

void test_jh_u16_from_bytes(void) {
  TEST_ASSERT_EQUAL_INT(0xABCD, jh_u16_from_bytes(0xAB, 0xCD));
}

/* ── jh_load_be16 / jh_store_be16 ─────────────────────────────────── */

void test_jh_load_be16(void) {
  unsigned char b[] = {0x12, 0x34};
  TEST_ASSERT_EQUAL_INT(0x1234, jh_load_be16(b));
}

void test_jh_store_be16(void) {
  unsigned char b[2];
  jh_store_be16(b, 0x5678);
  TEST_ASSERT_EQUAL_INT(0x56, b[0]);
  TEST_ASSERT_EQUAL_INT(0x78, b[1]);
}

void test_word_roundtrip(void) {
  unsigned char b[2];
  jh_store_be16(b, 0x1234);
  TEST_ASSERT_EQUAL_INT(0x1234, jh_load_be16(b));
}

/* ── hal_math_round_tenth / hal_math_round_precision
 * ───────────────────────────────────── */

void test_hal_math_round_tenth_rounds_down(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, hal_math_round_tenth(1.04f));
}

void test_hal_math_round_tenth_rounds_up(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, hal_math_round_tenth(1.45f));
}

void test_hal_math_round_precision_2dp(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.24f, hal_math_round_precision(1.235f, 2));
}

void test_hal_math_round_precision_0dp(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, hal_math_round_precision(1.5f, 0));
}

/* ── hal_text_concat ─────────────────────────────────────────────────────── */

void test_hal_text_concat_basic(void) {
  char buf[16] = {0};
  TEST_ASSERT_TRUE(hal_text_concat(buf, sizeof(buf), "hello", " world"));
  TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

void test_hal_text_concat_empty_sources(void) {
  char buf[4] = {'X', 0, 0, 0};
  TEST_ASSERT_TRUE(hal_text_concat(buf, sizeof(buf), "", ""));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_hal_text_concat_exact_fit(void) {
  char buf[6] = {0}; /* "abcde" + '\0' */
  TEST_ASSERT_TRUE(hal_text_concat(buf, sizeof(buf), "abc", "de"));
  TEST_ASSERT_EQUAL_STRING("abcde", buf);
}

void test_hal_text_concat_too_small_buffer(void) {
  char buf[5] = "init";
  TEST_ASSERT_FALSE(hal_text_concat(buf, sizeof(buf), "abc", "de"));
  TEST_ASSERT_EQUAL_STRING("init", buf);
}

void test_hal_text_concat_zero_dest_size(void) {
  char buf[4] = "abc";
  TEST_ASSERT_FALSE(hal_text_concat(buf, 0, "a", "b"));
  TEST_ASSERT_EQUAL_STRING("abc", buf);
}

void test_hal_text_concat_null_args(void) {
  char buf[8] = {0};
  TEST_ASSERT_FALSE(hal_text_concat(NULL, sizeof(buf), "a", "b"));
  TEST_ASSERT_FALSE(hal_text_concat(buf, sizeof(buf), NULL, "b"));
  TEST_ASSERT_FALSE(hal_text_concat(buf, sizeof(buf), "a", NULL));
}

void test_hal_debug_set_module_prefix_logs_module_prefix(void) {
  hal_mock_serial_reset();

  hal_debug_init_default();
  hal_debug_set_module_prefix("ECU");
  deb("hello");

  TEST_ASSERT_EQUAL_STRING("ECU: hello", hal_mock_deb_last_line());
}

/* ── hal_text_is_printable
 * ─────────────────────────────────────────────────────── */

void test_hal_text_is_printable_valid(void) {
  TEST_ASSERT_TRUE(hal_text_is_printable("hello", 10));
}

void test_hal_text_is_printable_null(void) {
  TEST_ASSERT_FALSE(hal_text_is_printable(NULL, 10));
}

void test_hal_text_is_printable_empty(void) {
  TEST_ASSERT_FALSE(hal_text_is_printable("", 10));
}

void test_hal_text_is_printable_zero_size(void) {
  TEST_ASSERT_FALSE(hal_text_is_printable("hello", 0));
}

void test_hal_text_is_printable_non_printable(void) {
  TEST_ASSERT_FALSE(hal_text_is_printable("\x01hello", 10));
}

void test_hal_text_is_printable_with_punctuation(void) {
  TEST_ASSERT_TRUE(hal_text_is_printable("hello, world!", 20));
}

/* ── hal_pixel_rgb888_to_rgb565
 * ───────────────────────────────────────────────────────── */

void test_hal_pixel_rgb888_to_rgb565_red(void) {
  TEST_ASSERT_EQUAL_INT(0xF800, hal_pixel_rgb888_to_rgb565(255, 0, 0));
}

void test_hal_pixel_rgb888_to_rgb565_green(void) {
  TEST_ASSERT_EQUAL_INT(0x07E0, hal_pixel_rgb888_to_rgb565(0, 255, 0));
}

void test_hal_pixel_rgb888_to_rgb565_blue(void) {
  TEST_ASSERT_EQUAL_INT(0x001F, hal_pixel_rgb888_to_rgb565(0, 0, 255));
}

void test_hal_pixel_rgb888_to_rgb565_black(void) {
  TEST_ASSERT_EQUAL_INT(0x0000, hal_pixel_rgb888_to_rgb565(0, 0, 0));
}

void test_hal_pixel_rgb888_buffer_to_rgb565_buffer(void) {
  const unsigned char rgb[] = {
      255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255,
  };
  unsigned short out[4] = {0};

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_pixel_rgb888_buffer_to_rgb565_ex(rgb, out, 4));
  TEST_ASSERT_EQUAL_HEX16(0xF800, out[0]);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, out[1]);
  TEST_ASSERT_EQUAL_HEX16(0x001F, out[2]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, out[3]);
}

void test_hal_pixel_rgba8888_buffer_to_rgb565_buffer_ignores_alpha(void) {
  const unsigned char rgba[] = {
      255, 0, 0, 0, 0, 255, 0, 64, 0, 0, 255, 128, 255, 255, 255, 255,
  };
  unsigned short out[4] = {0};

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_pixel_rgba8888_buffer_to_rgb565_ex(rgba, out, 4));
  TEST_ASSERT_EQUAL_HEX16(0xF800, out[0]);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, out[1]);
  TEST_ASSERT_EQUAL_HEX16(0x001F, out[2]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, out[3]);
}

void test_rgb565_buffer_converters_reject_null(void) {
  const unsigned char rgb[] = {255, 0, 0};
  unsigned short out[1] = {0};

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pixel_rgb888_buffer_to_rgb565_ex(NULL, out, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pixel_rgb888_buffer_to_rgb565_ex(rgb, NULL, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pixel_rgba8888_buffer_to_rgb565_ex(NULL, out, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pixel_rgba8888_buffer_to_rgb565_ex(rgb, NULL, 1));
}

/* ── hal_network_format_mac
 * ───────────────────────────────────────────────────────── */

void test_hal_network_format_mac(void) {
  uint8_t mac[6] = {0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC};
  char buf[20];
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_network_format_mac_ex(mac, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("11:22:33:AA:BB:CC", buf);
}

void test_hal_wifi_scan_for_ssid_uses_hal_wifi_scan_results(void) {
  const uint8_t bssid0[HAL_WIFI_BSSID_LEN] = {0x00, 0x11, 0x22,
                                              0x33, 0x44, 0x55};
  const uint8_t bssid1[HAL_WIFI_BSSID_LEN] = {0x66, 0x77, 0x88,
                                              0x99, 0xAA, 0xBB};

  TEST_ASSERT_TRUE(hal_mock_wifi_set_scan_result(0, "shop", HAL_WIFI_ENC_NONE,
                                                 bssid0, 1, -80));
  TEST_ASSERT_TRUE(hal_mock_wifi_set_scan_result(
      1, "home-main", HAL_WIFI_ENC_WPA2, bssid1, 11, -42));

  TEST_ASSERT_TRUE(hal_wifi_scan_for_ssid("home"));
  TEST_ASSERT_FALSE(hal_wifi_scan_for_ssid("missing"));
}

/* ── hal_text_hex_pair_to_byte
 * ─────────────────────────────────────────────────────────── */

void test_hal_text_hex_pair_to_byte_letter_A(void) {
  TEST_ASSERT_EQUAL_INT(0x41,
                        (unsigned char)hal_text_hex_pair_to_byte('4', '1'));
}

void test_hal_text_hex_pair_to_byte_space(void) {
  TEST_ASSERT_EQUAL_INT(0x20,
                        (unsigned char)hal_text_hex_pair_to_byte('2', '0'));
}

void test_hal_text_hex_pair_to_byte_max(void) {
  TEST_ASSERT_EQUAL_INT(0xFF,
                        (unsigned char)hal_text_hex_pair_to_byte('F', 'F'));
}

/* ── hal_text_url_decode
 * ─────────────────────────────────────────────────────────── */

void test_hal_text_url_decode_percent_encoding(void) {
  char buf[16];
  hal_text_url_decode("%41%42%43", buf);
  TEST_ASSERT_EQUAL_STRING("ABC", buf);
}

void test_hal_text_url_decode_plus_to_space(void) {
  char buf[16];
  hal_text_url_decode("hello+world", buf);
  TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

void test_hal_text_url_decode_percent_space(void) {
  char buf[16];
  hal_text_url_decode("hello%20world", buf);
  TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

/* ── hal_text_remove_whitespace
 * ──────────────────────────────────────────────────────── */

void test_hal_text_remove_whitespace_middle(void) {
  char s[] = "hello world";
  hal_text_remove_whitespace(s);
  TEST_ASSERT_EQUAL_STRING("helloworld", s);
}

void test_hal_text_remove_whitespace_leading_trailing(void) {
  char s[] = "  abc  ";
  hal_text_remove_whitespace(s);
  TEST_ASSERT_EQUAL_STRING("abc", s);
}

void test_hal_text_remove_whitespace_no_spaces(void) {
  char s[] = "abc";
  hal_text_remove_whitespace(s);
  TEST_ASSERT_EQUAL_STRING("abc", s);
}

/* ── hal_text_starts_with
 * ────────────────────────────────────────────────────────── */

void test_hal_text_starts_with_true(void) {
  TEST_ASSERT_TRUE(hal_text_starts_with("hello world", "hello"));
}

void test_hal_text_starts_with_false(void) {
  TEST_ASSERT_FALSE(hal_text_starts_with("hello world", "world"));
}

void test_hal_text_starts_with_exact(void) {
  TEST_ASSERT_TRUE(hal_text_starts_with("abc", "abc"));
}

/* ── hal_text_parse_number
 * ───────────────────────────────────────────────────────── */

void test_hal_text_parse_number_basic(void) {
  const char *s = "123abc";
  int n = hal_text_parse_number(&s);
  TEST_ASSERT_EQUAL_INT(123, n);
  TEST_ASSERT_EQUAL_CHAR('a', *s);
}

void test_hal_text_parse_number_no_digits(void) {
  const char *s = "abc";
  int n = hal_text_parse_number(&s);
  TEST_ASSERT_EQUAL_INT(0, n);
  TEST_ASSERT_EQUAL_CHAR('a', *s);
}

void test_hal_text_parse_number_zero(void) {
  const char *s = "0end";
  int n = hal_text_parse_number(&s);
  TEST_ASSERT_EQUAL_INT(0, n);
  TEST_ASSERT_EQUAL_CHAR('e', *s);
}

/* ── hal_gps_nmea_hex_value / hal_gps_nmea_decimal_x100 / hal_gps_nmea_degrees
 * ─────────────────────────── */

void test_hal_gps_nmea_hex_value_digit(void) {
  TEST_ASSERT_EQUAL_INT(9, hal_gps_nmea_hex_value('9'));
}

void test_hal_gps_nmea_hex_value_upper_letter(void) {
  TEST_ASSERT_EQUAL_INT(10, hal_gps_nmea_hex_value('A'));
}

void test_hal_gps_nmea_hex_value_lower_letter(void) {
  TEST_ASSERT_EQUAL_INT(15, hal_gps_nmea_hex_value('f'));
}

void test_hal_gps_nmea_decimal_x100_integer(void) {
  TEST_ASSERT_EQUAL_INT(12300, hal_gps_nmea_decimal_x100("123"));
}

void test_hal_gps_nmea_decimal_x100_fraction_2dp(void) {
  TEST_ASSERT_EQUAL_INT(1234, hal_gps_nmea_decimal_x100("12.34"));
}

void test_hal_gps_nmea_decimal_x100_negative(void) {
  TEST_ASSERT_EQUAL_INT(-125, hal_gps_nmea_decimal_x100("-1.25"));
}

void test_hal_gps_nmea_degrees_gprmc_latitude_format(void) {
  int16_t deg = 0;
  uint32_t billionths = 0;
  hal_gps_nmea_degrees("4807.038", &deg, &billionths);

  TEST_ASSERT_EQUAL_INT(48, deg);
  TEST_ASSERT_EQUAL_UINT32(117300000UL, billionths);
}

/* ── hal_time_is_in_range ────────────────────────────────────────────────────
 */

void test_hal_time_is_in_range_inside(void) {
  TEST_ASSERT_TRUE(hal_time_is_in_range(5, 0, 10));
}

void test_hal_time_is_in_range_at_start(void) {
  TEST_ASSERT_TRUE(hal_time_is_in_range(0, 0, 10));
}

void test_hal_time_is_in_range_at_end_exclusive(void) {
  TEST_ASSERT_FALSE(hal_time_is_in_range(10, 0, 10));
}

void test_hal_time_is_in_range_below(void) {
  TEST_ASSERT_FALSE(hal_time_is_in_range(-1, 0, 10));
}

/* ── hal_time_extract_minutes
 * ──────────────────────────────────────────────────────── */

void test_hal_time_extract_minutes_basic(void) {
  int h, m;
  hal_time_extract_minutes(125, &h, &m);
  TEST_ASSERT_EQUAL_INT(2, h);
  TEST_ASSERT_EQUAL_INT(5, m);
}

void test_hal_time_extract_minutes_zero(void) {
  int h, m;
  hal_time_extract_minutes(0, &h, &m);
  TEST_ASSERT_EQUAL_INT(0, h);
  TEST_ASSERT_EQUAL_INT(0, m);
}

void test_hal_time_extract_minutes_exact_hour(void) {
  int h, m;
  hal_time_extract_minutes(60, &h, &m);
  TEST_ASSERT_EQUAL_INT(1, h);
  TEST_ASSERT_EQUAL_INT(0, m);
}

void test_hal_time_extract_minutes_null_safe(void) {
  int h = 99;
  hal_time_extract_minutes(60, &h, NULL);
  TEST_ASSERT_EQUAL_INT(1, h);
  hal_time_extract_minutes(60, NULL, NULL);
}

/* ── hal_math_map_f32
 * ──────────────────────────────────────────────────────────── */

void test_hal_math_map_f32_midpoint(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f,
                           hal_math_map_f32(5.0f, 0.0f, 10.0f, 0.0f, 100.0f));
}

void test_hal_math_map_f32_min(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f,
                           hal_math_map_f32(0.0f, 0.0f, 10.0f, 0.0f, 100.0f));
}

void test_hal_math_map_f32_max(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f,
                           hal_math_map_f32(10.0f, 0.0f, 10.0f, 0.0f, 100.0f));
}

void test_hal_math_map_f32_equal_in_range(void) {
  /* in_max == in_min -> returns out_min */
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f,
                           hal_math_map_f32(5.0f, 5.0f, 5.0f, 0.0f, 100.0f));
}

/* ── hal_text_transliterate_ascii
 * ──────────────────────────────────────────────────── */

void test_hal_text_transliterate_ascii_plain(void) {
  char buf[32];
  hal_text_transliterate_ascii("hello", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("hello", buf);
}

void test_hal_text_transliterate_ascii_polish_l(void) {
  /* Unicode l-stroke: 0xC5 0x82 -> 'l' */
  const char in[] = {(char)0xC5, (char)0x82, '\0'};
  char buf[8];
  hal_text_transliterate_ascii(in, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("l", buf);
}

void test_hal_text_transliterate_ascii_mixed_word(void) {
  /* "Lodz" with Polish diacritics encoded in UTF-8 should normalize to "Lodz"
   */
  const char in[] = {(char)0xC5, (char)0x81, (char)0xC3, (char)0xB3,
                     'd',        (char)0xC5, (char)0xBA, '\0'};
  char buf[16];
  hal_text_transliterate_ascii(in, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("Lodz", buf);
}

void test_hal_text_transliterate_ascii_strips_unknown(void) {
  /* 0x80 is not a supported UTF-8 prefix -> stripped */
  const char in[] = {(char)0x80, 'a', '\0'};
  char buf[8];
  hal_text_transliterate_ascii(in, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("a", buf);
}

void test_hal_text_transliterate_ascii_null_input(void) {
  char buf[8] = "x";
  hal_text_transliterate_ascii(NULL, buf, sizeof(buf)); /* must not crash */
}

/* ── hal_text_format_binary_int
 * ────────────────────────────────────────────────── */

void test_hal_text_format_binary_int_zero(void) {
  char buf[64];
  hal_text_format_binary_int(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("00000000", buf);
}

void test_hal_text_format_binary_int_one(void) {
  char buf[64];
  hal_text_format_binary_int(1, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("00000001", buf);
}

void test_hal_text_format_binary_int_0xFF(void) {
  char buf[64];
  hal_text_format_binary_int(0xFF, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("11111111", buf);
}

void test_hal_text_format_binary_int_16bit(void) {
  char buf[64];
  hal_text_format_binary_int(0x100, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0000000100000000", buf);
}

void test_hal_text_format_binary_int_null_buf(void) {
  char *r = hal_text_format_binary_int(42, NULL, 0);
  TEST_ASSERT_NULL(r);
}

/* ── HAL-dependent: hal_adc_read_average, hal_get_seconds ─────────────────────
 */

void test_hal_adc_read_average(void) {
  hal_mock_adc_inject(0, 513);
  float avg = hal_adc_read_average(0);
  /* hal_adc_compensate_rp2040_12bit(513) = 521; all NUMSAMPLES samples
   * identical -> avg = 521 */
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 521.0f, avg);
}

void test_hal_get_seconds(void) {
  hal_mock_set_millis(5500);
  TEST_ASSERT_EQUAL_UINT32(6, hal_get_seconds()); /* (5500 + 500) / 1000 */
}

/* ── hal_adc_raw_to_voltage
 * ─────────────────────────────────────────────────────────── */

void test_hal_adc_raw_to_voltage_zero_adc(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, hal_adc_raw_to_voltage(0, 0.0f, 1.0f));
}

void test_hal_adc_raw_to_voltage_full_scale_no_divider(void) {
  /* ADC = 4096, r1=0 (no high-side resistor), r2=1 -> scale = 1.0 */
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.3f,
                           hal_adc_raw_to_voltage(4096, 0.0f, 1.0f));
}

void test_hal_adc_raw_to_voltage_half_scale_no_divider(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.65f,
                           hal_adc_raw_to_voltage(2048, 0.0f, 1.0f));
}

void test_hal_adc_raw_to_voltage_equal_divider(void) {
  /* r1=r2 -> scale = 2.0; full-scale ADC -> 2 * 3.3 V */
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.6f,
                           hal_adc_raw_to_voltage(4096, 10000.0f, 10000.0f));
}

/* ── hal_ntc_steinhart
 * ─────────────────────────────────────────────────────────── */

void test_hal_ntc_steinhart_nominal_temp(void) {
  /* When NTC resistance == thermistor nominal (Ro) the output
   * must equal TEMPERATURENOMINAL (21 °C by default).
   * Pass val=1.0 so that (r / val) = r = thermistor nominal. */
  float result = hal_ntc_steinhart(1.0f, 10000, 10000, true);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, (float)HAL_TOOLS_TEMPERATURENOMINAL, result);
}

void test_hal_ntc_steinhart_warm_temp(void) {
  /* NTC ≈ 3333 Ω -> ~50 °C with B=3600, To=21 °C, Ro=10000. */
  float result = hal_ntc_steinhart(1.0f, 10000, 3333, true);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, result);
}

/* ── hal_text_pack_field / hal_text_pack_field_pad
 * ───────────────────────────────── */

void test_hal_text_pack_field_exact_fit(void) {
  uint8_t buf[4];
  hal_text_pack_field(buf, "ABC", 3);
  TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
  TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
  TEST_ASSERT_EQUAL_UINT8('C', buf[2]);
}

void test_hal_text_pack_field_shorter_pads_zero(void) {
  uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  hal_text_pack_field(buf, "AB", 4);
  TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
  TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);
}

void test_hal_text_pack_field_pad_custom_byte(void) {
  uint8_t buf[4] = {0};
  hal_text_pack_field_pad(buf, "A", 4, 0x20);
  TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0x20, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0x20, buf[2]);
  TEST_ASSERT_EQUAL_UINT8(0x20, buf[3]);
}

void test_hal_text_pack_field_truncates_long_string(void) {
  uint8_t buf[3] = {0};
  hal_text_pack_field(buf, "ABCDEF", 3);
  TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
  TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
  TEST_ASSERT_EQUAL_UINT8('C', buf[2]);
}

void test_hal_text_pack_field_null_inputs_no_crash(void) {
  uint8_t buf[4] = {0};
  hal_text_pack_field(NULL, "AB", 4); /* must not crash */
  hal_text_pack_field(buf, NULL, 4);  /* must not crash */
}

/* -- Thematic HAL APIs ---------------------------------------------------- */

void test_hal_math_average_handles_large_values(void) {
  const int values[] = {INT_MAX, INT_MAX};
  int average = 0;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_math_average_i32_ex(values, COUNTOF(values), &average));
  TEST_ASSERT_EQUAL_INT(INT_MAX, average);
}

void test_hal_math_midpoint_handles_full_integer_range(void) {
  const int values[] = {INT_MIN, INT_MAX};
  int midpoint = 123;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_math_midpoint_min_max_i32_ex(
                                    values, COUNTOF(values), &midpoint));
  TEST_ASSERT_EQUAL_INT(0, midpoint);
}

void test_hal_math_array_helpers_reject_empty_input(void) {
  int result = 7;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_math_average_i32_ex(NULL, 0u, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_math_min_i32_ex(NULL, 0u, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_math_midpoint_min_max_i32_ex(NULL, 0u, &result));
  TEST_ASSERT_EQUAL_INT(7, result);
}

void test_hal_text_url_decode_reports_overflow_and_terminates(void) {
  char output[4] = {'x', 'x', 'x', 'x'};
  size_t length = 99u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      hal_text_url_decode_ex("hello", output, sizeof(output), &length));
  TEST_ASSERT_EQUAL_STRING("hel", output);
  TEST_ASSERT_EQUAL_UINT32(3u, length);
}

void test_hal_text_hex_pair_rejects_invalid_input(void) {
  uint8_t value = 0xA5u;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_text_hex_pair_to_byte_ex('G', '0', &value));
  TEST_ASSERT_EQUAL_HEX8(0xA5u, value);
}

void test_hal_adc_voltage_rejects_invalid_divider(void) {
  float voltage = 123.0f;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_adc_raw_to_voltage_ex(100, 3.3f, 12u, 1000.0f, 0.0f, &voltage));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.0f, voltage);
}

void test_hal_adc_average_uses_explicit_transform(void) {
  hal_mock_adc_inject(0, 513);
  const hal_adc_average_config_t config = {
      0u, 3u, 0u, false, hal_adc_compensate_rp2040_12bit,
  };
  float average = 0.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_read_average_ex(&config, &average));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 521.0f, average);
}

void test_hal_ntc_rejects_adc_endpoints(void) {
  const hal_ntc_beta_config_t config = {10000.0f, 10000.0f, 3600.0f, 21.0f};
  float temperature = 99.0f;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_ntc_temperature_from_adc_ex(
                                        0.0f, 4095.0f, &config, &temperature));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      hal_ntc_temperature_from_adc_ex(4095.0f, 4095.0f, &config, &temperature));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 99.0f, temperature);
}

void test_hal_pixel_converters_report_invalid_arguments(void) {
  const uint8_t rgb[] = {255u, 0u, 0u};
  uint16_t output = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_pixel_rgb888_buffer_to_rgb565_ex(NULL, &output, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_pixel_rgb888_buffer_to_rgb565_ex(rgb, NULL, 1u));
}

void test_hal_periodic_random_holds_value_until_interval(void) {
  hal_periodic_random_int_t random;
  hal_periodic_random_int_init(&random, 1u);
  int initial = 0;
  int before_interval = 0;
  int at_interval = -1;

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_periodic_random_int_get_ex(&random, 0u, 10u, 100, &initial));
  TEST_ASSERT_EQUAL_INT(-1, initial);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_periodic_random_int_get_ex(
                                    &random, 9u, 10u, 100, &before_interval));
  TEST_ASSERT_EQUAL_INT(initial, before_interval);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_periodic_random_int_get_ex(
                                    &random, 10u, 10u, 100, &at_interval));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, at_interval);
  TEST_ASSERT_LESS_THAN_INT(100, at_interval);
}

void test_hal_periodic_random_elapsed_check_is_wrap_safe(void) {
  hal_periodic_random_int_t random;
  hal_periodic_random_int_init(&random, 1u);
  random.last_update_ms = UINT32_MAX - 4u;
  random.cached_value = 77;
  int value = 0;

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_periodic_random_int_get_ex(&random, 3u, 8u, 100, &value));
  TEST_ASSERT_NOT_EQUAL(77, value);
}

void test_hal_periodic_random_rejects_invalid_ranges(void) {
  hal_periodic_random_int_t integer_random;
  hal_periodic_random_float_t float_random;
  int integer_value = 1;
  float float_value = 1.0f;
  hal_periodic_random_int_init(&integer_random, 1u);
  hal_periodic_random_float_init(&float_random, 1u);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_periodic_random_int_get_ex(&integer_random, 0u, 10u,
                                                       0, &integer_value));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_periodic_random_float_get_ex(&float_random, 0u, 10u,
                                                         0.0f, &float_value));
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_tools_config_macro_aliases);
  RUN_TEST(test_hal_math_split_decimal_tenths_positive);
  RUN_TEST(test_hal_math_split_decimal_tenths_zero);
  RUN_TEST(test_hal_math_split_decimal_tenths_null_pointers);
  RUN_TEST(test_hal_math_join_decimal_tenths_basic);
  RUN_TEST(test_hal_math_join_decimal_tenths_roundtrip);

  RUN_TEST(test_hal_math_low_pass_alpha_one);
  RUN_TEST(test_hal_math_low_pass_alpha_zero);
  RUN_TEST(test_hal_math_low_pass_mid_alpha);
  RUN_TEST(test_hal_math_blend_alpha_one);
  RUN_TEST(test_hal_math_blend_mid_alpha);

  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_zero);
  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_boundary_512);
  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_510);
  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_511);
  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_above_512);
  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_above_1536);
  RUN_TEST(test_hal_adc_compensate_rp2040_12bit_above_3584);

  RUN_TEST(test_hal_math_percent_to_value_50);
  RUN_TEST(test_hal_math_percent_to_value_100);
  RUN_TEST(test_hal_math_percent_to_value_zero);
  RUN_TEST(test_hal_math_percent_from_value_basic);
  RUN_TEST(test_hal_math_percent_from_value_zero_max);

  RUN_TEST(test_hal_math_nonnegative_average_i32_basic);
  RUN_TEST(test_hal_math_nonnegative_average_i32_size_zero);
  RUN_TEST(test_hal_math_min_i32_basic);
  RUN_TEST(test_hal_math_min_i32_size_zero);
  RUN_TEST(test_hal_math_midpoint_min_max_i32_basic);
  RUN_TEST(test_hal_math_midpoint_min_max_i32_n_zero);
  RUN_TEST(test_hal_math_rolling_average_default_f32_accumulates);
  RUN_TEST(test_hal_math_rolling_average_default_f32_wraps_index);

  RUN_TEST(test_dst_january);
  RUN_TEST(test_dst_july);
  RUN_TEST(test_dst_november);
  RUN_TEST(test_dst_march_2024_before_last_sunday);
  RUN_TEST(test_dst_march_2024_on_last_sunday);
  RUN_TEST(test_dst_october_2024_before_last_sunday);
  RUN_TEST(test_dst_october_2024_on_last_sunday);
  RUN_TEST(test_hal_time_adjust_cet_cest_dst_plus2);
  RUN_TEST(test_hal_time_adjust_cet_cest_no_dst_plus1);
  RUN_TEST(test_hal_time_adjust_cet_cest_midnight_overflow);
  RUN_TEST(test_hal_time_adjust_cet_cest_null_safe);
  RUN_TEST(test_hal_time_adjust_cet_cest_month_and_year_rollover);

  RUN_TEST(test_MSB);
  RUN_TEST(test_LSB);
  RUN_TEST(test_MSB_and_LSB_evaluate_argument_once_and_keep_function_symbols);
  RUN_TEST(test_jh_u16_from_bytes);
  RUN_TEST(test_jh_load_be16);
  RUN_TEST(test_jh_store_be16);
  RUN_TEST(test_word_roundtrip);

  RUN_TEST(test_hal_math_round_tenth_rounds_down);
  RUN_TEST(test_hal_math_round_tenth_rounds_up);
  RUN_TEST(test_hal_math_round_precision_2dp);
  RUN_TEST(test_hal_math_round_precision_0dp);
  RUN_TEST(test_hal_text_concat_basic);
  RUN_TEST(test_hal_text_concat_empty_sources);
  RUN_TEST(test_hal_text_concat_exact_fit);
  RUN_TEST(test_hal_text_concat_too_small_buffer);
  RUN_TEST(test_hal_text_concat_zero_dest_size);
  RUN_TEST(test_hal_text_concat_null_args);
  RUN_TEST(test_hal_debug_set_module_prefix_logs_module_prefix);

  RUN_TEST(test_hal_text_is_printable_valid);
  RUN_TEST(test_hal_text_is_printable_null);
  RUN_TEST(test_hal_text_is_printable_empty);
  RUN_TEST(test_hal_text_is_printable_zero_size);
  RUN_TEST(test_hal_text_is_printable_non_printable);
  RUN_TEST(test_hal_text_is_printable_with_punctuation);

  RUN_TEST(test_hal_pixel_rgb888_to_rgb565_red);
  RUN_TEST(test_hal_pixel_rgb888_to_rgb565_green);
  RUN_TEST(test_hal_pixel_rgb888_to_rgb565_blue);
  RUN_TEST(test_hal_pixel_rgb888_to_rgb565_black);
  RUN_TEST(test_hal_pixel_rgb888_buffer_to_rgb565_buffer);
  RUN_TEST(test_hal_pixel_rgba8888_buffer_to_rgb565_buffer_ignores_alpha);
  RUN_TEST(test_rgb565_buffer_converters_reject_null);

  RUN_TEST(test_hal_network_format_mac);
  RUN_TEST(test_hal_wifi_scan_for_ssid_uses_hal_wifi_scan_results);

  RUN_TEST(test_hal_text_hex_pair_to_byte_letter_A);
  RUN_TEST(test_hal_text_hex_pair_to_byte_space);
  RUN_TEST(test_hal_text_hex_pair_to_byte_max);

  RUN_TEST(test_hal_text_url_decode_percent_encoding);
  RUN_TEST(test_hal_text_url_decode_plus_to_space);
  RUN_TEST(test_hal_text_url_decode_percent_space);

  RUN_TEST(test_hal_text_remove_whitespace_middle);
  RUN_TEST(test_hal_text_remove_whitespace_leading_trailing);
  RUN_TEST(test_hal_text_remove_whitespace_no_spaces);

  RUN_TEST(test_hal_text_starts_with_true);
  RUN_TEST(test_hal_text_starts_with_false);
  RUN_TEST(test_hal_text_starts_with_exact);

  RUN_TEST(test_hal_text_parse_number_basic);
  RUN_TEST(test_hal_text_parse_number_no_digits);
  RUN_TEST(test_hal_text_parse_number_zero);
  RUN_TEST(test_hal_gps_nmea_hex_value_digit);
  RUN_TEST(test_hal_gps_nmea_hex_value_upper_letter);
  RUN_TEST(test_hal_gps_nmea_hex_value_lower_letter);
  RUN_TEST(test_hal_gps_nmea_decimal_x100_integer);
  RUN_TEST(test_hal_gps_nmea_decimal_x100_fraction_2dp);
  RUN_TEST(test_hal_gps_nmea_decimal_x100_negative);
  RUN_TEST(test_hal_gps_nmea_degrees_gprmc_latitude_format);

  RUN_TEST(test_hal_time_is_in_range_inside);
  RUN_TEST(test_hal_time_is_in_range_at_start);
  RUN_TEST(test_hal_time_is_in_range_at_end_exclusive);
  RUN_TEST(test_hal_time_is_in_range_below);

  RUN_TEST(test_hal_time_extract_minutes_basic);
  RUN_TEST(test_hal_time_extract_minutes_zero);
  RUN_TEST(test_hal_time_extract_minutes_exact_hour);
  RUN_TEST(test_hal_time_extract_minutes_null_safe);

  RUN_TEST(test_hal_math_map_f32_midpoint);
  RUN_TEST(test_hal_math_map_f32_min);
  RUN_TEST(test_hal_math_map_f32_max);
  RUN_TEST(test_hal_math_map_f32_equal_in_range);

  RUN_TEST(test_hal_text_transliterate_ascii_plain);
  RUN_TEST(test_hal_text_transliterate_ascii_polish_l);
  RUN_TEST(test_hal_text_transliterate_ascii_mixed_word);
  RUN_TEST(test_hal_text_transliterate_ascii_strips_unknown);
  RUN_TEST(test_hal_text_transliterate_ascii_null_input);

  RUN_TEST(test_hal_text_format_binary_int_zero);
  RUN_TEST(test_hal_text_format_binary_int_one);
  RUN_TEST(test_hal_text_format_binary_int_0xFF);
  RUN_TEST(test_hal_text_format_binary_int_16bit);
  RUN_TEST(test_hal_text_format_binary_int_null_buf);

  RUN_TEST(test_hal_adc_read_average);
  RUN_TEST(test_hal_get_seconds);

  RUN_TEST(test_hal_adc_raw_to_voltage_zero_adc);
  RUN_TEST(test_hal_adc_raw_to_voltage_full_scale_no_divider);
  RUN_TEST(test_hal_adc_raw_to_voltage_half_scale_no_divider);
  RUN_TEST(test_hal_adc_raw_to_voltage_equal_divider);

  RUN_TEST(test_hal_ntc_steinhart_nominal_temp);
  RUN_TEST(test_hal_ntc_steinhart_warm_temp);

  RUN_TEST(test_hal_text_pack_field_exact_fit);
  RUN_TEST(test_hal_text_pack_field_shorter_pads_zero);
  RUN_TEST(test_hal_text_pack_field_pad_custom_byte);
  RUN_TEST(test_hal_text_pack_field_truncates_long_string);
  RUN_TEST(test_hal_text_pack_field_null_inputs_no_crash);

  RUN_TEST(test_hal_math_average_handles_large_values);
  RUN_TEST(test_hal_math_midpoint_handles_full_integer_range);
  RUN_TEST(test_hal_math_array_helpers_reject_empty_input);
  RUN_TEST(test_hal_text_url_decode_reports_overflow_and_terminates);
  RUN_TEST(test_hal_text_hex_pair_rejects_invalid_input);
  RUN_TEST(test_hal_adc_voltage_rejects_invalid_divider);
  RUN_TEST(test_hal_adc_average_uses_explicit_transform);
  RUN_TEST(test_hal_ntc_rejects_adc_endpoints);
  RUN_TEST(test_hal_pixel_converters_report_invalid_arguments);
  RUN_TEST(test_hal_periodic_random_holds_value_until_interval);
  RUN_TEST(test_hal_periodic_random_elapsed_check_is_wrap_safe);
  RUN_TEST(test_hal_periodic_random_rejects_invalid_ranges);

  return UNITY_END();
}
