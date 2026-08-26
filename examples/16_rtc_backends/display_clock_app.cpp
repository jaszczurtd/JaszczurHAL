#include <hal/core/hal_app.h>
#include <hal/display/hal_display.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/rtc/hal_rtc.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <tools.h>
#include <utils/draw7Segment.h>

#include <cstring>

namespace {

constexpr uint8_t kRtcSdaPin = 25u; /* PB9, D14. */
constexpr uint8_t kRtcSclPin = 24u; /* PB8, D15. */
constexpr uint8_t kTftCsPin = 22u;  /* PB6, D10. */
constexpr uint8_t kTftDcPin = 39u;  /* PC7, D9. */
constexpr uint8_t kTftRstPin = 9u;  /* PA9, D8. */

constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
constexpr int kDigitWidth = 36;
constexpr int kDigitSlotWidth = 42;
constexpr int kColonSlotWidth = 22;
constexpr int kDigitHeight = 86;
constexpr float kSegmentThickness = 6.0f;

/* Approximate local wall time captured when this battery-retention fixture was
 * prepared. It advances the older valid test value once, but is never used to
 * hide a lost-integrity condition after battery removal. */
constexpr hal_rtc_datetime_t kInitialTime = {
    .second = 0u,
    .minute = 20u,
    .hour = 22u,
    .day = 26u,
    .weekday = 3u,
    .month = 8u,
    .year = 2026u,
    .clock_integrity = true,
};

hal_rtc_t s_rtc = nullptr;
bool s_display_ready = false;
uint32_t s_last_poll_ms = 0u;
char s_previous_text[9] = {};
uint16_t s_previous_color = HAL_COLOR_BLACK;

bool datetime_before(const hal_rtc_datetime_t &left,
                     const hal_rtc_datetime_t &right) {
  const uint16_t left_fields[] = {
      left.year, left.month, left.day, left.hour, left.minute, left.second,
  };
  const uint16_t right_fields[] = {
      right.year, right.month,  right.day,
      right.hour, right.minute, right.second,
  };
  for (size_t i = 0u; i < sizeof(left_fields) / sizeof(left_fields[0]); ++i) {
    if (left_fields[i] != right_fields[i]) {
      return left_fields[i] < right_fields[i];
    }
  }
  return false;
}

void render_clock(const char *text, uint16_t color) {
  if (!s_display_ready || text == nullptr) {
    return;
  }

  const int display_width = hal_display_get_width();
  const int display_height = hal_display_get_height();
  const int y = (display_height - kDigitHeight) / 2;
  const int clock_width = 6 * kDigitSlotWidth + 2 * kColonSlotWidth;
  int cursor_x = (display_width - clock_width) / 2;
  const bool redraw_all =
      s_previous_text[0] == '\0' || color != s_previous_color;

  if (redraw_all) {
    (void)hal_display_fill_rect(
        cursor_x, y - (int)kSegmentThickness, clock_width,
        kDigitHeight + 2 * (int)kSegmentThickness, HAL_COLOR_BLACK);
  }

  for (size_t i = 0u; i < 8u; ++i) {
    const bool colon = text[i] == ':';
    const int slot_width = colon ? kColonSlotWidth : kDigitSlotWidth;
    if (redraw_all || text[i] != s_previous_text[i]) {
      if (!redraw_all) {
        /* The renderer gives '1' a proportional width. Erase the previous
         * glyph at its exact drawing position before clearing the cell so no
         * edge pixel can survive a transition between differently sized
         * digits on the ILI9341. */
        const char previous_character[] = {s_previous_text[i], '\0'};
        const int previous_width = get7SegStringWidth(
            previous_character, kDigitWidth, kSegmentThickness);
        draw7SegString(
            previous_character, cursor_x + (slot_width - previous_width) / 2, y,
            kDigitWidth, kDigitHeight, kSegmentThickness, HAL_COLOR_BLACK);
        (void)hal_display_fill_rect(
            cursor_x, y - (int)kSegmentThickness, slot_width,
            kDigitHeight + 2 * (int)kSegmentThickness, HAL_COLOR_BLACK);
      }

      const char character[] = {text[i], '\0'};
      const int character_width =
          get7SegStringWidth(character, kDigitWidth, kSegmentThickness);
      draw7SegString(character, cursor_x + (slot_width - character_width) / 2,
                     y, kDigitWidth, kDigitHeight, kSegmentThickness, color);
    }
    cursor_x += slot_width;
  }
  (void)hal_display_flush();

  std::memcpy(s_previous_text, text, sizeof(s_previous_text));
  s_previous_color = color;
}

bool initialize_display() {
  hal_status_t status = hal_display_init(kTftCsPin, kTftDcPin, kTftRstPin);
  if (status == HAL_OK) {
    status = hal_display_configure_ex(
        kPanelWidth, kPanelHeight, HAL_DISPLAY_ROTATION(90),
        HAL_DISPLAY_INVERT_OFF, HAL_DISPLAY_COLOR_ORDER_RGB);
  }
  if (status != HAL_OK) {
    derr("ILI9341 init failed: %s", hal_status_to_string(status));
    return false;
  }

  s_display_ready = true;
  (void)hal_display_fill_screen(HAL_COLOR_BLACK);
  deb("ILI9341 ready: %dx%d", hal_display_get_width(),
      hal_display_get_height());
  return true;
}

bool initialize_rtc() {
  const hal_rtc_config_t config = {
      .chip = HAL_RTC_CHIP_DS3231,
      .bus = {.i2c = {.sda_pin = kRtcSdaPin,
                      .scl_pin = kRtcSclPin,
                      .clock_hz = HAL_I2C_CLOCK_FAST_HZ,
                      .i2c_bus = 0u,
                      .i2c_addr = HAL_RTC_DS3231_DEFAULT_I2C_ADDR}},
  };
  hal_status_t status = hal_rtc_init_ex(&config, &s_rtc);
  if (status != HAL_OK) {
    derr("DS3231 init failed: %s", hal_status_to_string(status));
    return false;
  }

  hal_rtc_datetime_t value = {};
  status = hal_rtc_get_datetime_ex(s_rtc, &value);
  if (status != HAL_OK) {
    derr("DS3231 initial read failed: %s", hal_status_to_string(status));
    return false;
  }
  if (!value.clock_integrity) {
    derr("DS3231 clock integrity lost; refusing to hide battery failure");
    return false;
  }

  if (datetime_before(value, kInitialTime)) {
    status = hal_rtc_set_datetime_ex(s_rtc, &kInitialTime);
    if (status != HAL_OK) {
      derr("DS3231 initial time write failed: %s",
           hal_status_to_string(status));
      return false;
    }
    deb("DS3231 set to approximately 2026-08-26 22:20:00 CEST");
  } else {
    deb("DS3231 retained time; embedded initial value not applied");
  }

  status = hal_rtc_set_clkout_mode_ex(s_rtc, HAL_RTC_CLKOUT_DISABLED);
  if (status != HAL_OK) {
    derr("DS3231 CLKOUT disable failed: %s", hal_status_to_string(status));
    return false;
  }
  return true;
}

} // namespace

