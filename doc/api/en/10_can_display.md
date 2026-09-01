# CAN bus and display

*Also available in [Polish](../pl/10_can_display.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

Covers: `hal_can`, `hal_hd44780`, `hal_display`.

## `hal_can` - CAN bus  *(optional - `HAL_ENABLE_CAN`, backends `HAL_ENABLE_MCP2515` / `HAL_ENABLE_MCP251XFD` / `HAL_ENABLE_STM32G474_FDCAN`)*

```c
#include <hal/can/hal_can.h>

#define HAL_CAN_MAX_DATA_LEN 8
#define HAL_CAN_FD_MAX_DATA_LEN 64
#define HAL_CAN_DLC_INVALID 0xFFu
#define HAL_CAN_STD_ID_MASK 0x7FFu
#define HAL_CAN_EXT_ID_MASK 0x1FFFFFFFu
#define HAL_CAN_MAX_FILTERS 6u
#define HAL_CAN_NO_INT_PIN   0xFF

// Opaque handle - one per physical CAN controller/backend instance
typedef hal_can_impl_t *hal_can_t;
typedef void (*hal_can_frame_cb_t)(uint32_t id, uint8_t len, const uint8_t *data);

typedef enum {
    HAL_CAN_BACKEND_MCP2515 = 0,
    HAL_CAN_BACKEND_MCP251XFD = 1,
    HAL_CAN_BACKEND_STM32G474_FDCAN = 2
} hal_can_backend_t;

enum {
    HAL_CAN_FRAME_EXTENDED = 0x01u,
    HAL_CAN_FRAME_RTR      = 0x02u,
    HAL_CAN_FRAME_FD       = 0x04u,
    HAL_CAN_FRAME_BRS      = 0x08u,
    HAL_CAN_FRAME_ESI      = 0x10u
};

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t len;
    uint8_t flags;
    uint8_t data[HAL_CAN_FD_MAX_DATA_LEN];
} hal_can_frame_t;

enum {
    HAL_CAN_FILTER_EXTENDED = 0x01u
};

typedef struct {
    uint32_t id;
    uint32_t mask;
    uint8_t flags;
} hal_can_filter_t;

typedef uint32_t hal_can_mode_t;

enum {
    HAL_CAN_MODE_NORMAL      = 0x00u,
    HAL_CAN_MODE_LOOPBACK    = 0x01u,
    HAL_CAN_MODE_LISTEN_ONLY = 0x02u,
    HAL_CAN_MODE_FD          = 0x04u,
    HAL_CAN_MODE_ONE_SHOT    = 0x08u,
    HAL_CAN_MODE_SLEEP       = 0x10u
};

typedef enum {
    HAL_CAN_STATE_ERROR_ACTIVE = 0,
    HAL_CAN_STATE_ERROR_WARNING,
    HAL_CAN_STATE_ERROR_PASSIVE,
    HAL_CAN_STATE_BUS_OFF,
    HAL_CAN_STATE_STOPPED
} hal_can_state_t;

typedef struct {
    uint8_t tx;
    uint8_t rx;
} hal_can_error_counters_t;

typedef struct {
    uint8_t spi_bus;
    uint8_t cs_pin;
    uint32_t bitrate_hz;
    uint32_t oscillator_hz;
    bool one_shot_tx;
    bool sleep_wakeup;
} hal_can_mcp2515_config_t;

typedef struct {
    uint8_t spi_bus;
    uint8_t cs_pin;
    uint32_t arbitration_bitrate_hz;
    uint32_t data_bitrate_hz;
    uint32_t oscillator_hz;
    uint32_t spi_clock_hz;
    bool enable_fd;
    bool one_shot_tx;
    bool sleep_wakeup;
} hal_can_mcp251xfd_config_t;

typedef struct {
    uint8_t rx_pin;
    uint8_t tx_pin;
    uint32_t arbitration_bitrate_hz;
    uint32_t data_bitrate_hz;
    bool enable_fd;
    bool one_shot_tx;
} hal_can_stm32g474_fdcan_config_t;

typedef struct {
    hal_can_backend_t backend;
    union {
        hal_can_mcp2515_config_t mcp2515;
        hal_can_mcp251xfd_config_t mcp251xfd;
        hal_can_stm32g474_fdcan_config_t stm32g474_fdcan;
    };
} hal_can_config_t;

// Default depends on enabled backends. If several are enabled, MCP2515 owns
// the compatibility default, followed by MCP251XFD, then STM32G474 FDCAN.
// MCP2515: SPI bus 0, CS pin 0, 500 kbps / 8 MHz crystal.
// MCP251XFD: SPI bus 0, CS pin 0, 500 kbit/s arbitration, 2 Mbit/s data.
// STM32G474 FDCAN: PA11/PA12, 500 kbit/s arbitration, 2 Mbit/s data.
hal_can_config_t hal_can_default_config(void);

// Create and init a CAN channel from config. NULL uses default config.
// Returns NULL on failure (chip not responding or pool exhausted)
hal_can_t hal_can_create(const hal_can_config_t *cfg);

// Release all resources; handle must not be used after this call
void hal_can_destroy(hal_can_t h);

// Send a CAN frame
bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data);

// Send a CAN/CAN FD frame. MCP2515 accepts only classic CAN frames;
// MCP251XFD and STM32G474 FDCAN accept CAN FD when enable_fd=true.
bool hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame);

// Read the next available frame (returns false if no frame ready)
bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);

// Read the next available CAN/CAN FD frame.
bool hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame);

// Start/stop and controller modes. New handles are started by default.
bool hal_can_start(hal_can_t h);
bool hal_can_stop(hal_can_t h);
bool hal_can_set_mode(hal_can_t h, hal_can_mode_t mode);
bool hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode);

// Controller state and diagnostics.
bool hal_can_get_state(hal_can_t h, hal_can_state_t *state);
bool hal_can_get_error_counters(hal_can_t h,
                                hal_can_error_counters_t *counters);

// Non-blocking check: true if at least one frame is waiting
bool hal_can_available(hal_can_t h);

// Configure hardware RX filters for two accepted standard 11-bit IDs.
// Non-matching IDs are dropped by backends with hardware filter support.
// Returns false if backend mask/filter programming fails.
bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1);

// Configure one acceptance-filter slot with id/mask/flags.
bool hal_can_set_filter(hal_can_t h, uint8_t index,
                        const hal_can_filter_t *filter);

// Retry-friendly create helper with optional IRQ pin setup.
hal_can_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                    uint8_t int_pin,
                                    void (*isr)(void),
                                    int max_retries,
                                    void (*retry_idle)(void));

// Drain pending RX frames and invoke callback for each valid one.
int hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb);

// CAN/CAN FD DLC helpers. bytes_to_dlc() rounds up to the next representable
// CAN FD length and returns HAL_CAN_DLC_INVALID for >64 bytes.
uint8_t hal_can_dlc_to_bytes(uint8_t dlc);
uint8_t hal_can_bytes_to_dlc(uint8_t bytes);
bool hal_can_validate_frame(const hal_can_frame_t *frame);
bool hal_can_validate_filter(const hal_can_filter_t *filter);
bool hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                  const hal_can_filter_t *filter);

// Encode temperature in °C as signed int8 CAN payload byte.
// Truncates toward zero, saturates to [-128, 127], returns two's complement byte.
uint8_t hal_can_encode_temp_i8(float temp_c);
```

- **shared thematic implementation:** Target `hal_can.cpp` files own the CAN facade, handle lifetime,
  mutexing and backend dispatch. MCP2515-specific operations live in
  `hal/can/mcp2515/hal_can_mcp2515.*`, backed by the HAL-only MCP2515
  register/SPI driver in `hal/can/mcp2515/mcp2515_driver.*`. MCP251XFD
  operations live in `hal/can/mcp251xfd/hal_can_mcp251xfd.*`, backed by the
  HAL-only polling register/SPI driver in `hal/can/mcp251xfd/mcp251xfd_driver.*`.
  STM32G474 native FDCAN operations live in
  `impl/stm32g474/hal_can_stm32g474_fdcan.*` and program FDCAN1 registers plus
  the fixed STM32G4 message RAM layout directly.
- **Backend selection:** The CAN API takes `hal_can_config_t`. Enable
  `HAL_ENABLE_MCP2515` for the classic MCP2515 backend or
  `HAL_ENABLE_MCP251XFD` for MCP2517FD/MCP2518FD CAN FD support. Both external
  controller flags pull in the CAN facade plus SPI dependency. Enable
  `HAL_ENABLE_STM32G474_FDCAN` for native FDCAN1 on STM32G474; this flag pulls in
  only the CAN facade and is compile-time rejected on other targets. Plain
  `HAL_ENABLE_CAN` no longer propagates SPI by itself and is treated as a facade
  flag that requires a backend.

**Thread safety:** Thread-safe and multicore-safe. Each channel has a per-instance `hal_mutex_t`. `hal_can_receive()` holds the lock across the availability check and frame read, eliminating TOCTOU races.

- **CAN FD API:** `hal_can_frame_t`, `hal_can_send_frame()`,
  `hal_can_receive_frame()`, and the DLC helpers are backend-agnostic. MCP2515 is
  a classic CAN 2.0 controller, so it rejects frames with `HAL_CAN_FRAME_FD`,
  `HAL_CAN_FRAME_BRS`, or `HAL_CAN_FRAME_ESI`. MCP251XFD accepts CAN FD frames
  when `cfg.mcp251xfd.enable_fd=true`; STM32G474 FDCAN accepts them when
  `cfg.stm32g474_fdcan.enable_fd=true`. Set `HAL_CAN_MODE_FD` for FD/mixed
  operation on FD-capable handles. Use `HAL_CAN_FRAME_EXTENDED` for 29-bit IDs and
  `HAL_CAN_FRAME_RTR` for remote frames.
- **Modes and diagnostics:** New handles are started by default.
  `hal_can_stop()` puts the controller into a non-participating/configuration
  mode and `hal_can_start()` reapplies the stored mode. MCP2515 supports normal,
  loopback, listen-only, sleep and one-shot mode flags. MCP251XFD also supports
  `HAL_CAN_MODE_FD` on FD-capable handles; STM32G474 FDCAN supports FD, loopback,
  listen-only, one-shot and sleep/configuration transitions through CCCR/TEST.
  State/error-counter APIs map backend controller registers into
  `hal_can_state_t` and `hal_can_error_counters_t`.
- **Filters:** `hal_can_set_filter()` programs one id/mask slot. `HAL_CAN_MAX_FILTERS`
  (6) is the *minimum* number of hardware acceptance filters every backend
  guarantees, so it is the portable slot count to rely on. MCP2515 maps them onto
  its six hardware filters; MCP251XFD and STM32G474 FDCAN map them onto the first
  six hardware filter objects routed to RX FIFO 0 (and may have more in hardware). `hal_can_set_std_filters()` remains a
  convenience helper for two exact 11-bit IDs. Programming an MCP2515 filter also
  clears receive-any mode on both hardware RX buffers so unmatched frames are
  rejected before consuming either buffer.
  `hal_can_create_with_retry()` retries init up to `max_retries + 1` attempts and can auto-attach an IRQ handler when `int_pin != HAL_CAN_NO_INT_PIN`.
  `hal_can_process_all()` repeatedly calls `hal_can_receive()` and forwards only frames with `id != 0` and `len > 0`.
  `hal_can_encode_temp_i8()` is a small shared wire-format helper for signed 1-byte temperature fields on CAN frames. It truncates the float input toward zero, saturates to `int8_t` range, and returns the matching two's complement payload byte.

**One-shot TX mode:** `hal_can_create()` enables MCP2515 one-shot mode (`CANCTRL.OSM = 1`) by default
after initialisation. It can be disabled through `cfg.mcp2515.one_shot_tx`. In one-shot mode, when a transmitted frame receives no ACK (e.g. no other node on the bus),
the hardware frees the TX buffer immediately instead of retransmitting indefinitely. This prevents TX buffer
starvation: without one-shot, just 3 consecutive un-ACK'd frames permanently block all 3 TX buffers, making
every subsequent `hal_can_send()` fail with `CAN_GETTXBFTIMEOUT`.

For periodic broadcast applications (where fresh data is sent on the next
timer tick anyway) one-shot has no practical downside - an individual lost frame
is replaced by the next update. Change-only publishers must instead retry a
failed send, add a periodic heartbeat, or disable `one_shot_tx`; otherwise one
lost frame can leave the receiver stale.

When the bus is healthy and all receivers are present, one-shot behaviour is
identical to normal mode: the first attempt succeeds and no retry is needed. In
one-shot mode, a missing ACK, lost arbitration, an aborted transmission, or a
bus error is reported by `hal_can_send()` as `false` and logged via
`hal_derr_limited("can", ...)` to avoid serial flooding. Normal mode continues
hardware retransmission and reports success when a later attempt completes.

---

## `hal_hd44780` - HD44780 character LCD  *(optional - `HAL_ENABLE_HD44780`)*

Parallel character LCD driver for HD44780-compatible modules. It supports the
same 4-bit and 8-bit GPIO transfer modes as the original LiquidCrystal library,
including optional `RW`, custom CGRAM characters, cursor/display control,
scrolling, autoscroll and row-offset overrides.

```cpp
#include <hal/display/hal_hd44780.h>

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

- **shared thematic implementation:** `hal/display/hd44780/hd44780.*`, reused by RP2040,
  STM32G474 and host tests. The driver uses HAL GPIO, `hal_delay_us()` and an
  instance `hal_mutex_t`.
- **Display class scope:** This is a character LCD driver, not the bitmap
  `hal_display` facade. Use `hal_display` for SPI TFT/OLED graphics.
- **Timing:** The init, clear/home, enable-pulse and command-settle delays match
  the proven HD44780 sequence: 50 ms power-on wait, 4.5 ms/150 us init retries,
  2 ms clear/home delay and 1/1/100 us enable pulse phases.

**Thread safety:** Public methods serialize each `HD44780` instance with a HAL
mutex, so multicore and FreeRTOS tasks cannot interleave command/data GPIO
sequences for the same display. Calls are not ISR-safe because `hal_mutex_lock`
is not ISR-safe.

---

## `hal_display` - TFT / OLED / LCD / EPD display  *(optional - `HAL_ENABLE_DISPLAY`)*

Supports SPI TFT displays (ILI9341, ST7789, ST7735, ST7796S, GC9A01),
SSD1331/SSD135x RGB OLEDs, SSD1306-family OLEDs (`SSD1306`, `SSD1309`,
`SSD1315`, `SH1106`, `CH1115`), ST7567 monochrome LCDs and SSD16xx/UC81xx
monochrome e-paper controllers over I2C/SPI/GPIO.

```c
// Define ONE of these before including hal_display.h (or in build flags):
#define HAL_DISPLAY_ILI9341
#define HAL_DISPLAY_ST7789
#define HAL_DISPLAY_ST7735
#define HAL_DISPLAY_ST7796S
#define HAL_DISPLAY_GC9A01

// Optional per-driver excludes:
// #define HAL_ENABLE_ILI9341
// #define HAL_ENABLE_ST7789
// #define HAL_ENABLE_ST7735
// #define HAL_ENABLE_ST7796S
// #define HAL_ENABLE_GC9A01
```

```c
#include <hal/display/hal_display.h>

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

// --- Raw buffer description ---
typedef enum {
    HAL_DISPLAY_PIXEL_FORMAT_NONE = 0u,
    HAL_DISPLAY_PIXEL_FORMAT_MONO01 = (1u << 0),      // 0=black, 1=white
    HAL_DISPLAY_PIXEL_FORMAT_MONO10 = (1u << 1),      // 1=black, 0=white
    HAL_DISPLAY_PIXEL_FORMAT_RGB565_BE = (1u << 2),   // high byte first
    HAL_DISPLAY_PIXEL_FORMAT_RGB565_NATIVE = (1u << 3),
    HAL_DISPLAY_PIXEL_FORMAT_RGB888 = (1u << 4),
    HAL_DISPLAY_PIXEL_FORMAT_BGR888 = (1u << 5),
    HAL_DISPLAY_PIXEL_FORMAT_L8 = (1u << 6),
} hal_display_pixel_format_t;

typedef struct {
    hal_display_pixel_format_t pixel_format;
    uint16_t pitch;           // pixels between consecutive source rows
    uint16_t width;           // rectangle width in pixels
    uint16_t height;          // rectangle height in pixels
    size_t buf_size;          // available source-buffer bytes
    bool frame_incomplete;    // EPD: load RAM now and defer physical refresh
} hal_display_buffer_desc_t;

typedef struct {
    uint16_t width, height;
    uint32_t supported_pixel_formats;
    hal_display_pixel_format_t current_pixel_format;
    uint8_t current_rotation, supported_rotations;
    uint16_t x_alignment, y_alignment;
    uint16_t width_alignment, height_alignment;
    uint32_t screen_info, flags;
} hal_display_capabilities_t;

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
    HAL_DISPLAY_OLED_CONTROLLER_SSD1306 = 0,
    HAL_DISPLAY_OLED_CONTROLLER_SSD1309,
    HAL_DISPLAY_OLED_CONTROLLER_SSD1315,
    HAL_DISPLAY_OLED_CONTROLLER_SH1106,
    HAL_DISPLAY_OLED_CONTROLLER_CH1115,
} hal_display_oled_controller_t;

