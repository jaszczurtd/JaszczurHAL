#include "hal/display/hal_display.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"
#include <string.h>

void setUp(void) {
  hal_mock_display_reset();
  hal_mock_serial_reset();
  hal_display_configure(128, 32, 0, false, false);
}

void tearDown(void) {}

void test_configure_sets_dimensions(void) {
  TEST_ASSERT_TRUE(hal_display_configure(240, 320, 1, false, false));
  TEST_ASSERT_EQUAL_INT(240, hal_display_get_width());
  TEST_ASSERT_EQUAL_INT(320, hal_display_get_height());
}

void test_ssd1306_init_sets_dimensions(void) {
  bool ok = hal_display_init_ssd1306_i2c(128, 32, 0x3C, -1, 0x02, false);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT(128, hal_display_get_width());
  TEST_ASSERT_EQUAL_INT(32, hal_display_get_height());
}

void test_ssd1306_init_ex_sets_dimensions_on_selected_bus(void) {
  bool ok = hal_display_init_ssd1306_i2c_ex(128, 64, 1, 0x3C, -1, 0x02, false);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT(128, hal_display_get_width());
  TEST_ASSERT_EQUAL_INT(64, hal_display_get_height());
}

void test_ssd1306_init_ex_rejects_invalid_size(void) {
  TEST_ASSERT_FALSE(
      hal_display_init_ssd1306_i2c_ex(0, 64, 0, 0x3C, -1, 0x02, false));
}

void test_display_buffer_descriptor_describes_raw_area(void) {
  const hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE, 128u, 64u, 32u, 4096u, true};

  TEST_ASSERT_EQUAL_UINT32(HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE,
                           desc.pixel_format);
  TEST_ASSERT_EQUAL_UINT16(128u, desc.pitch);
  TEST_ASSERT_EQUAL_UINT16(64u, desc.width);
  TEST_ASSERT_EQUAL_UINT16(32u, desc.height);
  TEST_ASSERT_EQUAL_size_t(4096u, desc.buf_size);
  TEST_ASSERT_TRUE(desc.frame_incomplete);
}

void test_capabilities_describe_active_mock_backend(void) {
  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_EQUAL_UINT16(128u, caps.width);
  TEST_ASSERT_EQUAL_UINT16(32u, caps.height);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE,
                        caps.supported_pixel_formats);
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_CAP_RAW_WRITE, caps.flags);
  TEST_ASSERT_EQUAL_UINT8(HAL_DISPLAY_ROTATION_MASK_ALL,
                          caps.supported_rotations);
}

void test_raw_write_validates_descriptor_and_area(void) {
  const uint8_t pixels[8] = {};
  hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE, 2u, 2u, 2u, sizeof(pixels), false};

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_write_raw_ex(1u, 2u, &desc, pixels));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(127u, 0u, &desc, pixels));
  desc.pixel_format = HAL_DISPLAY_PIXEL_FORMAT_MONO01;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_write_raw_ex(0u, 0u, &desc, pixels));
}

void test_raw_write_supports_padded_pitch(void) {
  const uint8_t pixels[8] = {};
  hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE, 3u, 2u, 2u, sizeof(pixels), false};

  /* pitch=3 with height=2 needs (2-1)*3*2 + 2*2 = 10 bytes; the 8-byte
   * buffer only covers a tightly packed layout. */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(0u, 0u, &desc, pixels));

  const uint8_t padded_pixels[10] = {};
  desc.buf_size = sizeof(padded_pixels);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_write_raw_ex(0u, 0u, &desc, padded_pixels));
}

void test_draw_image_draws_background_and_bitmap(void) {
  uint16_t data[6] = {1, 2, 3, 4, 5, 6};

  TEST_ASSERT_TRUE(hal_display_draw_image(10, 20, 3, 2, 0xF800, data));

  int x = 0, y = 0, w = 0, h = 0;
  uint16_t color = 0;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  TEST_ASSERT_EQUAL_INT(10, x);
  TEST_ASSERT_EQUAL_INT(20, y);
  TEST_ASSERT_EQUAL_INT(3, w);
  TEST_ASSERT_EQUAL_INT(2, h);
  TEST_ASSERT_EQUAL_HEX16(0xF800, color);

  uint16_t *bitmap_ptr = NULL;
  hal_mock_display_get_last_bitmap(&x, &y, &bitmap_ptr, &w, &h);
  TEST_ASSERT_EQUAL_INT(10, x);
  TEST_ASSERT_EQUAL_INT(20, y);
  TEST_ASSERT_EQUAL_INT(3, w);
  TEST_ASSERT_EQUAL_INT(2, h);
  TEST_ASSERT_EQUAL_PTR(data, bitmap_ptr);
}