void app_start(void) {
  debugInit();
  hal_serial_set_flush(true);
  deb("=== DS3231 battery-retention clock ===");

  (void)initialize_display();
  (void)hal_i2c_init(kRtcSdaPin, kRtcSclPin, HAL_I2C_CLOCK_FAST_HZ);
  if (!initialize_rtc()) {
    hal_rtc_deinit(s_rtc);
    s_rtc = nullptr;
    render_clock("--:--:--", HAL_COLOR_RED);
  }
}

void app_task0(void) {
  const uint32_t now_ms = hal_millis();
  if ((uint32_t)(now_ms - s_last_poll_ms) < 100u) {
    hal_delay_ms(10u);
    return;
  }
  s_last_poll_ms = now_ms;

  if (s_rtc == nullptr) {
    hal_delay_ms(100u);
    return;
  }

  hal_rtc_datetime_t value = {};
  const hal_status_t status = hal_rtc_get_datetime_ex(s_rtc, &value);
  if (status != HAL_OK || !value.clock_integrity) {
    if (std::strcmp(s_previous_text, "--:--:--") != 0 ||
        s_previous_color != HAL_COLOR_RED) {
      render_clock("--:--:--", HAL_COLOR_RED);
    }
    if (status != HAL_OK) {
      derr("DS3231 read failed: %s", hal_status_to_string(status));
    } else {
      derr("DS3231 clock integrity lost");
    }
    hal_delay_ms(100u);
    return;
  }

  char text[9] = {
      (char)('0' + value.hour / 10u),   (char)('0' + value.hour % 10u),   ':',
      (char)('0' + value.minute / 10u), (char)('0' + value.minute % 10u), ':',
      (char)('0' + value.second / 10u), (char)('0' + value.second % 10u), '\0',
  };
  if (std::strcmp(text, s_previous_text) != 0 ||
      s_previous_color != HAL_COLOR_GREEN) {
    render_clock(text, HAL_COLOR_GREEN);
    deb("DS3231 %04u-%02u-%02u %s integrity=1", (unsigned)value.year,
        (unsigned)value.month, (unsigned)value.day, text);
  }
}
