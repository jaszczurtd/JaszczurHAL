#include "hal/display/hal_display.h"
#include "hal/i2c/hal_i2c.h"
#include "support/ssd1306_panel.h"
#include "utils/unity.h"

#include <vector>

static Ssd1306Panel panel;
static std::vector<uint8_t> tx;
static int writes_until_error = -1;
static int frames = 0;
#ifdef JH_TEST_WRAP_MALLOC
static size_t allocations = 0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  ++allocations;
  return __real_malloc(size);
}
#endif
extern "C" {
hal_status_t hal_i2c_set_clock_bus(uint8_t, uint32_t) { return HAL_OK; }
void hal_i2c_begin_transmission_bus(uint8_t, hal_i2c_address_t) { tx.clear(); }
size_t hal_i2c_write_bus(uint8_t, uint8_t value) {
  tx.push_back(value);
  return 1;
}
uint8_t hal_i2c_end_transmission_bus(uint8_t) {
  if (writes_until_error == 0) {
    writes_until_error = -1;
    return 2;
  }
  if (writes_until_error > 0)
    --writes_until_error;
  panel.frame(tx.data(), tx.size());
  ++frames;
  return 0;
}
}

void setUp(void) { writes_until_error = -1; }
void tearDown(void) {}
static hal_display_ssd1306_family_config_t config(bool direct) {
  hal_display_ssd1306_family_config_t c = {};
  c.width = 128;
  c.height = 32;
  c.rst_pin = -1;
  c.buffer_mode = direct ? HAL_DISPLAY_UNBUFFERED : HAL_DISPLAY_BUFFERED;
  return c;
}
static const uint8_t glyph[] = {0x81, 0x42, 0x24};
static const hal_display_mono_font_t font = {glyph, sizeof(glyph), 'A', 'A', 3};
static hal_status_t text(int x = 4, int y = 8, uint8_t scale = 2) {
  return hal_display_draw_mono_text_ex(x, y, "AA", &font, scale,
                                       HAL_COLOR_WHITE, HAL_COLOR_BLACK);
}

void test_direct_matches_buffered_pixels_and_uses_no_heap(void) {
  auto c = config(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  TEST_ASSERT_EQUAL_INT(HAL_OK, text());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_flush_ex());
  const auto buffered = panel.ram;
  c.buffer_mode = HAL_DISPLAY_UNBUFFERED;
#ifdef JH_TEST_WRAP_MALLOC
  const size_t before = allocations;
#endif
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  TEST_ASSERT_EQUAL_INT(HAL_OK, text());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(buffered.data(), panel.ram.data(), 512);
#ifdef JH_TEST_WRAP_MALLOC
  TEST_ASSERT_EQUAL_size_t(before, allocations);
#endif
  const int sent = frames;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_flush_ex());
  TEST_ASSERT_EQUAL_INT(sent, frames);
  // Independent expected scaled columns, including both halves of bit 7.
  TEST_ASSERT_EQUAL_HEX8(0x03, panel.ram[128 + 4]);
  TEST_ASSERT_EQUAL_HEX8(0xc0, panel.ram[256 + 4]);
  hal_display_capabilities_t caps = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_get_capabilities_ex(&caps));
  TEST_ASSERT_BITS_HIGH(HAL_DISPLAY_CAP_MONO_TEXT | HAL_DISPLAY_CAP_RAW_WRITE,
                        caps.flags);
  TEST_ASSERT_BITS_LOW(HAL_DISPLAY_CAP_BUFFERED | HAL_DISPLAY_CAP_LEGACY_GFX,
                       caps.flags);
  TEST_ASSERT_EQUAL_UINT16(8, caps.y_alignment);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_display_set_rotation_ex(1));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, hal_display_print_ex("A"));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_draw_line_ex(0, 0, 5, 5, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_display_fill_rect_ex(0, 1, 5, 8, 0));
}

