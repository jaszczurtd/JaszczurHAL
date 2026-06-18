#include <hal/hal_app.h>
#include <hal/hal_display.h>
#include <hal/hal_system.h>
#include <tools.h>
#include <utils/draw7Segment.h>

static const uint8_t TFT_CS_PIN = 17;
static const uint8_t TFT_DC_PIN = 20;
static const uint8_t TFT_RST_PIN = 21;

static const int DISPLAY_WIDTH = 240;
static const int DISPLAY_HEIGHT = 320;

static uint32_t last_update_ms = 0;
static uint16_t counter = 0;

static void drawStaticLayout(void) {
  hal_display_fill_screen(HAL_COLOR_BLACK);

  hal_display_fill_rect(0, 0, DISPLAY_WIDTH, 34, HAL_COLOR_BLUE);
  hal_display_set_default_font_with_pos_and_color(10, 12, HAL_COLOR_WHITE);
  hal_display_print("JaszczurHAL TFT");

  hal_display_draw_rect(8, 46, DISPLAY_WIDTH - 16, 96, HAL_COLOR_CYAN);
  hal_display_fill_round_rect(18, 58, 92, 68, 8, HAL_COLOR_GREEN);
  hal_display_fill_circle(168, 92, 28, HAL_COLOR_ORANGE);
  hal_display_draw_circle(168, 92, 36, HAL_COLOR_YELLOW);

  for (int i = 0; i < 6; ++i) {
    const int y = 158 + i * 12;
    const uint16_t color = (i % 2 == 0) ? HAL_COLOR_PURPLE : HAL_COLOR_CYAN;
    hal_display_draw_line(12, y, DISPLAY_WIDTH - 12, y + 24, color);
  }

  hal_display_set_default_font_with_pos_and_color(12, 244, HAL_COLOR_WHITE);
  hal_display_print("Counter");
  hal_display_draw_rect(8, 260, DISPLAY_WIDTH - 16, 50, HAL_COLOR_WHITE);
}

static void drawCounter(uint16_t value) {
  char value_text[8] = {0};
  hal_display_prepare_text(value_text, sizeof(value_text), "%03u",
                           value % 1000u);

  hal_display_fill_rect(14, 266, DISPLAY_WIDTH - 28, 38, HAL_COLOR_BLACK);
  draw7SegString(value_text, 26, 272, 36, 28, 4.0f, HAL_COLOR_RED);
}

void app_start(void) {
  debugInit();

  hal_display_init(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
  if (!hal_display_configure(DISPLAY_WIDTH, DISPLAY_HEIGHT,
                             HAL_DISPLAY_ROTATION(0), HAL_DISPLAY_INVERT_OFF,
                             HAL_DISPLAY_COLOR_ORDER_RGB)) {
    derr("display configure failed");
    return;
  }

  drawStaticLayout();
  drawCounter(counter);
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if (now - last_update_ms < 500u) {
    return;
  }
  last_update_ms = now;

  drawCounter(counter++);
}