void test_stream_write_api_tracks_window_and_counts(void) {
  uint16_t pixels[] = {0x1111, 0x2222, 0x3333, 0x4444};
  uint8_t bytes[] = {0x12, 0x34, 0x56, 0x78};

  TEST_ASSERT_TRUE(hal_display_begin_write(3, 4, 2, 2));
  TEST_ASSERT_TRUE(hal_mock_display_stream_active());
  TEST_ASSERT_TRUE(hal_display_write_pixels_fast(pixels, 4));
  TEST_ASSERT_TRUE(hal_display_write_pixels_be(bytes, sizeof(bytes)));
  TEST_ASSERT_TRUE(hal_display_write_pixels_dma(bytes, sizeof(bytes)));
  TEST_ASSERT_TRUE(hal_display_end_write());
  TEST_ASSERT_FALSE(hal_mock_display_stream_active());

  int x = 0, y = 0, w = 0, h = 0;
  hal_mock_display_get_last_stream_window(&x, &y, &w, &h);
  TEST_ASSERT_EQUAL_INT(3, x);
  TEST_ASSERT_EQUAL_INT(4, y);
  TEST_ASSERT_EQUAL_INT(2, w);
  TEST_ASSERT_EQUAL_INT(2, h);
  TEST_ASSERT_EQUAL_size_t(4u, hal_mock_display_get_stream_fast_pixels());
  TEST_ASSERT_EQUAL_size_t(sizeof(bytes),
                           hal_mock_display_get_stream_be_bytes());
  TEST_ASSERT_EQUAL_size_t(sizeof(bytes),
                           hal_mock_display_get_stream_dma_bytes());
}

void test_stream_write_api_rejects_invalid_order_and_odd_bytes(void) {
  uint16_t pixels[] = {0x1111};
  uint8_t bytes[] = {0x12, 0x34, 0x56};

  TEST_ASSERT_FALSE(hal_display_write_pixels_fast(pixels, 1));
  TEST_ASSERT_TRUE(hal_display_begin_write(0, 0, 1, 1));
  TEST_ASSERT_FALSE(hal_display_write_pixels_be(bytes, sizeof(bytes)));
  TEST_ASSERT_TRUE(hal_display_end_write());
}

void test_text_bounds_and_size_helpers(void) {
  int w = 0, h = 0;
  TEST_ASSERT_TRUE(hal_display_get_text_bounds("abc", &w, &h));

  TEST_ASSERT_EQUAL_INT(18, w);
  TEST_ASSERT_EQUAL_INT(8, h);
  TEST_ASSERT_EQUAL_INT(18, hal_display_text_width("abc"));
  TEST_ASSERT_EQUAL_INT(8, hal_display_text_height("abc"));
}

void test_prepare_text_formats_and_returns_width(void) {
  char buf[32];
  int width = hal_display_prepare_text(buf, sizeof(buf), "V=%d", 42);

  TEST_ASSERT_EQUAL_STRING("V=42", buf);
  TEST_ASSERT_EQUAL_INT(24, width);
}

void test_prepare_text_invalid_args_return_zero(void) {
  char buf[8] = "abc";

  TEST_ASSERT_EQUAL_INT(0, hal_display_prepare_text(NULL, sizeof(buf), "x"));
  TEST_ASSERT_EQUAL_INT(0, hal_display_prepare_text(buf, 0, "x"));
  TEST_ASSERT_EQUAL_INT(0, hal_display_prepare_text(buf, sizeof(buf), NULL));
}

void test_println_prepared_text_routes_to_println(void) {
  char line[] = "hello";
  TEST_ASSERT_TRUE(hal_display_println_prepared_text(line));
  TEST_ASSERT_EQUAL_STRING("hello", hal_mock_display_last_println());
}

void test_set_default_font_and_size(void) {
  TEST_ASSERT_TRUE(hal_display_set_default_font());

  TEST_ASSERT_EQUAL_INT(HAL_FONT_DEFAULT, hal_mock_display_get_font());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_display_get_text_size());
}

