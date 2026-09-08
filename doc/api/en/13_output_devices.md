<a id="output-devices---rgb-led-digipot-pga2311-simple-io-chips-mfrc522-pn532-math-helpers"></a>

# LEDs, controls, I/O devices, and RFID/NFC readers

*Also available in [Polish](../pl/13_output_devices.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

This chapter covers NeoPixel LEDs, digital potentiometers, volume control, GPIO expanders, a DAC, and RFID/NFC readers. It also describes the `hal_math` numeric functions.

<a id="hal_math---lightweight-numeric-helpers"></a>

## `hal_math` - numeric functions

`hal_math.h` provides portable numeric functions for C and C++.

```c
#include <hal/core/hal_math.h>

#define hal_constrain(v, lo, hi) ...
#define hal_map(x, in_min, in_max, out_min, out_max) ...

static inline float hal_math_round_to_n(float v, int n);
```

`hal_math_round_to_n` rounds to `n` decimal places (`n < 0` -> `0`, `n > 6` -> `6`).
Half values are rounded away from zero.

---

<a id="hal_rgb_led---neopixel-status-led--optional---hal_enable_rgb_led"></a>

## `hal_rgb_led` - NeoPixel LEDs  *(optional - `HAL_ENABLE_RGB_LED`)*

Set NeoPixel colors and brightness, or turn the LEDs off. The initialization configuration selects the pixel format.

```c
#include <hal/gpio/hal_rgb_led.h>

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
hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels);

// Init with explicit pixel type (use HAL_RGB_LED_PIXEL_GRB_KHZ800 for WS2812B)
hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type);

// Set brightness [1, 255]; default is 30. Takes effect on next set_color() call.
void hal_rgb_led_set_brightness(uint8_t brightness);

// Set colour. Repeated calls with the same colour are suppressed (no LED transport traffic).
hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color);

// Turn LED off (equivalent to set_color(HAL_RGB_LED_NONE))
hal_status_t hal_rgb_led_off(void);
```

The historically `void` init, colour and off operations now return status in
place. Existing callers may continue to ignore the result. Invalid pixel
counts/types or colours return `HAL_EINVAL`, colour writes before init return
`HAL_EUNINIT`, allocation/resource failures return `HAL_ENOMEM`, and transport
failures return `HAL_EIO`. `hal_rgb_led_init_ex()` keeps its historical name
because `_ex` already denotes the explicit pixel-type variant.

- **impl/rp2040:** shared `hal/gpio/neopixel/jh_neopixel.*` core + RP2040 PIO transport (`hal/gpio/neopixel/rp2040_pio.h`).
- **impl/stm32g474:** shared `hal/gpio/neopixel/jh_neopixel.*` core + cycle-timed GPIO transport in `impl/stm32g474/hal_rgb_led.cpp`.
- **impl/esp32:** shared NeoPixel core + ESP-IDF RMT TX channel and bytes encoder.
  The transport supports the public 800 kHz pixel formats, waits for queued
  transmission completion, and applies the reset/latch interval before returning.
  Reinitialization disables and deletes the previous RMT channel before deleting
  its encoder. A failed delete keeps the corresponding live handle for the next
  teardown attempt; handles are cleared only after ESP-IDF confirms deletion.
- **impl/.mock:** records init parameters, pixel type, brightness and last colour; injectable via mock helpers.

**Thread safety:** RP2040, STM32G474, and ESP32-S3 backends are thread-safe for
HAL calls. A HAL mutex serializes singleton strip state and transport access.
Mock backend is unsynchronized and intended for single-threaded tests.

**Mock helpers:**
```c
bool                hal_mock_rgb_led_is_initialized(void);
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void);
uint8_t             hal_mock_rgb_led_get_brightness(void);
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void);
uint8_t             hal_mock_rgb_led_get_pin(void);
uint8_t             hal_mock_rgb_led_get_num_pixels(void);
void                hal_mock_rgb_led_reset(void);
void                hal_mock_rgb_led_fail_next_init(bool fail);
void                hal_mock_rgb_led_fail_next_write(bool fail);
```

---


<a id="hal_digipot---i2c-digital-potentiometers--optional---hal_enable_digipot"></a>

## `hal_digipot` - digital potentiometers  *(optional - `HAL_ENABLE_DIGIPOT`)*

Set the resistance of MCP401x and MAX5395 potentiometers over I2C. The `_ex` variants distinguish invalid parameters from communication failures.

```c
#include <hal/analog/hal_digipot.h>

hal_status_t hal_digipot_init_ex(const hal_digipot_config_t *cfg,
                                 hal_digipot_t *out);
hal_digipot_t hal_digipot_init(const hal_digipot_config_t *cfg);

hal_status_t hal_digipot_set_resistance_ex(hal_digipot_t h, uint32_t ohms);
bool hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms);

void hal_digipot_deinit(hal_digipot_t h);
uint16_t hal_digipot_step_count(hal_digipot_t h);
uint32_t hal_digipot_e2e_resistance(hal_digipot_t h);
hal_digipot_mode_t hal_digipot_mode(hal_digipot_t h);
```

`hal_digipot_init_ex()` returns `HAL_EINVAL` for an invalid configuration, `HAL_ENOMEM` when the static pool is exhausted, and `HAL_EBUS` when chip or bus initialization fails. `hal_digipot_set_resistance_ex()` returns `HAL_EUNINIT` for an invalid handle, `HAL_EINVAL` for an invalid resistance or mode, `HAL_EBUS` for an I2C failure, and `HAL_EIO` for an MCP401x read-back mismatch. The legacy `hal_digipot_init()` and `hal_digipot_set_resistance()` functions remain available for source compatibility.

**shared thematic implementation:** `hal_digipot.cpp` owns the handle pool, validation dispatch and
per-instance mutex; chip-specific MCP401x/MAX5395 transaction logic lives under
`hal/analog/digipot/`.

**Thread safety:** runtime operations are serialized per instance and each chip
transaction uses HAL I2C helpers.

---


<a id="hal_pga2311---pga2311-stereo-volume-controller--optional---hal_enable_pga2311"></a>

## `hal_pga2311` - stereo volume control  *(optional - `HAL_ENABLE_PGA2311`)*

Set the gain of both PGA2311 channels and mute the output. The driver uses SPI; the application configures the bus pins.

```c
#include <hal/audio/hal_pga2311.h>

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
hal_status_t hal_pga2311_init_ex(const hal_pga2311_config_t *cfg,
                                 hal_pga2311_t *out_handle);
hal_pga2311_t hal_pga2311_init(const hal_pga2311_config_t *cfg);
void hal_pga2311_deinit(hal_pga2311_t h);

hal_status_t hal_pga2311_set_raw_ex(hal_pga2311_t h, uint8_t left_code,
                                    uint8_t right_code);
bool hal_pga2311_set_raw(hal_pga2311_t h, uint8_t left_code, uint8_t right_code);
hal_status_t hal_pga2311_set_raw_both_ex(hal_pga2311_t h, uint8_t code);
bool hal_pga2311_set_raw_both(hal_pga2311_t h, uint8_t code);
hal_status_t hal_pga2311_set_gain_half_db_ex(hal_pga2311_t h,
                                             int16_t left_half_db,
                                             int16_t right_half_db);
bool hal_pga2311_set_gain_half_db(hal_pga2311_t h, int16_t left_half_db, int16_t right_half_db);
hal_status_t hal_pga2311_set_gain_db_ex(hal_pga2311_t h, float left_db,
                                        float right_db);
bool hal_pga2311_set_gain_db(hal_pga2311_t h, float left_db, float right_db);
hal_status_t hal_pga2311_set_gain_db_both_ex(hal_pga2311_t h, float db);
bool hal_pga2311_set_gain_db_both(hal_pga2311_t h, float db);

hal_status_t hal_pga2311_set_mute_ex(hal_pga2311_t h, bool mute);
bool hal_pga2311_set_mute(hal_pga2311_t h, bool mute);
bool hal_pga2311_is_muted(hal_pga2311_t h);

bool hal_pga2311_get_target_raw(hal_pga2311_t h, uint8_t *left_code, uint8_t *right_code);
bool hal_pga2311_get_target_gain_half_db(hal_pga2311_t h, int16_t *left_half_db, int16_t *right_half_db);

hal_status_t hal_pga2311_gain_half_db_to_raw_ex(int16_t half_db,
                                                uint8_t *out_code);
bool hal_pga2311_gain_half_db_to_raw(int16_t half_db, uint8_t *out_code);
hal_status_t hal_pga2311_raw_to_gain_half_db_ex(uint8_t code,
                                                int16_t *out_half_db);
bool hal_pga2311_raw_to_gain_half_db(uint8_t code, int16_t *out_half_db);
```

**Behavior notes:**
- `HAL_ENABLE_PGA2311` gains `HAL_ENABLE_SPI` through the generated feature
  registry included by `hal_config.h`.
- The module does not call `hal_spi_init()`; the application owns SPI bus pin setup.
- Status init distinguishes invalid configuration (`HAL_EINVAL`), static-pool
  or mutex exhaustion (`HAL_ENOMEM`) and propagated SPI setup/write failures.
- Status setters and conversion helpers return `HAL_EINVAL` for invalid
  handles, output pointers or gain ranges and propagate SPI transport errors.
  Existing handle/`bool` APIs are compatibility wrappers.
- With `mute_pin == HAL_PGA2311_MUTE_PIN_NONE`, mute is emulated in software by
  writing `HAL_PGA2311_CODE_MUTE` to both channels and restoring cached target
  codes on unmute.
- With a hardware mute pin configured, mute toggles only GPIO and does not send
  extra SPI frames.

**shared thematic implementation:** `hal/audio/pga2311/pga2311_driver.*` (HAL SPI/GPIO transport)
plus `hal_pga2311.cpp` facade with static handle pool + per-instance mutex.

**Thread safety:** per-instance mutex serializes API calls; SPI transactions are
wrapped in `hal_spi_lock()` / `hal_spi_unlock()`.

---

<a id="hal_mfrc522---mfrc522-rfid-reader--optional---hal_enable_mfrc522"></a>

## `hal_mfrc522` - RFID reader  *(optional - `HAL_ENABLE_MFRC522`)*

Communicate with an MFRC522 reader over SPI or I2C. The application selects the transport and initializes its bus before using the reader.

### C API

The C interface separates the bus transport from the reader. Both are opaque
handles and must be released in reverse creation order.

```c
#include <hal/nfc/hal_mfrc522.h>
#include <hal/spi/hal_spi.h>

hal_status_t read_mfrc522_uid(hal_mfrc522_uid_t *out_uid) {
    if (out_uid == NULL) return HAL_EINVAL;

    hal_status_t status = hal_spi_init(0u, 0u, 3u, 2u);
    if (status != HAL_OK) return status;

    hal_mfrc522_spi_config_t config = hal_mfrc522_spi_default_config(5u);
    hal_mfrc522_transport_t transport = NULL;
    hal_mfrc522_t reader = NULL;
    bool present = false;

    status = hal_mfrc522_transport_create_spi(&config, &transport);
    if (status == HAL_OK) status = hal_mfrc522_create(transport, &reader);
    if (status == HAL_OK) status = hal_mfrc522_begin(reader);
    if (status == HAL_OK) status = hal_mfrc522_is_new_card_present(reader, &present);
    if (status == HAL_OK && !present) status = HAL_ENOENT;
    if (status == HAL_OK) status = hal_mfrc522_read_uid(reader, out_uid);

    if (reader != NULL) {
        hal_status_t close_status = hal_mfrc522_destroy(reader);
        if (status == HAL_OK) status = close_status;
    }
    if (transport != NULL) {
        hal_status_t close_status = hal_mfrc522_transport_destroy(transport);
        if (status == HAL_OK) status = close_status;
    }
    return status;
}
```

`hal_mfrc522_transport_create_spi()` stores the chip-select pin, optional reset
pin, bus index, and SPI settings. With `HAL_ENABLE_I2C`, I2C transport creation
stores the bus, 7-bit address, and optional reset pin. The application retains
ownership of the initialized HAL bus.

One reader may be attached to a transport. Destroying an attached transport
returns `HAL_EBUSY`; first call `hal_mfrc522_destroy()`, then
`hal_mfrc522_transport_destroy()`. The default static pools contain
`HAL_MFRC522_MAX_TRANSPORTS` transports and `HAL_MFRC522_MAX_READERS` readers.

Card discovery returns a `hal_mfrc522_uid_t` containing UID bytes, length, and
SAK. `hal_mfrc522_card_type_from_sak()` and
`hal_mfrc522_card_type_name()` classify it. The status API also covers REQA,
WUPA, halt, antenna and power control, self-test, MIFARE authentication,
Classic block/value operations, Ultralight writes, NTAG216 authentication, and
UID-maintenance operations.

The port preserves the MFRC522 protocol logic from the
MFRC522-spi-i2c-uart-async / Miguel Balboa driver lineage while replacing
transport and timing calls with JaszczurHAL primitives. `StatusCodeToHalStatus()`
maps driver-local outcomes to shared `hal_status_t` values.

**Concurrency:** Reader operations are serialized per instance, and SPI/I2C
transactions use HAL bus locks. Transport and reader creation/destruction are
single-owner operations; do not destroy them while another task is using the
reader.

### Existing C++ compatibility API

The same public header continues to expose `MFRC522_SPI`, `MFRC522_I2C`, and
`MFRC522` to C++ builds. Existing class-based code remains supported.

```cpp
#include <hal/nfc/hal_mfrc522.h>

MFRC522_SPI bus(5, HAL_MFRC522_PIN_NONE, 0);
MFRC522 reader(&bus);

void start_mfrc522_cpp(void) {
    reader.PCD_Init();
    if (reader.PICC_IsNewCardPresent()) {
        (void)reader.PICC_ReadCardSerial();
    }
}
```

New code that needs opaque lifetime management and shared status values should
prefer the C interface above.

Example: `examples/22_rfid_nfc`.

---

<a id="hal_pn532---pn532-nfcrfid-reader--optional---hal_enable_pn532"></a>

## `hal_pn532` - NFC/RFID reader  *(optional - `HAL_ENABLE_PN532`)*

Detect passive cards and perform basic MIFARE operations through a PN532. SPI is available, with I2C and UART enabled by their corresponding flags.

### C API

As with MFRC522, create a transport first and attach one opaque reader handle.

```c
#include <hal/nfc/hal_pn532.h>
#include <hal/spi/hal_spi.h>

hal_status_t read_pn532_uid(hal_pn532_uid_t *out_uid) {
    if (out_uid == NULL) return HAL_EINVAL;

    hal_status_t status = hal_spi_init(0u, 0u, 3u, 2u);
    if (status != HAL_OK) return status;

    hal_pn532_spi_config_t config = hal_pn532_spi_default_config(5u);
    hal_pn532_transport_t transport = NULL;
    hal_pn532_t reader = NULL;
    uint32_t firmware = 0u;

    status = hal_pn532_transport_create_spi(&config, &transport);
    if (status == HAL_OK) status = hal_pn532_create(transport, &reader);
    if (status == HAL_OK) status = hal_pn532_begin(reader);
    if (status == HAL_OK) status = hal_pn532_get_firmware_version(reader, &firmware);
    if (status == HAL_OK) status = hal_pn532_sam_configure(reader);
    if (status == HAL_OK) {
        status = hal_pn532_read_passive_target(
            reader, HAL_PN532_MODULATION_ISO14443A,
            HAL_PN532_DEFAULT_TIMEOUT_MS, out_uid);
    }

    if (reader != NULL) {
        hal_status_t close_status = hal_pn532_destroy(reader);
        if (status == HAL_OK) status = close_status;
    }
    if (transport != NULL) {
        hal_status_t close_status = hal_pn532_transport_destroy(transport);
        if (status == HAL_OK) status = close_status;
    }
    return status;
}
```

`hal_pn532_transport_create_spi()` uses a caller-initialized HAL SPI bus. With
`HAL_ENABLE_I2C`, `hal_pn532_transport_create_i2c()` uses direct HAL I2C
transactions and the PN532 ready byte. With `HAL_ENABLE_UART`,
`hal_pn532_transport_create_uart()` creates and owns its HAL UART, while
`hal_pn532_transport_create_uart_handle()` wraps a caller-owned UART without
destroying it. That UART must remain valid for the transport's entire lifetime;
do not reconfigure or destroy it until after destroying the transport.

One reader may be attached to a transport. Destroying an attached transport
returns `HAL_EBUSY`; release the reader first. The default static pools contain
`HAL_PN532_MAX_TRANSPORTS` transports and `HAL_PN532_MAX_READERS` readers.

Passive discovery fills `hal_pn532_uid_t`. Further status-returning operations
cover firmware and SAM setup, wake-up, raw commands and response validation,
target listing/data exchange, MIFARE Classic authentication and block I/O, and
Ultralight/NTAG2xx page I/O.

The port preserves the Adafruit_PN532 frame construction, ACK handling,
firmware query, SAM configuration, passive target scan and core MIFARE
exchange helpers while replacing transport and timing calls with JaszczurHAL
primitives.

**Concurrency:** Reader operations are serialized per instance. SPI and I2C
transports also use HAL bus locks. Transport and reader creation/destruction
are single-owner operations; do not destroy them while another task is using
the reader.

### Existing C++ compatibility API

The same header continues to expose `PN532` and the enabled `PN532_SPI`,
`PN532_I2C`, or `PN532_UART` transports in C++ builds. Existing class-based
code remains supported.

```cpp
#include <hal/nfc/hal_pn532.h>

PN532_SPI bus(5, HAL_PN532_PIN_NONE, 0);
PN532 reader(&bus);

hal_status_t start_pn532_cpp(void) {
    uint32_t firmware = 0u;
    hal_status_t status = reader.begin();
    if (status == HAL_OK) status = reader.getFirmwareVersion(&firmware);
    return status;
}
```

New code that needs opaque lifetime management should prefer the C interface
above.

Example: `examples/22_rfid_nfc`.

---

<a id="simple-io-chips--optional---hal_enable_mcp23017-hal_enable_pca9654e-hal_enable_pcf8574-hal_enable_hc595-hal_enable_mcp4725"></a>

## GPIO expanders, shift registers, and DAC  *(optional - `HAL_ENABLE_MCP23017`, `HAL_ENABLE_PCA9654E`, `HAL_ENABLE_PCF8574`, `HAL_ENABLE_HC595`, `HAL_ENABLE_MCP4725`)*

```c
#include <hal/gpio/hal_mcp23017.h>
#include <hal/gpio/hal_pca9654e.h>
#include <hal/gpio/hal_pcf8574.h>
#include <hal/gpio/hal_hc595.h>
#include <hal/analog/hal_mcp4725.h>

hal_i2c_init(sda_pin, scl_pin, HAL_I2C_CLOCK_STANDARD_HZ);
hal_spi_init(0, miso_pin, mosi_pin, sck_pin);

/* MCP23017 - I2C 16-bit GPIO expander. */
hal_mcp23017_t gpio = {0};
hal_mcp23017_init_ex(&gpio, NULL);
hal_mcp23017_write_pin_ex(&gpio, 0, true);

/* PCA9654E - I2C 8-bit output expander. */
hal_pca9654e_t out8 = {0};
hal_pca9654e_init_ex(&out8, NULL);
hal_pca9654e_write_all_ex(&out8, 0x0Fu);

/* PCF8574 - I2C 8-bit quasi-bidirectional GPIO. */
hal_pcf8574_t io8 = {0};
hal_pcf8574_init_ex(&io8, NULL);
hal_pcf8574_write_pin_ex(&io8, 3u, true);
uint8_t input_port = hal_pcf8574_read_all(&io8);

/* 74HC595 - SPI shift register (up to 4 chained = 32 outputs). */
hal_hc595_config_t sr_cfg = hal_hc595_default_config(cs_pin);
hal_hc595_t sr = {0};
hal_hc595_init_ex(&sr, &sr_cfg);
hal_hc595_write_all_ex(&sr, 0x55u);

/* MCP4725 - I2C 12-bit DAC. */
hal_mcp4725_t dac = {0};
hal_mcp4725_init_ex(&dac, NULL);
hal_mcp4725_write_ex(&dac, 2048u); /* ~ mid-scale */
```

Available modules:

- `hal_mcp23017`: MCP23017 GPIO expander over I2C. Runtime modes mirror the
  grblHAL plugin variants: 8 inputs/8 outputs, 16 outputs, or 16 inputs. Input
  inversion, pull-ups and MCP interrupt register configuration are exposed with
  `hal_status_t` APIs.
- `hal_pca9654e`: PCA9654E output-only expander over I2C. Init writes the
  source-driver register sequence: all pins output, no inversion, outputs low.
- `hal_pcf8574`: PCF8574 quasi-bidirectional GPIO expander over I2C. The driver
  keeps the output latch locally, writes the full 8-bit port in one transaction,
  and reads the current port state as one byte.
- `hal_hc595`: one to four chained 74HC595 shift registers over HAL SPI and a
  GPIO latch/chip-select pin. Bytes are shifted highest register first, matching
  the source driver.
- `hal_mcp4725`: MCP4725 12-bit DAC over I2C. Init can send the general-call
  reset/wake sequence, reads the EEPROM-backed current DAC value, and writes
  fast-mode DAC updates.

The transaction flow is based on working grblHAL plugin drivers by Terje Io and
uses JaszczurHAL I2C/SPI/GPIO/timing/status/sync primitives throughout.

Each simple-I/O driver exposes `_ex` functions for fallible transactions and
keeps the legacy `bool`/value-returning calls as thin compatibility wrappers.
Invalid device pointers, pins, modes or output pointers return `HAL_EINVAL`;
use before successful init returns `HAL_EUNINIT`; mutex allocation failure
returns `HAL_ENOMEM`; and I2C/SPI/GPIO transaction failures return `HAL_EBUS`
or `HAL_EIO` depending on the backend operation. Value-returning compatibility
reads keep their historical zero-on-failure shape; use the `_ex` forms when the
caller needs to distinguish zero data from an error.

**Concurrency:** Each device instance has its own HAL mutex. Transactions also acquire HAL I2C/SPI bus locks. One part of the application should own initialization and cleanup of each instance.

Example: `examples/23_io_pmic`.

---

*Next: [Storage](14_storage.md)*
