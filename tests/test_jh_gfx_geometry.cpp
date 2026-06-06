#include "utils/unity.h"

#include "jh_gfx.h"

void setUp(void) {}

void tearDown(void) {}

static unsigned count_color_pixels(const JHGfxCanvas16 &canvas,
                                   uint16_t color,
                                   int16_t x0,
                                   int16_t y0,
                                   int16_t w,
                                   int16_t h) {
    unsigned count = 0;
    for (int16_t y = y0; y < y0 + h; ++y) {
        for (int16_t x = x0; x < x0 + w; ++x) {
            if (canvas.getPixel(x, y) == color) {
                ++count;
            }
        }
    }
    return count;
}

void test_draw_line_updates_canvas_pixels(void) {
    JHGfxCanvas16 canvas(8, 8);
    TEST_ASSERT_NOT_NULL(canvas.getBuffer());

    canvas.drawLine(0, 0, 4, 4, 0x1234);

    for (int16_t i = 0; i <= 4; ++i) {
        TEST_ASSERT_EQUAL_HEX16(0x1234, canvas.getPixel(i, i));
    }
    TEST_ASSERT_EQUAL_HEX16(0x0000, canvas.getPixel(0, 1));
}

void test_fill_rect_updates_expected_region(void) {
    JHGfxCanvas16 canvas(10, 8);
    TEST_ASSERT_NOT_NULL(canvas.getBuffer());

    canvas.fillRect(3, 2, 4, 3, 0xABCD);

    TEST_ASSERT_EQUAL_UINT(12, count_color_pixels(canvas, 0xABCD, 3, 2, 4, 3));
    TEST_ASSERT_EQUAL_HEX16(0x0000, canvas.getPixel(2, 2));
    TEST_ASSERT_EQUAL_HEX16(0x0000, canvas.getPixel(7, 4));
}

void test_draw_char_uses_builtin_font(void) {
    JHGfxCanvas16 canvas(12, 10);
    TEST_ASSERT_NOT_NULL(canvas.getBuffer());

    canvas.drawChar(0, 0, 'A', 0xFFFF, 0x0000, 1);

    TEST_ASSERT_TRUE(count_color_pixels(canvas, 0xFFFF, 0, 0, 6, 8) > 0);
    TEST_ASSERT_EQUAL_HEX16(0x0000, canvas.getPixel(7, 7));
}

void test_text_write_advances_cursor(void) {
    JHGfxCanvas16 canvas(60, 16);
    TEST_ASSERT_NOT_NULL(canvas.getBuffer());

    canvas.setTextSize(1);
    canvas.setTextColor(0xFFFF, 0x0000);
    canvas.setCursor(0, 0);
    canvas.write((uint8_t)'H');
    canvas.write((uint8_t)'i');

    TEST_ASSERT_EQUAL_INT16(12, canvas.getCursorX());
    TEST_ASSERT_TRUE(count_color_pixels(canvas, 0xFFFF, 0, 0, 12, 8) > 0);
}

void test_get_text_bounds(void) {
    JHGfxCanvas16 canvas(100, 20);
    int16_t x1, y1;
    uint16_t w, h;

    canvas.setTextSize(1);
    canvas.getTextBounds("ABC", 0, 0, &x1, &y1, &w, &h);

    TEST_ASSERT_EQUAL_UINT16(18, w);  // 3 chars * 6 pixels
    TEST_ASSERT_EQUAL_UINT16(8, h);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_draw_line_updates_canvas_pixels);
    RUN_TEST(test_fill_rect_updates_expected_region);
    RUN_TEST(test_draw_char_uses_builtin_font);
    RUN_TEST(test_text_write_advances_cursor);
    RUN_TEST(test_get_text_bounds);
    return UNITY_END();
}