void test_set_default_font_with_pos_and_color(void) {
  TEST_ASSERT_TRUE(
      hal_display_set_default_font_with_pos_and_color(11, 22, 0x07E0));

  int x = 0, y = 0;
  hal_mock_display_get_cursor(&x, &y);
  TEST_ASSERT_EQUAL_INT(HAL_FONT_DEFAULT, hal_mock_display_get_font());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_display_get_text_size());
  TEST_ASSERT_EQUAL_HEX16(0x07E0, hal_mock_display_get_text_color());
  TEST_ASSERT_EQUAL_INT(11, x);
  TEST_ASSERT_EQUAL_INT(22, y);
}

void test_set_sans_bold_with_pos_and_color(void) {
  TEST_ASSERT_TRUE(hal_display_set_sans_bold_with_pos_and_color(7, 9, 0x001F));

  int x = 0, y = 0;
  hal_mock_display_get_cursor(&x, &y);
  TEST_ASSERT_EQUAL_INT(HAL_FONT_SANS_BOLD_9PT, hal_mock_display_get_font());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_display_get_text_size());
  TEST_ASSERT_EQUAL_HEX16(0x001F, hal_mock_display_get_text_color());
  TEST_ASSERT_EQUAL_INT(7, x);
  TEST_ASSERT_EQUAL_INT(9, y);
}

void test_set_serif9pt_with_color(void) {
  TEST_ASSERT_TRUE(hal_display_set_serif9pt_with_color(0xFFFF));

  TEST_ASSERT_EQUAL_INT(HAL_FONT_SERIF_9PT, hal_mock_display_get_font());
  TEST_ASSERT_EQUAL_INT(1, hal_mock_display_get_text_size());
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, hal_mock_display_get_text_color());
}

void test_print_at_sets_cursor_and_prints(void) {
  TEST_ASSERT_TRUE(hal_display_print_at(12, 9, "abc"));

  int x = 0, y = 0;
  hal_mock_display_get_cursor(&x, &y);
  TEST_ASSERT_EQUAL_INT(12, x);
  TEST_ASSERT_EQUAL_INT(9, y);
  TEST_ASSERT_EQUAL_STRING("abc", hal_mock_display_last_print());
}

void test_clear_text_line_uses_full_width_rect(void) {
  TEST_ASSERT_TRUE(hal_display_configure(128, 32, 0, false, false));
  TEST_ASSERT_TRUE(hal_display_clear_text_line(2, 10, 0x0000));

  int x = 0, y = 0, w = 0, h = 0;
  uint16_t color = 0;
  hal_mock_display_get_last_fill_rect(&x, &y, &w, &h, &color);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(20, y);
  TEST_ASSERT_EQUAL_INT(128, w);
  TEST_ASSERT_EQUAL_INT(10, h);
  TEST_ASSERT_EQUAL_HEX16(0x0000, color);
}

void test_print_line_clears_then_prints_with_color(void) {
  TEST_ASSERT_TRUE(hal_display_configure(128, 32, 0, false, false));
  TEST_ASSERT_TRUE(hal_display_print_line(1, 10, "line", true, 0xFFFF, 0x0000));

  int x = 0, y = 0;
  hal_mock_display_get_cursor(&x, &y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(10, y);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, hal_mock_display_get_text_color());
  TEST_ASSERT_EQUAL_STRING("line", hal_mock_display_last_print());
}

void test_draw_text_centered_positions_text(void) {
  TEST_ASSERT_TRUE(
      hal_display_draw_text_centered("abc", 0xFFFF, 0x0000, true, true));

  int x = 0, y = 0;
  hal_mock_display_get_cursor(&x, &y);
  TEST_ASSERT_EQUAL_INT(55, x);
  TEST_ASSERT_EQUAL_INT(12, y);
  TEST_ASSERT_EQUAL_STRING("abc", hal_mock_display_last_print());
}