void test_profile_overrides_zero_values_and_defaults_are_preserved(void) {
  auto c = config(true);
  c.override_panel = true;
  c.panel = {0x3f, 0xf0, 0x22, 0x20, 0, false};
  const auto commands = panel.commands;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  TEST_ASSERT_EQUAL_size_t(commands[0x33], panel.commands[0x33]);
  TEST_ASSERT_EQUAL_size_t(commands[0x2e], panel.commands[0x2e]);
  TEST_ASSERT_EQUAL_HEX8(0x3f, panel.registers[0x81]);
  TEST_ASSERT_EQUAL_HEX8(0xf0, panel.registers[0xd5]);
  TEST_ASSERT_EQUAL_HEX8(0x22, panel.registers[0xd9]);
  TEST_ASSERT_EQUAL_HEX8(0x20, panel.registers[0xdb]);
  TEST_ASSERT_EQUAL_HEX8(2, panel.registers[0x20]);
  c.panel.contrast = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  TEST_ASSERT_EQUAL_HEX8(0, panel.registers[0x81]);
  c = config(false);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  TEST_ASSERT_EQUAL_size_t(commands[0x33] + 1, panel.commands[0x33]);
  TEST_ASSERT_EQUAL_size_t(commands[0x2e] + 1, panel.commands[0x2e]);
  TEST_ASSERT_EQUAL_HEX8(0x8f, panel.registers[0x81]);
  TEST_ASSERT_EQUAL_HEX8(0x80, panel.registers[0xd5]);
  TEST_ASSERT_EQUAL_HEX8(0xf1, panel.registers[0xd9]);
  TEST_ASSERT_EQUAL_HEX8(0x40, panel.registers[0xdb]);
  TEST_ASSERT_EQUAL_HEX8(0, panel.registers[0x20]);
}

void test_direct_failure_repositions_and_retry_repairs_cell(void) {
  auto c = config(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  writes_until_error = 1; // Address succeeds, data fails.
  TEST_ASSERT_EQUAL_INT(HAL_EIO, text());
  TEST_ASSERT_EQUAL_INT(HAL_OK, text());
  TEST_ASSERT_EQUAL_HEX8(0x03, panel.ram[128 + 4]);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_suspend_ex());
  TEST_ASSERT_FALSE(panel.enabled);
  TEST_ASSERT_EQUAL_INT(HAL_OK, text());
  TEST_ASSERT_FALSE(panel.enabled);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_resume_ex());
  TEST_ASSERT_EQUAL_INT(HAL_OK, text());
  writes_until_error = 0;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_display_init_ssd1306_family_ex(&c));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, text());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
}

void test_text_validation_clipping_background_and_raw_pages(void) {
  auto c = config(true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_init_ssd1306_family_ex(&c));
  const int before = frames;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, text(0, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, text(-1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, text(0, 24, 2));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, text(0, 0, 0));
  auto bad = font;
  bad.data_size = 2;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_display_draw_mono_text_ex(0, 0, "A", &bad, 1, 1, 0));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_display_draw_mono_text_ex(0, 0, "A\n", &font, 1, 1, 0));
  TEST_ASSERT_EQUAL_INT(before, frames);
  TEST_ASSERT_EQUAL_INT(HAL_OK, text(122, 0)); // One complete enlarged cell.
  TEST_ASSERT_EQUAL_HEX8(3, panel.ram[122]);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_display_draw_mono_text_ex(122, 0, "?", &font, 2, 0, 1));
  TEST_ASSERT_EQUAL_HEX8(0xff, panel.ram[122]);
  const uint8_t raw[] = {0xa5, 0x5a};
  hal_display_buffer_desc_t desc = {
      HAL_DISPLAY_PIXEL_FORMAT_MONO01, 2, 2, 8, sizeof(raw), false};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_display_write_raw_ex(126, 24, &desc, raw));
  TEST_ASSERT_EQUAL_HEX8(0xa5, panel.ram[510]);
  TEST_ASSERT_EQUAL_HEX8(0x5a, panel.ram[511]);
  desc.buf_size = 1;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_display_write_raw_ex(126, 24, &desc, raw));
  c.height = 31;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_display_init_ssd1306_family_ex(&c));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_direct_matches_buffered_pixels_and_uses_no_heap);
  RUN_TEST(test_profile_overrides_zero_values_and_defaults_are_preserved);
  RUN_TEST(test_direct_failure_repositions_and_retry_repairs_cell);
  RUN_TEST(test_text_validation_clipping_background_and_raw_pages);
  return UNITY_END();
}
