# CAN bus and display

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_can`, `hal_hd44780`, `hal_display`.

## `hal_can` - CAN bus  *(optional - `HAL_ENABLE_CAN`)*

```c
#include <hal/hal_can.h>

#define HAL_CAN_MAX_DATA_LEN 8
#define HAL_CAN_NO_INT_PIN   0xFF

// Opaque handle - one per MCP2515 chip (CS pin)
typedef hal_can_impl_t *hal_can_t;
typedef void (*hal_can_frame_cb_t)(uint32_t id, uint8_t len, const uint8_t *data);

// Create and init a CAN channel at 500 kbps / 8 MHz crystal
// Returns NULL on failure (chip not responding or pool exhausted)
hal_can_t hal_can_create(uint8_t cs_pin);

// Release all resources; handle must not be used after this call
void hal_can_destroy(hal_can_t h);

// Send a CAN frame
bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data);

// Read the next available frame (returns false if no frame ready)
bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);

// Non-blocking check: true if at least one frame is waiting
bool hal_can_available(hal_can_t h);

// Configure hardware RX filters for two accepted standard 11-bit IDs.
// Non-matching IDs are dropped by MCP2515 before entering RX buffers.
// Returns false if MCP2515 mask/filter programming fails.
bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1);

// Retry-friendly create helper with optional IRQ pin setup.
hal_can_t hal_can_create_with_retry(uint8_t cs_pin,
                                    uint8_t int_pin,
                                    void (*isr)(void),
                                    int max_retries,
                                    void (*retry_idle)(void));

// Drain pending RX frames and invoke callback for each valid one.
int hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb);

// Encode temperature in °C as signed int8 CAN payload byte.
// Truncates toward zero, saturates to [-128, 127], returns two's complement byte.
uint8_t hal_can_encode_temp_i8(float temp_c);
```

**impl/shared:** Arduino-free MCP2515 driver in `impl/shared/mcp2515/mcp2515_driver.*`, reused by RP2040 and STM32G474 wrappers.
**Thread safety:** Thread-safe and multicore-safe. Each channel has a per-instance `hal_mutex_t`. `hal_can_receive()` holds the lock across the availability check and frame read, eliminating TOCTOU races.
`hal_can_create_with_retry()` retries init up to `max_retries + 1` attempts and can auto-attach an IRQ handler when `int_pin != HAL_CAN_NO_INT_PIN`.
`hal_can_process_all()` repeatedly calls `hal_can_receive()` and forwards only frames with `id != 0` and `len > 0`.
`hal_can_encode_temp_i8()` is a small shared wire-format helper for signed 1-byte temperature fields on CAN frames. It truncates the float input toward zero, saturates to `int8_t` range, and returns the matching two's complement payload byte.

**One-shot TX mode:** `hal_can_create()` enables MCP2515 one-shot mode (`CANCTRL.OSM = 1`) immediately after
initialisation. In one-shot mode, when a transmitted frame receives no ACK (e.g. no other node on the bus),
the hardware frees the TX buffer immediately instead of retransmitting indefinitely. This prevents TX buffer
starvation: without one-shot, just 3 consecutive un-ACK'd frames permanently block all 3 TX buffers, making
every subsequent `hal_can_send()` fail with `CAN_GETTXBFTIMEOUT`. For periodic broadcast applications (where
fresh data is sent on the next timer tick anyway) one-shot has no practical downside - an individual lost frame
is transparent to the receiver. When the bus is healthy and all receivers are present, one-shot behaviour is
identical to normal mode: the first attempt succeeds and no retry is needed. `hal_can_send()` failure due to
missing ACK is logged via `hal_derr_limited("can", ...)` to avoid serial flooding.

---

## `hal_hd44780` - HD44780 character LCD  *(optional - `HAL_ENABLE_HD44780`)*

Parallel character LCD driver for HD44780-compatible modules. It supports the
same 4-bit and 8-bit GPIO transfer modes as the original LiquidCrystal library,
including optional `RW`, custom CGRAM characters, cursor/display control,
scrolling, autoscroll and row-offset overrides.

```cpp
#include <hal/hal_hd44780.h>

// 4-bit mode, RW tied to GND:
HD44780 lcd(rs_pin, enable_pin, d4_pin, d5_pin, d6_pin, d7_pin);

// 4-bit mode with RW pin:
HD44780 lcd_rw(rs_pin, rw_pin, enable_pin, d4_pin, d5_pin, d6_pin, d7_pin);

// 8-bit mode:
HD44780 lcd8(rs_pin, enable_pin,
             d0_pin, d1_pin, d2_pin, d3_pin,
             d4_pin, d5_pin, d6_pin, d7_pin);