void test_invalid_print_line_null_text_is_rejected(void) {
  TEST_ASSERT_FALSE(hal_display_print_line(0, 10, NULL, true, 0xFFFF, 0x0000));

  int x = -1, y = -1;
  hal_mock_display_get_cursor(&x, &y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(0, y);
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_invalid_print_at_null_text_is_rejected(void) {
  TEST_ASSERT_FALSE(hal_display_print_at(10, 10, NULL));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_invalid_clear_text_line_height_is_rejected(void) {
  TEST_ASSERT_FALSE(hal_display_clear_text_line(0, 0, 0x0000));
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_invalid_text_size_zero_is_rejected(void) {
  TEST_ASSERT_TRUE(hal_display_set_text_size(2));
  TEST_ASSERT_FALSE(hal_display_set_text_size(0));
  TEST_ASSERT_EQUAL_UINT8(2, hal_mock_display_get_text_size());
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

void test_invalid_get_text_bounds_null_text_returns_zero(void) {
  int w = 123;
  int h = 456;
  TEST_ASSERT_FALSE(hal_display_get_text_bounds(NULL, &w, &h));
  TEST_ASSERT_EQUAL_INT(0, w);
  TEST_ASSERT_EQUAL_INT(0, h);
  TEST_ASSERT_TRUE(strlen(hal_mock_serial_last_line()) > 0);
}

/* ---- Status-returning (_ex) API coverage ---- */

void test_ex_configure_and_getters_report_status(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_display_configure_ex(240, 320, 1, false, false));
  int w = 0;
  int h = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_width_ex(&w));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_height_ex(&h));
  TEST_ASSERT_EQUAL_INT(240, w);
  TEST_ASSERT_EQUAL_INT(320, h);

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_configure_ex(0, 320, 1, false, false));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_get_width_ex(NULL));
}

void test_ex_getters_report_uninit_when_unconfigured(void) {
  hal_mock_display_reset();
  int w = 123;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_display_get_width_ex(&w));
  TEST_ASSERT_EQUAL_INT(0, w);
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_display_fill_screen_ex(0x1234));
}

void test_ex_geometry_validates_arguments(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_fill_rect_ex(0, 0, 10, 10, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_fill_rect_ex(0, 0, 0, 10, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_fill_circle_ex(0, 0, -1, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_fill_round_rect_ex(0, 0, 10, 10, -1, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_draw_image_ex(0, 0, 2, 2, 0, NULL));
}

void test_ex_stream_write_flow_and_state(void) {
  const uint8_t pixels_be[4] = {0x12, 0x34, 0x56, 0x78};

  /* No open stream yet. */
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_display_write_pixels_be_ex(pixels_be, 4u));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_begin_write_ex(0, 0, 10, 10));
  TEST_ASSERT_TRUE(hal_mock_display_stream_active());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_pixels_be_ex(pixels_be, 4u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_pixels_dma_ex(pixels_be, 4u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_pixels_be_ex(pixels_be, 3u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_end_write_ex());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_display_end_write_ex());
}

void test_ex_text_helpers_report_status(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_set_text_size_ex(0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_set_text_size_ex(2));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_print_ex(NULL));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_print_ex("hello"));

  int width = -1;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_text_width_ex("hello", &width));
  TEST_ASSERT_EQUAL_INT((int)strlen("hello") * 6, width);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_text_width_ex(NULL, &width));
}

void test_ex_ssd1306_status_init_sets_dimensions(void) {
  hal_mock_display_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_i2c_status_ex(
                                    128, 64, 1, 0x3C, -1, 0x02, false));
  int w = 0;
  int h = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_width_ex(&w));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_height_ex(&h));
  TEST_ASSERT_EQUAL_INT(128, w);
  TEST_ASSERT_EQUAL_INT(64, h);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_init_ssd1306_i2c_status_ex(
                                        0, 64, 0, 0x3C, -1, 0x02, false));
}

void test_ex_ssd1306_family_init_and_power_status(void) {
  hal_mock_display_reset();
  hal_display_ssd1306_family_config_t config = {};
  config.controller = HAL_DISPLAY_OLED_CONTROLLER_SH1106;
  config.bus_type = HAL_DISPLAY_OLED_BUS_I2C;
  config.width = 128;
  config.height = 64;
  config.bus = 1u;
  config.i2c_addr = 0x3Cu;
  config.rst_pin = -1;
  config.switchvcc = HAL_DISPLAY_VCC_SWITCHCAP;
  config.segment_offset = 2u;
  config.page_offset = 1u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&config));
  TEST_ASSERT_EQUAL_INT(128, hal_display_get_width());
  TEST_ASSERT_EQUAL_INT(64, hal_display_get_height());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_suspend_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_resume_ex());

  config.width = 0;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_init_ssd1306_family_ex(&config));
}