typedef enum {
    HAL_DISPLAY_OLED_BUS_I2C = 0,
    HAL_DISPLAY_OLED_BUS_SPI,
} hal_display_oled_bus_t;

typedef enum {
    HAL_DISPLAY_OLED_ORIENTATION_NATIVE = 0,
    HAL_DISPLAY_OLED_ORIENTATION_ROTATED_180,
} hal_display_oled_orientation_t;

typedef struct {
    hal_display_oled_controller_t controller;
    hal_display_oled_bus_t bus_type;
    int width, height;
    uint8_t bus, i2c_addr;
    int16_t rst_pin, spi_dc_pin, spi_cs_pin;
    uint8_t switchvcc, spi_mode;
    uint32_t clock_hz;
    uint8_t segment_offset, page_offset, display_offset;
    hal_display_oled_orientation_t orientation;
    bool internal_iref;
    bool periphBegin;
} hal_display_ssd1306_family_config_t;

typedef enum {
    HAL_FONT_DEFAULT = 0,
    HAL_FONT_SANS_BOLD_9PT,
    HAL_FONT_SERIF_9PT,
} hal_font_id_t;

// --- Init / control ---

// Construct display object and start the SPI driver.
// For ILI9341: also calls begin(). For other drivers: init is deferred to configure().
hal_status_t hal_display_init(uint8_t cs, uint8_t dc, uint8_t rst);

