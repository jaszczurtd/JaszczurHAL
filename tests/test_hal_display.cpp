#include "utils/unity.h"
#include "hal/hal_display.h"
#include "hal/impl/.mock/hal_mock.h"
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
    TEST_ASSERT_FALSE(hal_display_init_ssd1306_i2c_ex(0, 64, 0, 0x3C, -1, 0x02, false));
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
    TEST_ASSERT_TRUE(hal_display_set_default_font_with_pos_and_color(11, 22, 0x07E0));

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
    TEST_ASSERT_TRUE(hal_display_draw_text_centered("abc", 0xFFFF, 0x0000, true, true));

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_configure_sets_dimensions);
    RUN_TEST(test_ssd1306_init_sets_dimensions);
    RUN_TEST(test_ssd1306_init_ex_sets_dimensions_on_selected_bus);
    RUN_TEST(test_ssd1306_init_ex_rejects_invalid_size);
    RUN_TEST(test_draw_image_draws_background_and_bitmap);
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
    return UNITY_END();
}
