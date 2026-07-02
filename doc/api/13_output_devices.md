# Output devices - RGB LED, PGA2311, MFRC522, PN532, math helpers

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

Covers: `hal_rgb_led`, `hal_pga2311`, `hal_mfrc522`, `hal_pn532`, `hal_math`.

## `hal_math` - Lightweight numeric helpers

`hal_math.h` provides platform-independent helpers usable from both C and C++.

```c
#include <hal/hal_math.h>

#define hal_constrain(v, lo, hi) ...
#define hal_map(x, in_min, in_max, out_min, out_max) ...

static inline float roundToN(float v, int n);
/* Backward-compatible alias: hal_roundToN(v, n) */
```

`roundToN` rounds to `n` decimal places (`n < 0` -> `0`, `n > 6` -> `6`).
Half values are rounded away from zero.

---

## `hal_rgb_led` - NeoPixel status LED  *(optional - `HAL_ENABLE_RGB_LED`)*

```c
#include <hal/hal_rgb_led.h>

typedef enum {
    HAL_RGB_LED_NONE   = 0,
    HAL_RGB_LED_RED    = 1,
    HAL_RGB_LED_GREEN  = 2,
    HAL_RGB_LED_YELLOW = 3,
    HAL_RGB_LED_WHITE  = 4,
    HAL_RGB_LED_BLUE   = 5,
    HAL_RGB_LED_PURPLE = 6,
} hal_rgb_led_color_t;

typedef enum {
    HAL_RGB_LED_PIXEL_RGB_KHZ800  = 0x0006,  // RGB byte order, 800 kHz
    HAL_RGB_LED_PIXEL_GRB_KHZ800  = 0x0052,  // GRB byte order, 800 kHz (WS2812B, RP2040-Zero)
    HAL_RGB_LED_PIXEL_RGBW_KHZ800 = 0x0018,  // RGBW byte order, 800 kHz
} hal_rgb_led_pixel_type_t;

// Init with default RGB byte order
void hal_rgb_led_init(uint8_t pin, uint8_t num_pixels);

// Init with explicit pixel type (use HAL_RGB_LED_PIXEL_GRB_KHZ800 for WS2812B)
void hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels, hal_rgb_led_pixel_type_t pixel_type);

// Set brightness [1, 255]; default is 30. Takes effect on next set_color() call.
void hal_rgb_led_set_brightness(uint8_t brightness);

// Set colour. Repeated calls with the same colour are suppressed (no LED transport traffic).
void hal_rgb_led_set_color(hal_rgb_led_color_t color);

// Turn LED off (equivalent to set_color(HAL_RGB_LED_NONE))
void hal_rgb_led_off(void);
```

**impl/rp2040:** shared `impl/shared/drivers/neopixel/jh_neopixel.*` core + RP2040 PIO transport (`impl/shared/drivers/neopixel/rp2040_pio.h`).
**impl/stm32g474:** shared `impl/shared/drivers/neopixel/jh_neopixel.*` core + cycle-timed GPIO transport in `impl/stm32g474/hal_rgb_led.cpp`.
**impl/.mock:** records init parameters, pixel type, brightness and last colour; injectable via mock helpers.
**Thread safety:** RP2040 and STM32G474 backends are thread-safe for HAL calls. A HAL mutex serializes singleton strip state and transport access. Mock backend is unsynchronized and intended for single-threaded tests.

**Mock helpers:**
```c
bool                hal_mock_rgb_led_is_initialized(void);
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void);
uint8_t             hal_mock_rgb_led_get_brightness(void);
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void);
uint8_t             hal_mock_rgb_led_get_pin(void);
uint8_t             hal_mock_rgb_led_get_num_pixels(void);
void                hal_mock_rgb_led_reset(void);
```

---


## `hal_pga2311` - PGA2311 stereo volume controller  *(optional - `HAL_ENABLE_PGA2311`)*

```c
#include <hal/hal_pga2311.h>

#define HAL_PGA2311_PIN_NONE            0xFFu
#define HAL_PGA2311_MUTE_PIN_NONE       HAL_PGA2311_PIN_NONE
#define HAL_PGA2311_SPI_DEFAULT_HZ      1000000UL

#define HAL_PGA2311_CODE_MUTE           0x00u
#define HAL_PGA2311_CODE_MIN            0x01u
#define HAL_PGA2311_CODE_0DB            0xC0u
#define HAL_PGA2311_CODE_MAX            0xFFu

#define HAL_PGA2311_GAIN_HALF_DB_MIN   (-191)
#define HAL_PGA2311_GAIN_HALF_DB_MAX   (63)

#define HAL_PGA2311_GAIN_DB_MIN        (-95.5f)
#define HAL_PGA2311_GAIN_DB_MAX        (31.5f)

typedef enum {
  HAL_PGA2311_MUTE_ACTIVE_LOW = 0,
  HAL_PGA2311_MUTE_ACTIVE_HIGH = 1,
} hal_pga2311_mute_polarity_t;

typedef struct {
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint8_t mute_pin;
  hal_pga2311_mute_polarity_t mute_polarity;
  uint32_t spi_clock_hz;
  uint8_t spi_bit_order;
  uint8_t spi_mode;
  bool start_muted;
} hal_pga2311_config_t;

typedef struct hal_pga2311_impl_s hal_pga2311_impl_t;
typedef hal_pga2311_impl_t *hal_pga2311_t;

hal_pga2311_config_t hal_pga2311_default_config(void);
hal_pga2311_t hal_pga2311_init(const hal_pga2311_config_t *cfg);
void hal_pga2311_deinit(hal_pga2311_t h);

bool hal_pga2311_set_raw(hal_pga2311_t h, uint8_t left_code, uint8_t right_code);
bool hal_pga2311_set_raw_both(hal_pga2311_t h, uint8_t code);
bool hal_pga2311_set_gain_half_db(hal_pga2311_t h, int16_t left_half_db, int16_t right_half_db);
bool hal_pga2311_set_gain_db(hal_pga2311_t h, float left_db, float right_db);
bool hal_pga2311_set_gain_db_both(hal_pga2311_t h, float db);

bool hal_pga2311_set_mute(hal_pga2311_t h, bool mute);
bool hal_pga2311_is_muted(hal_pga2311_t h);

bool hal_pga2311_get_target_raw(hal_pga2311_t h, uint8_t *left_code, uint8_t *right_code);
bool hal_pga2311_get_target_gain_half_db(hal_pga2311_t h, int16_t *left_half_db, int16_t *right_half_db);

bool hal_pga2311_gain_half_db_to_raw(int16_t half_db, uint8_t *out_code);
bool hal_pga2311_raw_to_gain_half_db(uint8_t code, int16_t *out_half_db);
```