// Construct and initialise an SSD1306 OLED connected via I2C.
bool hal_display_init_ssd1306_i2c(int width, int height, uint8_t i2c_addr,
                                  int8_t rst_pin, uint8_t switchvcc,
                                  bool periphBegin);

// Status-returning configurable SSD1306-family init.
hal_status_t hal_display_init_ssd1306_family_ex(
    const hal_display_ssd1306_family_config_t *config);

// Config structs are declared conditionally by the matching HAL_ENABLE flag.
hal_status_t hal_display_init_rgb_oled_ex(
    const hal_display_rgb_oled_config_t *config);
hal_status_t hal_display_init_st7567_ex(
    const hal_display_st7567_config_t *config);
hal_status_t hal_display_init_ssd16xx_ex(
    const hal_display_ssd16xx_config_t *config);
hal_status_t hal_display_init_uc81xx_ex(
    const hal_display_uc81xx_config_t *config);

// Configure dimensions, rotation, colour order. Must be called after init().
bool hal_display_configure(int width, int height, uint8_t rotation, bool invert, bool bgr);

// Re-send backend register-init sequence when the selected driver supports it.
hal_status_t hal_display_soft_init(int delay_ms);
hal_status_t hal_display_suspend_ex(void);
hal_status_t hal_display_resume_ex(void);