void test_ex_prepare_text_formats_and_reports_status(void) {
  char buf[32] = {};
  int width = 0;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_prepare_text_ex(buf, sizeof(buf), &width, "x=%d", 7));
  TEST_ASSERT_EQUAL_STRING("x=7", buf);
  TEST_ASSERT_EQUAL_INT((int)strlen("x=7") * 6, width);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_display_prepare_text_ex(NULL, sizeof(buf), &width, "x"));
}

void test_status_init_and_soft_init_return_real_results(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init(1u, 2u, 3u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_soft_init(0));
}

void test_status_stream_distinguishes_busy_and_invalid_state(void) {
  const uint8_t pixels_be[2] = {0x12, 0x34};
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        hal_display_write_pixels_be_ex(pixels_be, 2u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_begin_write_ex(0, 0, 2, 2));
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, hal_display_begin_write_ex(0, 0, 2, 2));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_end_write_ex());
}

void test_status_flush_surfaces_backend_io_failure(void) {
  hal_mock_display_fail_next_io();
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_display_flush_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_flush_ex());
}

void test_status_prepare_text_reports_overflow(void) {
  char buf[4] = {};
  int width = 123;
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, hal_display_prepare_text_ex(
                                           buf, sizeof(buf), &width, "abcdef"));
  TEST_ASSERT_EQUAL_STRING("abc", buf);
  TEST_ASSERT_EQUAL_INT(0, width);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_configure_sets_dimensions);
  RUN_TEST(test_ssd1306_init_sets_dimensions);
  RUN_TEST(test_ssd1306_init_ex_sets_dimensions_on_selected_bus);
  RUN_TEST(test_ssd1306_init_ex_rejects_invalid_size);
  RUN_TEST(test_display_buffer_descriptor_describes_raw_area);
  RUN_TEST(test_capabilities_describe_active_mock_backend);
  RUN_TEST(test_raw_write_validates_descriptor_and_area);
  RUN_TEST(test_raw_write_supports_padded_pitch);
  RUN_TEST(test_draw_image_draws_background_and_bitmap);
  RUN_TEST(test_stream_write_api_tracks_window_and_counts);
  RUN_TEST(test_stream_write_api_rejects_invalid_order_and_odd_bytes);
  RUN_TEST(test_text_bounds_and_size_helpers);
  RUN_TEST(test_prepare_text_formats_and_returns_width);
  RUN_TEST(test_prepare_text_invalid_args_return_zero);
  RUN_TEST(test_println_prepared_text_routes_to_println);
  RUN_TEST(test_set_default_font_and_size);
  RUN_TEST(test_set_default_font_with_pos_and_color);
  RUN_TEST(test_set_sans_bold_with_pos_and_color);
  RUN_TEST(test_set_serif9pt_with_color);
  RUN_TEST(test_print_at_sets_cursor_and_prints);
  RUN_TEST(test_clear_text_line_uses_full_width_rect);
  RUN_TEST(test_print_line_clears_then_prints_with_color);
  RUN_TEST(test_draw_text_centered_positions_text);
  RUN_TEST(test_invalid_print_line_null_text_is_rejected);
  RUN_TEST(test_invalid_print_at_null_text_is_rejected);
  RUN_TEST(test_invalid_clear_text_line_height_is_rejected);
  RUN_TEST(test_invalid_text_size_zero_is_rejected);
  RUN_TEST(test_invalid_get_text_bounds_null_text_returns_zero);
  RUN_TEST(test_ex_configure_and_getters_report_status);
  RUN_TEST(test_ex_getters_report_uninit_when_unconfigured);
  RUN_TEST(test_ex_geometry_validates_arguments);
  RUN_TEST(test_ex_stream_write_flow_and_state);
  RUN_TEST(test_ex_text_helpers_report_status);
  RUN_TEST(test_ex_ssd1306_status_init_sets_dimensions);
  RUN_TEST(test_ex_ssd1306_family_init_and_power_status);
  RUN_TEST(test_ex_prepare_text_formats_and_reports_status);
  RUN_TEST(test_status_init_and_soft_init_return_real_results);
  RUN_TEST(test_status_stream_distinguishes_busy_and_invalid_state);
  RUN_TEST(test_status_flush_surfaces_backend_io_failure);
  RUN_TEST(test_status_prepare_text_reports_overflow);
  return UNITY_END();
}
