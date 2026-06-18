#include "hal/hal_hd44780.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <stddef.h>
#include <stdint.h>

#define LCD_RS 2u
#define LCD_RW 3u
#define LCD_EN 4u

static const uint8_t kData4[] = {10u, 11u, 12u, 13u};
static const uint8_t kData8[] = {20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u};

typedef struct {
  bool rs;
  uint8_t data;
} lcd_bus_sample_t;

static int data_pin_index(uint8_t pin, const uint8_t *data_pins,
                          size_t data_pin_count) {
  for (size_t i = 0; i < data_pin_count; ++i) {
    if (data_pins[i] == pin) {
      return (int)i;
    }
  }
  return -1;
}

static size_t capture_enable_samples(const uint8_t *data_pins,
                                     size_t data_pin_count,
                                     lcd_bus_sample_t *out, size_t out_count) {
  bool rs = false;
  uint8_t data = 0u;
  bool enable = false;
  size_t sample_count = 0u;
  const size_t trace_count = hal_mock_gpio_trace_count();

  for (size_t i = 0; i < trace_count; ++i) {
    hal_mock_gpio_event_t event = {};
    TEST_ASSERT_TRUE(hal_mock_gpio_trace_get(i, &event));
    if (event.type != HAL_MOCK_GPIO_EVENT_WRITE) {
      continue;
    }

    if (event.pin == LCD_RS) {
      rs = event.value != 0;
      continue;
    }

    if (event.pin == LCD_EN) {
      bool new_enable = event.value != 0;
      if (!enable && new_enable && sample_count < out_count) {
        out[sample_count].rs = rs;
        out[sample_count].data = data;
        sample_count++;
      }
      enable = new_enable;
      continue;
    }

    int bit = data_pin_index(event.pin, data_pins, data_pin_count);
    if (bit >= 0) {
      if (event.value != 0) {
        data |= (uint8_t)(1u << (uint8_t)bit);
      } else {
        data &= (uint8_t) ~(1u << (uint8_t)bit);
      }
    }
  }

  return sample_count;
}

static size_t capture_4bit_bytes(lcd_bus_sample_t *out, size_t out_count) {
  lcd_bus_sample_t samples[64] = {};
  size_t sample_count = capture_enable_samples(kData4, COUNTOF(kData4), samples,
                                               COUNTOF(samples));
  size_t byte_count = 0u;
  for (size_t i = 0; (i + 1u) < sample_count && byte_count < out_count;
       i += 2u) {
    out[byte_count].rs = samples[i].rs;
    out[byte_count].data = (uint8_t)(((samples[i].data & 0x0Fu) << 4) |
                                     (samples[i + 1u].data & 0x0Fu));
    byte_count++;
  }
  return byte_count;
}

static size_t capture_8bit_bytes(lcd_bus_sample_t *out, size_t out_count) {
  return capture_enable_samples(kData8, COUNTOF(kData8), out, out_count);
}

void setUp(void) {
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
  hal_mock_set_micros(0u);
}

void tearDown(void) {
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
}

void test_constructor_configures_4bit_gpio_without_rw(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  (void)lcd;

  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(LCD_RS));
  TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(LCD_EN));
  for (size_t i = 0; i < COUNTOF(kData4); ++i) {
    TEST_ASSERT_EQUAL_INT(HAL_GPIO_OUTPUT, hal_mock_gpio_get_mode(kData4[i]));
  }
  TEST_ASSERT_GREATER_THAN_UINT32(50000u, hal_micros());
}