bool hal_display_set_rotation(uint8_t r);
bool hal_display_invert(bool invert);
int  hal_display_get_width(void);
int  hal_display_get_height(void);
hal_status_t hal_display_get_capabilities_ex(hal_display_capabilities_t *caps);
hal_status_t hal_display_set_pixel_format_ex(hal_display_pixel_format_t format);
hal_status_t hal_display_write_raw_ex(uint16_t x, uint16_t y,
                                      const hal_display_buffer_desc_t *desc,
                                      const void *buffer);
hal_status_t hal_display_epd_refresh_ex(
    hal_display_epd_refresh_mode_t refresh_mode);

// --- Screen ---
bool hal_display_fill_screen(uint16_t color);
bool hal_display_flush(void); // SSD1306: sends framebuffer; EPD: refreshes pending RAM
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

// --- TFT streaming ---
bool hal_display_begin_write(int x, int y, int w, int h);
bool hal_display_write_pixels_fast(const uint16_t *pixels, size_t count);
bool hal_display_write_pixels_be(const uint8_t *pixels_be, size_t byte_count);
bool hal_display_write_pixels_dma(const uint8_t *pixels_be, size_t byte_count);
bool hal_display_write_pixels_dma_async_start(const uint8_t *pixels_be,
                                              size_t byte_count);