**Behavior notes:**
- `HAL_ENABLE_PGA2311` auto-propagates `HAL_ENABLE_SPI` in `hal_config.h`.
- The module does not call `hal_spi_init()`; the application owns SPI bus pin setup.
- With `mute_pin == HAL_PGA2311_MUTE_PIN_NONE`, mute is emulated in software by
  writing `HAL_PGA2311_CODE_MUTE` to both channels and restoring cached target
  codes on unmute.
- With a hardware mute pin configured, mute toggles only GPIO and does not send
  extra SPI frames.

**impl/shared:** `impl/shared/drivers/pga2311/pga2311_driver.*` (HAL SPI/GPIO transport)
plus `hal_pga2311.cpp` facade with static handle pool + per-instance mutex.

**Thread safety:** per-instance mutex serializes API calls; SPI transactions are
wrapped in `hal_spi_lock()` / `hal_spi_unlock()`.

---

## `hal_mfrc522` - MFRC522 RFID reader  *(optional - `HAL_ENABLE_MFRC522`)*

```cpp
#include <hal/hal_mfrc522.h>

hal_spi_init(0, miso_pin, mosi_pin, sck_pin);

MFRC522_SPI bus(cs_pin, rst_pin, 0 /* SPI bus */);
MFRC522 rfid(&bus);
rfid.PCD_Init();

byte version = rfid.PCD_GetVersion();
if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
  MFRC522::PICC_Type type = MFRC522::PICC_GetType(rfid.uid.sak);
  const char *name = MFRC522::PICC_GetTypeName(type);
}
```

`MFRC522_SPI` uses HAL SPI transactions and chip-select GPIO control.
`MFRC522_I2C` uses HAL I2C write/read transactions. The application still owns
bus pin setup with `hal_spi_init()` or `hal_i2c_init_bus()`.

The port preserves the MFRC522 protocol logic from the
MFRC522-spi-i2c-uart-async / Miguel Balboa driver lineage while replacing
transport and timing calls with JaszczurHAL primitives. `StatusCodeToHalStatus()`
maps driver-local outcomes to shared `hal_status_t` values.

**Thread safety:** SPI and I2C register transactions use HAL bus locks. The
driver allocates a per-instance HAL mutex for future broader sequencing; create
and destroy remain single-owner lifecycle operations.

Example: `examples/46_mfrc522_rfid`.

---

## `hal_pn532` - PN532 NFC/RFID reader  *(optional - `HAL_ENABLE_PN532`)*

```cpp
#include <hal/hal_pn532.h>

hal_spi_init(0, miso_pin, mosi_pin, sck_pin);

PN532_SPI bus(cs_pin, rst_pin, 0 /* SPI bus */);
PN532 nfc(&bus);
nfc.begin();

uint32_t firmware = 0;
if (nfc.getFirmwareVersion(&firmware) == HAL_OK) {
  nfc.SAMConfig();
}
```

`PN532_SPI` uses HAL SPI transactions and chip-select GPIO control.
`PN532_I2C` is available when `HAL_ENABLE_I2C` is enabled and uses HAL I2C
direct read/write transactions with the PN532 ready byte. `PN532_UART` is
available when `HAL_ENABLE_UART` is enabled and uses timeout-based reads on the
HAL UART API. The application owns bus pin setup with `hal_spi_init()`,
`hal_i2c_init_bus()` or `hal_uart_create()`/`hal_uart_begin()`.

The port preserves the Adafruit_PN532 frame construction, ACK handling,
firmware query, SAM configuration, passive target scan and core MIFARE
exchange helpers while replacing transport and timing calls with JaszczurHAL
primitives. Public PN532 operations return `hal_status_t`.

**Thread safety:** public PN532 operations are serialized with a per-instance
HAL mutex created through `jh_hal_mutex_create_once()`. SPI and I2C transports
also use HAL bus locks for physical transactions. Create and destroy remain
single-owner lifecycle operations.

Example: `examples/47_pn532_nfc`.

---

*Next: [Storage](14_storage.md)*