lcd.begin(16, 2);
lcd.clear();
lcd.print("JaszczurHAL");
lcd.setCursor(0, 1);
lcd.print(hal_millis() / 1000u);

uint8_t glyph[8] = {0x00, 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x00};
lcd.createChar(0, glyph);
lcd.write((uint8_t)0);
```

**impl/shared:** `impl/shared/hd44780/hd44780.*`, reused by RP2040,
STM32G474 and host tests. The driver uses HAL GPIO, `hal_delay_us()` and an
instance `hal_mutex_t`.
**Display class scope:** This is a character LCD driver, not the bitmap
`hal_display` facade. Use `hal_display` for SPI TFT/OLED graphics.
**Timing:** The init, clear/home, enable-pulse and command-settle delays match
the proven HD44780 sequence: 50 ms power-on wait, 4.5 ms/150 us init retries,
2 ms clear/home delay and 1/1/100 us enable pulse phases.
**Thread safety:** Public methods serialize each `HD44780` instance with a HAL
mutex, so multicore and FreeRTOS tasks cannot interleave command/data GPIO
sequences for the same display. Calls are not ISR-safe because `hal_mutex_lock`
is not ISR-safe.

---

## `hal_display` - TFT / OLED display  *(optional - `HAL_ENABLE_DISPLAY`)*

Supports SPI TFT displays (ILI9341, ST7789, ST7735, ST7796S) and SSD1306 OLED over I2C.

```c
// Define ONE of these before including hal_display.h (or in build flags):
#define HAL_DISPLAY_ILI9341
#define HAL_DISPLAY_ST7789
#define HAL_DISPLAY_ST7735
#define HAL_DISPLAY_ST7796S

// Optional per-driver excludes:
// #define HAL_ENABLE_ILI9341
// #define HAL_ENABLE_ST7789
// #define HAL_ENABLE_ST7735
// #define HAL_ENABLE_ST7796S
```

```c
#include <hal/hal_display.h>

// --- Common RGB565 colors ---
#define HAL_COLOR_BLACK   0x0000
#define HAL_COLOR_WHITE   0xFFFF
#define HAL_COLOR_RED     0xF800
#define HAL_COLOR_GREEN   0x07E0
#define HAL_COLOR_BLUE    0x001F
#define HAL_COLOR_ORANGE  0xFD20
#define HAL_COLOR_PURPLE  0x780F
#define HAL_COLOR_YELLOW  0xFFE0
#define HAL_COLOR_CYAN    0x07FF

// Helper selector: HAL_COLOR(RED) -> HAL_COLOR_RED
#define HAL_COLOR(name) HAL_COLOR_##name

// --- Display orientation / mode helpers ---
typedef enum {
    HAL_DISPLAY_ROTATION_0   = 0,
    HAL_DISPLAY_ROTATION_90  = 1,
    HAL_DISPLAY_ROTATION_180 = 2,
    HAL_DISPLAY_ROTATION_270 = 3,
} hal_display_rotation_t;

#define HAL_DISPLAY_ROTATION(deg) \
    ((uint8_t)( \
        ((deg) == 0)   ? HAL_DISPLAY_ROTATION_0 : \
        ((deg) == 90)  ? HAL_DISPLAY_ROTATION_90 : \
        ((deg) == 180) ? HAL_DISPLAY_ROTATION_180 : \
        ((deg) == 270) ? HAL_DISPLAY_ROTATION_270 : \
                        HAL_DISPLAY_ROTATION_0))

#define HAL_DISPLAY_INVERT_OFF false
#define HAL_DISPLAY_INVERT_ON  true
#define HAL_DISPLAY_COLOR_ORDER_RGB false
#define HAL_DISPLAY_COLOR_ORDER_BGR true

// SSD1306 power mode
#define HAL_DISPLAY_VCC_EXTERNAL  0x01
#define HAL_DISPLAY_VCC_SWITCHCAP 0x02

typedef enum {
    HAL_FONT_DEFAULT = 0,
    HAL_FONT_SANS_BOLD_9PT,
    HAL_FONT_SERIF_9PT,
} hal_font_id_t;

// --- Init / control ---

// Construct display object and start the SPI driver.
// For ILI9341: also calls begin(). For other drivers: init is deferred to configure().
void hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst);

// Construct and initialise an SSD1306 OLED connected via I2C.
bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periphBegin);

// Configure dimensions, rotation, colour order. Must be called after init().
bool hal_display_configure(int width, int height, uint8_t rotation, bool invert, bool bgr);