bool hal_display_write_pixels_dma_async_busy(void);
bool hal_display_write_pixels_dma_async_wait(void);
bool hal_display_end_write(void);

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

- **Colors:** RGB565 `uint16_t`. Use predefined constants (`HAL_COLOR_BLACK`, `HAL_COLOR_WHITE`, `HAL_COLOR_RED`, ...)
  or `HAL_COLOR(name)` selector, for example `HAL_COLOR(ORANGE)`.
- **Display mode helpers:** `HAL_DISPLAY_ROTATION_*`, `HAL_DISPLAY_ROTATION(deg)`,
  `HAL_DISPLAY_INVERT_ON/OFF`, `HAL_DISPLAY_COLOR_ORDER_RGB/BGR`.
- **Capabilities and raw buffers:** Query the active backend with
  `hal_display_get_capabilities_ex()`, then use only advertised formats and
  alignments with `hal_display_write_raw_ex()`. `pitch` is in pixels. TFT and
  RGB OLED backends (and the mock) accept `pitch > width`: each source row is
  streamed separately inside one addressing window, so the caller's buffer
  only needs real bytes up to the last row's `width` pixels -- trailing
  padding past that does not need to be backed by memory. Page-tiled or
  per-call-reconfiguring backends still require `pitch == width` and return
  `HAL_EUNSUPPORTED` otherwise: ST7567 always (a byte encodes 8 stacked pixel
  rows, so there is no per-pixel-row boundary), SSD16xx at rotation 0/180
  (tiling flips with rotation), and UC81xx (its write call re-applies the
  panel profile on every invocation, so splitting it per row would replay
  that side effect once per row). ST7567 accepts `MONO01`/`MONO10`, reports
  `HAL_DISPLAY_SCREEN_INFO_MONO_VTILED`, and requires `y` and `height` aligned
  to 8 pixels. Use `hal_display_set_pixel_format_ex()` before changing the
  ST7567 monochrome polarity.