void test_command_sends_high_then_low_nibble_with_rs_low(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  hal_mock_gpio_trace_reset();
  hal_mock_set_micros(0u);

  lcd.command(0xABu);

  lcd_bus_sample_t bytes[2] = {};
  TEST_ASSERT_EQUAL_UINT32(1u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_FALSE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8(0xABu, bytes[0].data);
  TEST_ASSERT_EQUAL_UINT32(204u, hal_micros());
}

void test_write_sends_high_then_low_nibble_with_rs_high(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  hal_mock_gpio_trace_reset();

  TEST_ASSERT_EQUAL_UINT32(1u, lcd.write((uint8_t)'H'));

  lcd_bus_sample_t bytes[2] = {};
  TEST_ASSERT_EQUAL_UINT32(1u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_TRUE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8(0x48u, bytes[0].data);
}

void test_8bit_mode_writes_one_enable_pulse_per_byte(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData8[0], kData8[1], kData8[2], kData8[3],
              kData8[4], kData8[5], kData8[6], kData8[7]);
  hal_mock_gpio_trace_reset();

  lcd.command(0xA5u);

  lcd_bus_sample_t bytes[2] = {};
  TEST_ASSERT_EQUAL_UINT32(1u, capture_8bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_FALSE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8(0xA5u, bytes[0].data);
}

void test_set_cursor_uses_original_row_offsets(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  lcd.begin(20u, 4u);
  hal_mock_gpio_trace_reset();

  lcd.setCursor(3u, 2u);

  lcd_bus_sample_t bytes[2] = {};
  TEST_ASSERT_EQUAL_UINT32(1u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_FALSE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8(0x97u, bytes[0].data); /* 0x80 | (3 + 20) */
}

void test_create_char_masks_location_and_writes_eight_rows(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  const uint8_t glyph[8] = {0x00u, 0x04u, 0x0Eu, 0x15u,
                            0x04u, 0x04u, 0x04u, 0x00u};
  hal_mock_gpio_trace_reset();

  lcd.createChar(9u, glyph);

  lcd_bus_sample_t bytes[10] = {};
  TEST_ASSERT_EQUAL_UINT32(9u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_FALSE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8(0x48u, bytes[0].data); /* 0x40 | ((9 & 7) << 3) */
  for (size_t i = 0; i < COUNTOF(glyph); ++i) {
    TEST_ASSERT_TRUE(bytes[i + 1u].rs);
    TEST_ASSERT_EQUAL_HEX8(glyph[i], bytes[i + 1u].data);
  }
}

void test_print_string_and_number_use_lcd_write_path(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  hal_mock_gpio_trace_reset();

  TEST_ASSERT_EQUAL_UINT32(2u, lcd.print("Hi"));
  TEST_ASSERT_EQUAL_UINT32(2u, lcd.print(42));

  lcd_bus_sample_t bytes[5] = {};
  TEST_ASSERT_EQUAL_UINT32(4u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_TRUE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8('H', bytes[0].data);
  TEST_ASSERT_EQUAL_HEX8('i', bytes[1].data);
  TEST_ASSERT_EQUAL_HEX8('4', bytes[2].data);
  TEST_ASSERT_EQUAL_HEX8('2', bytes[3].data);
}

void test_println_advances_to_next_line_without_control_bytes(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);
  lcd.begin(16u, 2u);
  hal_mock_gpio_trace_reset();

  lcd.println("A");

  /* Expect the glyph 'A' followed by a cursor move to row 1, col 0
   * (LCD_SETDDRAMADDR | 0x40 == 0xC0) -- never the CR/LF control bytes. */
  lcd_bus_sample_t bytes[4] = {};
  TEST_ASSERT_EQUAL_UINT32(2u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_TRUE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8('A', bytes[0].data);
  TEST_ASSERT_FALSE(bytes[1].rs);
  TEST_ASSERT_EQUAL_HEX8(0xC0u, bytes[1].data);
  TEST_ASSERT_NOT_EQUAL_HEX8(0x0Du, bytes[0].data);
  TEST_ASSERT_NOT_EQUAL_HEX8(0x0Au, bytes[0].data);

  /* A second println wraps from the last row back to row 0 (0x80). */
  hal_mock_gpio_trace_reset();
  lcd.println("B");
  TEST_ASSERT_EQUAL_UINT32(2u, capture_4bit_bytes(bytes, COUNTOF(bytes)));
  TEST_ASSERT_TRUE(bytes[0].rs);
  TEST_ASSERT_EQUAL_HEX8('B', bytes[0].data);
  TEST_ASSERT_FALSE(bytes[1].rs);
  TEST_ASSERT_EQUAL_HEX8(0x80u, bytes[1].data);
}

void test_multibyte_operations_take_one_instance_mutex(void) {
  HD44780 lcd(LCD_RS, LCD_EN, kData4[0], kData4[1], kData4[2], kData4[3]);

  hal_mock_mutex_stats_reset();
  TEST_ASSERT_EQUAL_UINT32(2u, lcd.print("Hi"));
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_lock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_unlock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_max_depth());

  const uint8_t glyph[8] = {0x00u, 0x04u, 0x0Eu, 0x15u,
                            0x04u, 0x04u, 0x04u, 0x00u};
  hal_mock_mutex_stats_reset();
  lcd.createChar(2u, glyph);
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_lock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_unlock_count());
  TEST_ASSERT_EQUAL_UINT32(1u, hal_mock_mutex_max_depth());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_constructor_configures_4bit_gpio_without_rw);
  RUN_TEST(test_command_sends_high_then_low_nibble_with_rs_low);
  RUN_TEST(test_write_sends_high_then_low_nibble_with_rs_high);
  RUN_TEST(test_8bit_mode_writes_one_enable_pulse_per_byte);
  RUN_TEST(test_set_cursor_uses_original_row_offsets);
  RUN_TEST(test_create_char_masks_location_and_writes_eight_rows);
  RUN_TEST(test_print_string_and_number_use_lcd_write_path);
  RUN_TEST(test_println_advances_to_next_line_without_control_bytes);
  RUN_TEST(test_multibyte_operations_take_one_instance_mutex);
  return UNITY_END();
}