// Re-send backend register-init sequence when the selected driver supports it.
void hal_display_soft_init(int delay_ms);

bool hal_display_set_rotation(uint8_t r);
bool hal_display_invert(bool invert);
int  hal_display_get_width(void);
int  hal_display_get_height(void);

// --- Screen ---
bool hal_display_fill_screen(uint16_t color);
bool hal_display_flush(void);               // SSD1306: sends framebuffer; TFT: no-op
bool hal_display_draw_image(int x, int y, int w, int h, uint16_t background, uint16_t *data);

// --- Geometry ---
bool hal_display_fill_rect(int x, int y, int w, int h, uint16_t color);
bool hal_display_draw_rect(int x, int y, int w, int h, uint16_t color);
bool hal_display_fill_circle(int x, int y, int r, uint16_t color);
bool hal_display_draw_circle(int x, int y, int r, uint16_t color);
bool hal_display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);
bool hal_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

// --- Bitmap ---
bool hal_display_draw_rgb_bitmap(int x, int y, uint16_t *data, int w, int h);

// --- Text ---
bool hal_display_set_font(hal_font_id_t font);
bool hal_display_set_text_color(uint16_t color);
bool hal_display_set_text_size(uint8_t size);
bool hal_display_set_cursor(int x, int y);
bool hal_display_print(const char *s);
bool hal_display_println(const char *s);
bool hal_display_print_at(int x, int y, const char *s);
bool hal_display_get_text_bounds(const char *s, int *w, int *h);
int  hal_display_text_width(const char *text);
int  hal_display_text_height(const char *text);

// --- Text-line helpers ---
bool hal_display_clear_text_line(int line_index, int line_height, uint16_t bg_color);
bool hal_display_print_line(int line_index, int line_height, const char *text,
                            bool clear_first, uint16_t fg_color, uint16_t bg_color);
bool hal_display_draw_text_centered(const char *text, uint16_t fg_color,
                                    uint16_t bg_color, bool clear_first,
                                    bool flush_after);

// --- Font / style presets ---
bool hal_display_println_prepared_text(char *text);
bool hal_display_set_default_font(void);
bool hal_display_set_default_font_with_pos_and_color(int x, int y, uint16_t color);
bool hal_display_set_text_size_one_with_color(uint16_t color);
bool hal_display_set_sans_bold_with_pos_and_color(int x, int y, uint16_t color);
bool hal_display_set_serif9pt_with_color(uint16_t color);

// --- Formatted text ---
int  hal_display_prepare_text(char *display_txt, size_t display_txt_size,
                              const char *format, ...);
int  hal_display_prepare_text_v(char *display_txt, size_t display_txt_size,
                                const char *format, va_list args);
```

**Colors:** RGB565 `uint16_t`. Use predefined constants (`HAL_COLOR_BLACK`, `HAL_COLOR_WHITE`, `HAL_COLOR_RED`, ...)
or `HAL_COLOR(name)` selector, for example `HAL_COLOR(ORANGE)`.
**Display mode helpers:** `HAL_DISPLAY_ROTATION_*`, `HAL_DISPLAY_ROTATION(deg)`,
`HAL_DISPLAY_INVERT_ON/OFF`, `HAL_DISPLAY_COLOR_ORDER_RGB/BGR`.
**impl/arduino:** Adafruit driver selected by compile-time define + `Adafruit_GFX` + `Adafruit_SSD1306`. ILI9341 and ST77xx soft-init paths use shared init command tables. Fonts: `FreeSansBold9pt7b`, `FreeSerif9pt7b`.
**impl/stm32g474:** Uses the same shared HAL display stack as RP2040. ILI9341 and ST77xx use shared HAL SPI/GPIO drivers; SSD1306 uses the shared HAL I2C driver; geometry, bitmap, and text rendering run through the shared `jh_gfx` engine.
**impl/.mock:** deterministic host mock with inspectable state for tests.
**Thread safety:** Arduino and STM32G474 backends serialize display operations with an internal `hal_mutex_t`. Mock backend is unsynchronized and intended for single-threaded tests.

**Mock helpers:**
```c
void         hal_mock_display_reset(void);
const char  *hal_mock_display_last_print(void);
const char  *hal_mock_display_last_println(void);
hal_font_id_t hal_mock_display_get_font(void);
uint16_t     hal_mock_display_get_text_color(void);
uint8_t      hal_mock_display_get_text_size(void);
void         hal_mock_display_get_cursor(int *x, int *y);
void         hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h, uint16_t *color);
void         hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w, int *h);
```

---


---

*Next: [Sensors](11_sensors.md)*