**SSD16xx / UC81xx e-paper:** Enable `HAL_ENABLE_SSD16XX` or
`HAL_ENABLE_UC81XX`; both flags add DISPLAY and SPI. The shared transport uses
SPI plus CS/DC, optional reset and optional BUSY GPIO. A configured BUSY pin is
polled with `busy_timeout_ms`, returning `HAL_ETIMEOUT` instead of blocking
forever. SSD16xx supports rotations 0/90/180/270 and uses vertical 8-pixel
packing at rotations 0/180. UC81xx uses horizontal MSB-first packing and
requires `x` and `width` aligned to 8 pixels. Both accept only `MONO10`.

Set `frame_incomplete=true` when loading one or more areas without an immediate
panel update. Finish the batch with `hal_display_flush_ex()`; deferred writes
use a full refresh so the controller's old/new RAM stays coherent without the
facade retaining caller buffers. With `frame_incomplete=false`, a configured
partial profile is selected before the area write and refresh. The cycle can
also be selected explicitly with
`hal_display_epd_refresh_ex(HAL_DISPLAY_EPD_REFRESH_FULL/PARTIAL)`; requesting
partial refresh for a pending full-refresh batch returns `HAL_ESTATE`, while a
missing partial profile returns `HAL_EUNSUPPORTED`. LUT/profile bytes are
panel-vendor data and their backing arrays must remain valid while the backend
is active; do not reuse a LUT merely because two modules contain the same
controller.

```c
hal_display_ssd16xx_config_t cfg = {0};
cfg.controller = HAL_DISPLAY_SSD16XX_SSD1681;
cfg.transport.bus = 0;
cfg.transport.cs_pin = 17;
cfg.transport.dc_pin = 20;
cfg.transport.rst_pin = 21;
cfg.transport.busy_pin = 22;
cfg.transport.busy_active_high = true;
cfg.transport.busy_timeout_ms = 30000;
cfg.width = 200;
cfg.height = 200;
cfg.rotation = HAL_DISPLAY_ROTATION_0;

hal_status_t status = hal_display_init_ssd16xx_ex(&cfg);
if (status == HAL_OK) {
    hal_display_buffer_desc_t frame = {
        HAL_DISPLAY_PIXEL_FORMAT_MONO10, 200, 200, 200, 5000, false
    };
    status = hal_display_write_raw_ex(0, 0, &frame, framebuffer);
}
```

- **TFT streaming:** Immediate-mode TFT backends support an explicit streaming
  sequence for large contiguous updates:
  `hal_display_begin_write(x, y, w, h)`, one or more pixel writes, then
  `hal_display_end_write()`. `hal_display_write_pixels_fast()` accepts native
  `uint16_t` RGB565 words and swaps them to controller byte order internally;
  `hal_display_write_pixels_be()` accepts already big-endian RGB565 bytes;
  `hal_display_write_pixels_dma()` is the blocking DMA-capable byte-stream helper.
  The asynchronous variant,
  `hal_display_write_pixels_dma_async_start()` / `_busy()` / `_wait()`, maps to
  `hal_spi_write_dma_async_*()` for ILI9341 and ST77xx drivers. When the backend
  is truly asynchronous, keep `pixels_be` valid and keep the display write stream
  open until `_wait()` completes. `hal_display_end_write()` waits for any active
  async pixel DMA before closing the TFT transaction.
- **ST77xx/GC9A01 notes:** `HAL_DISPLAY_ST7735`, `HAL_DISPLAY_ST7789`,
  `HAL_DISPLAY_ST7796S`, and `HAL_DISPLAY_GC9A01` share the ST77xx-style backend.
  `JH_ST77XX_SPI_DEFAULT_HZ` can be overridden before including/building the
  driver to tune the default TFT SPI clock for a board. ST7796S keeps its
  documented BGR-oriented defaults without forcing swapped inversion commands.
  GC9A01 uses the local Zephyr GC9x01x command sequence and defaults to 240x240.
  SSD1331/SSD135x are immediate RGB565 backends and support raw writes, legacy
  streaming and GFX primitives. Their raw/GFX orientation is currently native
  only. ST7567 is intentionally a raw page backend; capabilities do not advertise
  legacy GFX, streaming or DMA for it.
- **impl/rp2040:** Uses the shared HAL display stack. ILI9341 and ST77xx use
  shared HAL SPI/GPIO drivers; SSD1306-family OLEDs use the shared HAL I2C/SPI
  driver; geometry, bitmap, and text rendering run through the shared `jh_gfx`
  engine.
- **impl/stm32g474:** Uses the same shared HAL display stack as RP2040.
- **impl/.mock:** deterministic host mock with inspectable state for tests.

**Thread safety:** Hardware backends serialize display operations with an internal `hal_mutex_t`. During TFT streaming the mutex stays held between `hal_display_begin_write()` and `hal_display_end_write()`, including any async DMA wait. Mock backend is unsynchronized and intended for single-threaded tests.

**Mock helpers:**
```c
void         hal_mock_display_reset(void);
void         hal_mock_display_fail_next_io(void);
const char  *hal_mock_display_last_print(void);
const char  *hal_mock_display_last_println(void);
hal_font_id_t hal_mock_display_get_font(void);
uint16_t     hal_mock_display_get_text_color(void);
uint8_t      hal_mock_display_get_text_size(void);
void         hal_mock_display_get_cursor(int *x, int *y);
void         hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h, uint16_t *color);
void         hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w, int *h);
```

**Status-first API:** status validation and error mapping live in the mock and
shared hardware backends. Historical `bool` functions are thin compatibility
wrappers over status-returning `_ex` operations. Historical `void` init and
soft-init now return `hal_status_t` in place; existing callers can keep ignoring
the result. Value-returning getters retain their original signature and expose
typed errors through `_ex` variants with an output parameter.

The backend can report `HAL_EINVAL`, `HAL_EUNINIT`, `HAL_EUNSUPPORTED`,
`HAL_ESTATE`, `HAL_EBUSY`, `HAL_EOVERFLOW` and `HAL_EIO` without collapsing
them through a legacy boolean result.

```c
// Configure + draw with typed diagnostics
hal_status_t st = hal_display_configure_ex(240, 320, 0, false, false);
// HAL_EINVAL -> bad width/height, HAL_EIO -> backend init failed

st = hal_display_fill_rect_ex(0, 0, 240, 40, HAL_COLOR(BLUE));
// HAL_EINVAL -> non-positive width/height, HAL_EUNINIT -> not configured yet

int width = 0;
if (hal_display_get_width_ex(&width) == HAL_OK) {
    // width valid only when the call returned HAL_OK
}

// Streaming distinguishes an absent stream from an already-open one.
if (hal_display_begin_write_ex(0, 0, 240, 320) == HAL_OK) {
    hal_display_write_pixels_be_ex(pixels_be, byte_count); // HAL_EINVAL on odd count
    hal_display_end_write_ex();
}
```

Naming note: because `hal_display_init_ssd1306_i2c_ex()` already exists (the
bus-selecting initialiser), the SSD1306 status entry point is
`hal_display_init_ssd1306_i2c_status_ex()`. For new OLED-family work prefer
`hal_display_init_ssd1306_family_ex()`: it selects the controller, I2C/SPI
transport, segment/page/display offsets, hardware orientation and variant
current-reference behavior in one status-returning config struct. `HAL_ENABLE_SSD1306`
still auto-enables I2C for the historical helper; SPI OLED transport also
requires `HAL_ENABLE_SPI`.

---


---

*Next: [Sensors](11_sensors.md)*
