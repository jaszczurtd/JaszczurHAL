# JaszczurHAL - API Reference

Hardware Abstraction Layer for embedded projects.
The current primary backend is RP2040 via Arduino-pico, but the intent is to
grow additional ports over time (for example STM32) without changing the
application-facing HAL API.

This document is the canonical, detailed API reference.
The top-level `README.md` intentionally stays concise and links here for full behavior/contracts.

Current Arduino backend requirement: Earle Philhower Arduino core for RP2040/RP2350
(arduino-pico): https://github.com/earlephilhower/arduino-pico
Minimum version for RP2350 support: 4.0.0 (latest stable recommended).

**Author:** Marcin 'Jaszczur' Kielesiński

**Repository:** `git@github.com:jaszczurtd/JaszczurHAL.git`
**Include root:** `libraries/JaszczurHAL/src/` (registered in `otherLibrariesFolders`)
**Public include:** `#include <JaszczurHAL.h>`
**Internal HAL-only include:** `#include <hal/hal.h>`

---

## Library structure

- `src/JaszczurHAL.h` - umbrella include for HAL + utility modules.
- `src/HAL_FLAGS.txt` - concise `HAL_DISABLE_*` / `HAL_ENABLE_*` flag summary.
- `src/libConfig.h` - backward-compat redirect to `hal/hal_config.h`.
- `src/tools.h` - C++ utility aggregator.
- `src/tools_c.h` - C-compatible utility declarations.
- `src/arduino_host_stubs/` - host-build compatibility stubs such as `Arduino.h`, `SPI.h`, and `SD.h`.
- `src/hal/hal.h` - HAL-only umbrella include.
- `src/hal/hal_config.h` and `src/hal/hal_config.cpp` - build-time feature flags and runtime config helpers.
- `src/hal/*.h` - public HAL module interfaces such as GPIO, ADC, PWM, timers, sync, serial, crypto, I2C, SPI, CAN, display, GPS, EEPROM, WiFi, and time.
- `src/hal/hal_can_util.cpp`, `src/hal/hal_crypto.cpp`, `src/hal/hal_kv.cpp`, `src/hal/hal_soft_timer.cpp`, `src/hal/hal_pid_controller.cpp` - shared HAL wrapper implementations.
- `src/hal/hal_uart_config.h` - UART configuration constants and helpers.
- `src/hal/impl/arduino/` - Arduino / RP2040 backend.
- `src/hal/impl/.mock/` - deterministic host-test backend.
- `src/hal/impl/arduino/drivers/` - bundled third-party drivers used by optional HAL modules.
- `src/utils/` - higher-level utilities: `tools`, `SmartTimers`, `pidController`, `multicoreWatchdog`, `draw7Segment`, optional `cJSON`, and bundled Unity sources.

`JaszczurHAL.h` is the current top-level public include and should be the
default include in project code. `hal/hal.h` remains available as a HAL-only
aggregator, but it is not the primary include exported by the current library
metadata.

---

## Documentation scope

This file is the API-oriented companion to `README.md`.

Recommended split of responsibilities:

- `README.md`: overview, architecture, quick start, build/test entry points, practical examples
- `JaszczurHAL_API.md`: module layout, migration notes, public API details, feature-flag reference

Where both documents touch the same topic, `README.md` should be treated as the
short onboarding guide, while this file stays focused on reference material.

---

## Public API vs helper modules

The repository contains both the HAL itself and a set of utility modules.

### HAL public API

These are the portability-oriented interfaces intended to decouple application
logic from Arduino and other board-specific SDK calls:

- `hal_gpio`, `hal_adc`, `hal_pwm`, `hal_pwm_freq`
- `hal_timer`, `hal_soft_timer`, `hal_system`, `hal_bits`, `hal_sync`, `hal_serial`
- `hal_crypto`
- `hal_pid_controller`
- `hal_uart`, `hal_swserial`, `hal_spi`, `hal_i2c`
- `hal_can`, `hal_display`, `hal_rgb_led`
- `hal_thermocouple`, `hal_external_adc`, `hal_gps`
- `hal_eeprom`, `hal_kv`, `hal_wifi`, `hal_time`
- `hal_time_from_components(...)` for deterministic date/time-to-epoch conversion
- optional timestamp hook for error logging via `hal_debug_set_timestamp_hook(...)`

### Helper / utility modules

These are convenient adjuncts, but they are not the portability boundary
itself:

- `tools`
- `SmartTimers`
- `pidController`
- `multicoreWatchdog`
- `draw7Segment`

When designing new application code, prefer depending on the HAL layer first.
Helper modules are useful building blocks, but they should not replace the HAL
boundary conceptually.

---

## Selective module inclusion (`HAL_DISABLE_*`)

By default every module listed above is compiled and linked.
To exclude modules your project does not use, define one or more
`HAL_DISABLE_<MODULE>` preprocessor flags.  This removes:

* the **API declarations** from the header (the file compiles to an empty
  translation unit, so calling a disabled function gives a clear compile-time
  error);
* the **implementation** .cpp (no object code, no third-party `#include`);
* the entry in the **umbrella header** `hal/hal.h`.

### Available flags

| Flag | Header | Impl | 3rd-party deps removed |
|---|---|---|---|
| `HAL_DISABLE_WIFI` | `hal_wifi.h` | `hal_wifi.cpp` | WiFi (arduino-pico) |
| `HAL_DISABLE_TIME` | `hal_time.h`* | `hal_time.cpp` | WiFi NTP helpers |
| `HAL_DISABLE_EEPROM` | `hal_eeprom.h` | `hal_eeprom.cpp` | EEPROM, Wire (AT24C256) |
| `HAL_DISABLE_KV` | `hal_kv.h` | `hal_kv.cpp` | *(depends on EEPROM)* |
| `HAL_DISABLE_GPS` | `hal_gps.h` | `hal_gps.cpp` | TinyGPS++ |
| `HAL_DISABLE_THERMOCOUPLE` | `hal_thermocouple.h` | `hal_thermocouple.cpp` | bundled MCP9600/MAX6675 drivers |
| `HAL_DISABLE_MCP9600` | `hal_thermocouple.h` | `hal_thermocouple.cpp` | bundled MCP9600 driver |
| `HAL_DISABLE_MAX6675` | `hal_thermocouple.h` | `hal_thermocouple.cpp` | bundled MAX6675 driver |
| `HAL_DISABLE_UART` | `hal_uart.h` | `hal_uart.cpp` | SerialUART |
| `HAL_DISABLE_SWSERIAL` | `hal_swserial.h` | `hal_swserial.cpp` | SoftwareSerial |
| `HAL_DISABLE_I2C` | `hal_i2c.h` | `hal_i2c.cpp` | Wire (master) |
| `HAL_DISABLE_I2C_SLAVE` | `hal_i2c_slave.h` | `hal_i2c_slave.cpp` | Wire (slave/target) |
| `HAL_DISABLE_EXTERNAL_ADC` | `hal_external_adc.h` | `hal_external_adc.cpp` | bundled ADS1X15 driver |
| `HAL_DISABLE_PWM_FREQ` | `hal_pwm_freq.h` | `hal_pwm_freq.cpp` | hardware/pwm (pico SDK) |
| `HAL_DISABLE_RGB_LED` | `hal_rgb_led.h` | `hal_rgb_led.cpp` | Adafruit NeoPixel |
| `HAL_DISABLE_CAN` | `hal_can.h` | `hal_can.cpp` | bundled MCP2515 driver |
| `HAL_DISABLE_DISPLAY` | `hal_display.h` | `hal_display.cpp` | Adafruit GFX, ILI9341/ST77xx/SSD1306 |
| `HAL_DISABLE_TFT` | `hal_display.h` | `hal_display.cpp` | all TFT drivers (`Adafruit_ILI9341`, `Adafruit_ST7789`, `Adafruit_ST7735`, `Adafruit_ST7796S`) |
| `HAL_DISABLE_SSD1306` | `hal_display.h` | `hal_display.cpp` | `Adafruit_SSD1306` |
| `HAL_DISABLE_ILI9341` | `hal_display.h` | `hal_display.cpp` | `Adafruit_ILI9341` |
| `HAL_DISABLE_ST7789` | `hal_display.h` | `hal_display.cpp` | `Adafruit_ST7789` |
| `HAL_DISABLE_ST7735` | `hal_display.h` | `hal_display.cpp` | `Adafruit_ST7735` |
| `HAL_DISABLE_ST7796S` | `hal_display.h` | `hal_display.cpp` | `Adafruit_ST7796S` |
| `HAL_DISABLE_UNITY` | utility headers/sources | `utils/unity.*` | bundled Unity framework |

### `HAL_ENABLE_*` flags

| Flag | Scope | Effect |
|---|---|---|
| `HAL_ENABLE_CJSON` | `src/tools.h` aggregator | exposes bundled `utils/cJSON.h` and `utils/cJSON_Utils.h` includes |

### Dependency propagation (hal\_config.h)

Disabling a base module automatically disables its dependants:

```
HAL_DISABLE_EEPROM   →  HAL_DISABLE_KV
HAL_DISABLE_WIFI     →  HAL_DISABLE_TIME
HAL_DISABLE_I2C      →  HAL_DISABLE_EXTERNAL_ADC
HAL_DISABLE_SWSERIAL →  HAL_DISABLE_GPS
HAL_DISABLE_MCP9600 + HAL_DISABLE_MAX6675 → HAL_DISABLE_THERMOCOUPLE
HAL_DISABLE_ILI9341 + HAL_DISABLE_ST7789 + HAL_DISABLE_ST7735 + HAL_DISABLE_ST7796S → HAL_DISABLE_TFT
HAL_DISABLE_TFT + HAL_DISABLE_SSD1306 → HAL_DISABLE_DISPLAY
```

You may also define a dependant flag alone (e.g. `HAL_DISABLE_KV` without
`HAL_DISABLE_EEPROM`) to keep the base while excluding the dependant.

### Passing flags - recommended: `hal_project_config.h`

Create `hal_project_config.h` in your sketch directory:

```c
#pragma once
#define HAL_DISABLE_WIFI
#define HAL_DISABLE_EEPROM      // → propagates HAL_DISABLE_KV
#define HAL_DISABLE_GPS
#define HAL_DISABLE_THERMOCOUPLE
#define HAL_DISABLE_UART
#define HAL_DISABLE_SWSERIAL
#define HAL_DISABLE_I2C         // → propagates HAL_DISABLE_EXTERNAL_ADC
#define HAL_DISABLE_PWM_FREQ
```

`hal_config.h` detects it via `__has_include("hal_project_config.h")`.

Arduino CLI does not add the sketch directory to the include path for library
source files, so the build command must add it explicitly:

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --build-property "compiler.cpp.extra_flags=-I '/path/to/sketch'" \
  --build-property "compiler.c.extra_flags=-I '/path/to/sketch'" \
  --build-path .build \
  --warnings all .
```

In VS Code tasks, use `${workspaceFolder}` for the path.

### Alternative: `-D` flags on the command line

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --build-property "build.extra_flags=\
-DHAL_DISABLE_WIFI \
-DHAL_DISABLE_EEPROM \
-DHAL_DISABLE_GPS \
-DHAL_DISABLE_THERMOCOUPLE \
-DHAL_DISABLE_UART \
-DHAL_DISABLE_SWSERIAL \
-DHAL_DISABLE_I2C \
-DHAL_DISABLE_EXTERNAL_ADC \
-DHAL_DISABLE_PWM_FREQ" \
  --build-path .build \
  --warnings all .
```

### Core modules (no disable flag)

| Module | Purpose |
|---|---|
| `hal_gpio` | GPIO read / write / interrupts |
| `hal_adc` | On-chip ADC |
| `hal_pwm` | Basic analogWrite PWM |
| `hal_timer` | One-shot alarm timers |
| `hal_system` | millis / delay / watchdog / idle + type-independent `hal_constrain` / `hal_map` + `COUNTOF(arr)` |
| `hal_bits` | bit helpers (`is_set`, `set_bit`, `bitSet`, volatile register ops) |
| `hal_sync` | Mutexes, critical sections |
| `hal_serial` | Debug serial output |
| `hal_spi` | SPI bus init |
| `hal_crypto` | Base64, MD5, ChaCha20, ChaCha20-Poly1305 helpers |
| `hal_math` | type-independent `hal_constrain` / `hal_map` macros |

### SD library (`<SD.h>`)

Separate from `HAL_DISABLE_*`: the `<SD.h>` include in `tools.h` is guarded by
`#ifdef SD_LOGGER`.  If your project does not define `SD_LOGGER`, the SD
library is not compiled.

### Note about `library.properties:depends`

`library.properties` currently does **not** declare `depends=`.
Optional Arduino drivers used by HAL modules are vendored in
`src/hal/impl/arduino/drivers/`.

Actual compiled dependencies are controlled by the module set:

- enabled modules may pull their third-party backends
- disabled modules compile out both declarations and implementation details

\* `HAL_DISABLE_TIME` hides NTP/local-time APIs, but `hal_time_from_components(...)`
remains available as a pure conversion helper with no network dependency.

---

## Multicore safety policy

JaszczurHAL targets RP2040/RP2350 dual-core systems (core 0 + core 1).
The following design rules apply:

### Initialisation: single-core only

All `*_init()`, `*_create()`, and `*_deinit()` / `*_destroy()` functions must be
called from **one core only** (typically core 0 during `setup()`).  These
functions allocate from static pools, configure hardware peripherals, and
establish internal state.  They are **not** protected by mutexes because:

- pool allocation is inherently single-shot (done once at boot),
- hardware peripheral setup must complete before use,
- adding mutex overhead to init paths provides no practical benefit when the
  documented contract is respected.

### Runtime: multicore-safe (Arduino backend)

After initialisation, most HAL runtime APIs are multicore-safe on the Arduino
backend.  Each module documents its thread-safety guarantee in the per-module
section below.  The general pattern is:

- **Per-instance mutexes** protect handle-based APIs (`hal_can`, `hal_thermocouple`, `SmartTimers`).
- **Per-bus mutexes** protect shared communication buses (`hal_spi`, `hal_i2c`).
- **Singleton mutexes** protect global modules (`hal_eeprom`, `hal_display`, `hal_gps`, `hal_external_adc`, `hal_wifi`, `hal_kv`, debug serial).
- **Stateless helpers** (`hal_bits`, `hal_math`, `hal_crypto`, `hal_constrain`, `hal_map`) are inherently thread-safe.

Modules documented as **"Not thread-safe"** (`hal_uart`, `hal_swserial`, `hal_time`, `pidController`)
must be serialized by the caller or used from a single core.

### Mock backend

Mock implementations (`impl/.mock/`) are designed for deterministic single-threaded
unit tests and do **not** provide real cross-thread synchronization.  Thread-safety
guarantees listed in per-module sections apply to the **Arduino backend only**
unless explicitly stated otherwise.

---

## Drivers

Bundled drivers live under `src/hal/impl/arduino/drivers/` and are integrated
as HAL-internal implementation detail (not public API).

### Inventory, authors and license paths

| Driver folder | HAL usage | Upstream author(s) | License | License path in repo |
|---|---|---|---|---|
| `ADS1X15` | `hal_external_adc` | Rob Tillaart | MIT | `src/hal/impl/arduino/drivers/ADS1X15/LICENSE` |
| `Adafruit_BusIO` | display + MCP9600 transport layer | Adafruit Industries | MIT | `src/hal/impl/arduino/drivers/Adafruit_BusIO/LICENSE` |
| `Adafruit_GFX_Library` | display base classes/fonts | Limor Fried (Ladyada) + contributors | BSD | `src/hal/impl/arduino/drivers/Adafruit_GFX_Library/license.txt` |
| `Adafruit_ILI9341` | TFT backend (`HAL_DISPLAY_ILI9341`) | Limor Fried (Ladyada) | license notice in sources (BSD) + README note (MIT) | `src/hal/impl/arduino/drivers/Adafruit_ILI9341/Adafruit_ILI9341.h` and `src/hal/impl/arduino/drivers/Adafruit_ILI9341/README.md` |
| `Adafruit_MCP9600` | thermocouple MCP9600 backend | Kevin Townsend, Limor Fried | BSD in driver docs/sources (`license.txt`) + extra MIT file | `src/hal/impl/arduino/drivers/Adafruit_MCP9600/license.txt` and `src/hal/impl/arduino/drivers/Adafruit_MCP9600/LICENSE` |
| `Adafruit_NeoPixel` | `hal_rgb_led` | Phil "Paint Your Dragon" Burgess + contributors | LGPL | `src/hal/impl/arduino/drivers/Adafruit_NeoPixel/COPYING` |
| `Adafruit_SSD1306` | OLED backend (`HAL_DISABLE_SSD1306`) | Limor Fried (Ladyada) + contributors | BSD | `src/hal/impl/arduino/drivers/Adafruit_SSD1306/license.txt` |
| `Adafruit_ST7735_and_ST7789_Library` | ST7735/ST7789/ST7796S backends | Limor Fried (Ladyada) | MIT | `src/hal/impl/arduino/drivers/Adafruit_ST7735_and_ST7789_Library/README.txt` |
| `Adafruit_Zero_DMA_Library` | SPI TFT DMA path (`Adafruit_SPITFT`) | Phil "PaintYourDragon" Burgess | MIT (+ ASF-derived `utility/dma.h`) | `src/hal/impl/arduino/drivers/Adafruit_Zero_DMA_Library/LICENSE` and `src/hal/impl/arduino/drivers/Adafruit_Zero_DMA_Library/utility/dma.h` |
| `MAX6675` | thermocouple MAX6675 backend | Adafruit (Limor Fried) | BSD (license file in driver folder) | `src/hal/impl/arduino/drivers/MAX6675/license.txt` |
| `MCP2515` | `hal_can` backend | Seeed Technology (Loovee), Cory J. Fowler | LGPL (headers indicate LGPL-2.1+, `license.txt` included) | `src/hal/impl/arduino/drivers/MCP2515/license.txt` and `src/hal/impl/arduino/drivers/MCP2515/mcp_can.h` |
| `TinyGPSPlus` | `hal_gps` parser backend | Mikal Hart | LGPL-2.1+ notice in source headers | `src/hal/impl/arduino/drivers/TinyGPSPlus/src/TinyGPS++.h` |

Note: `Adafruit_GFX_Library/Fonts/` includes additional per-font notices in
font headers (e.g. `TomThumb.h`, `Tiny3x3a2pt7b.h`).

### Integration changes and rationale

| Area | What changed | Why |
|---|---|---|
| Include wiring | HAL modules include bundled drivers from local `drivers/` paths; driver-to-driver includes were rewired to local relative paths. | Keeps third-party code encapsulated inside HAL internals and avoids global include namespace leaks. |
| Conditional compilation | Driver `.cpp` files are wrapped with module-level `HAL_DISABLE_*` guards. | Disabled modules remove both HAL wrappers and third-party backend code from build. |
| SPI synchronization | Drivers using SPI transactions now integrate `hal_spi_lock`/`hal_spi_unlock` where needed (CAN, BusIO SPI, SPITFT path). | Prevents cross-thread/cross-core SPI transaction interleaving. |
| I2C synchronization | Drivers doing I2C traffic integrate `hal_i2c_lock_bus`/`hal_i2c_unlock_bus` and bus mapping where needed. | Prevents mixed Wire/Wire1 transactions and improves determinism under concurrency. |
| Per-driver mutexes | Selected drivers/wrappers now own mutexes for multi-step operations (`MCP2515`, `MAX6675`, `Adafruit_MCP9600`, HAL wrappers). | Reduces race conditions in read/modify/write and multi-call command sequences. |
| Wire1 support | HAL I2C APIs and driver adapters map `TwoWire*` to bus index 0/1 and support `Wire1` when present. | Allows second controller usage without bypassing HAL thread-safety. |
| ZeroDMA bundling | `Adafruit_SPITFT` now references bundled `Adafruit_Zero_DMA_Library`; ZeroDMA code is display/TFT-guarded. | Keeps display DMA path self-contained and consistent with HAL feature flags. |
| TinyGPSPlus bundling | `hal_gps` now uses bundled TinyGPSPlus source with `HAL_DISABLE_GPS` compile guard in driver source. | Parser version is controlled in-repo and compiles out with GPS module disable. |

---

## Logging timestamp hook

The serial debug module supports optional timestamp prefixing for error logs.

API:

- `typedef bool (*hal_debug_timestamp_hook_t)(char *out, size_t out_size, void *user);`
- `void hal_debug_set_timestamp_hook(hal_debug_timestamp_hook_t hook, void *user);`

Behavior:

- if hook returns `true` and writes non-empty text, `hal_derr()` /
    `hal_derr_limited()` prepend `[`timestamp`]` before `ERROR! ...`
- if hook is unset or returns `false`, logging behaves exactly as before

Typical usage:

```c
static bool app_ts_hook(char *out, size_t out_size, void *user) {
        (void)user;
        unsigned long ms = hal_millis();
        snprintf(out, out_size, "t+%lu.%03lus", ms / 1000UL, ms % 1000UL);
        return true;
}

void setup(void) {
        hal_debug_init(115200, NULL);
        hal_debug_set_timestamp_hook(app_ts_hook, NULL);
}
```

---

## Time conversion helper

`hal_time_from_components(int year, int month, int day, int hour, int minute, int second)`
converts date/time components to Unix epoch seconds.

Validation:

- returns `0` for out-of-range values (year < 1970, invalid month/day/time)
- supports leap-year rules (including century exceptions)

This helper is implemented for both Arduino and mock backends.

---

## Examples

For quick-start usage examples, prefer the examples in `README.md`.

Typical flows covered there:

- GPIO plus timing
- I2C plus EEPROM
- WiFi plus NTP/system time
- display initialisation

This file keeps the lower-level API reference and migration mapping.

---

## Host-test coverage

Host/mock tests are built via CMake and validate the desktop-facing mock
backend together with selected utility modules.

Covered test targets include:

- `test_hal_gpio`, `test_hal_adc`, `test_hal_pwm`, `test_hal_spi`, `test_hal_timer`
- `test_hal_i2c`, `test_hal_i2c_slave`, `test_hal_rgb_led`, `test_hal_external_adc`, `test_hal_gps`, `test_hal_system`, `test_hal_bits`
- `test_hal_serial`, `test_hal_serial_session`, `test_hal_uart`, `test_hal_swserial`
- `test_hal_can`, `test_hal_thermocouple`, `test_hal_display`
- `test_hal_eeprom`, `test_hal_kv`, `test_hal_wifi`, `test_hal_time`
- `test_SmartTimers`, `test_pidController`, `test_multicoreWatchdog`, `test_tools`
- `hal_soft_timer_*` and `hal_pid_controller_*` are thin wrappers over these utility cores.

Build/run entry point:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Migration guide: replacing Arduino/pico SDK calls

| Old call | New call / macro |
|---|---|
| `millis()` | `hal_millis()` |
| `micros()` | `hal_micros()` |
| `time_us_64()` | `hal_micros64()` |
| `delay(ms)` | `hal_delay_ms(ms)` or `m_delay(ms)` |
| `delayMicroseconds(us)` | `hal_delay_us(us)` or `m_delay_microseconds(us)` |
| `rp2040.getFreeHeap()` | `hal_get_free_heap()` |
| `analogReadTemp()` | `hal_read_chip_temp()` |
| `watchdog_update()` | `hal_watchdog_feed()` |
| `watchdog_enable(ms, dbg)` | `hal_watchdog_enable(ms, dbg)` |
| `watchdog_caused_reboot()` | `hal_watchdog_caused_reboot()` |
| `pinMode(pin, OUTPUT)` | `hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT)` |
| `pinMode(pin, INPUT)` | `hal_gpio_set_mode(pin, HAL_GPIO_INPUT)` |
| `pinMode(pin, INPUT_PULLUP)` | `hal_gpio_set_mode(pin, HAL_GPIO_INPUT_PULLUP)` |
| `digitalWrite(pin, HIGH/LOW)` | `hal_gpio_write(pin, true/false)` |
| `digitalRead(pin)` | `hal_gpio_read(pin)` |
| `attachInterrupt(...)` | `hal_gpio_attach_interrupt(pin, cb, mode)` |
| `irq_set_priority(IO_IRQ_BANK0, p)` | `hal_gpio_set_irq_priority(priority)` |
| `analogWriteResolution(b)` | `hal_pwm_set_resolution(b)` |
| `analogWrite(pin, val)` | `hal_pwm_write(pin, val)` |
| `analogReadResolution(b)` | `hal_adc_set_resolution(b)` |
| `analogRead(pin)` | `hal_adc_read(pin)` |
| `noInterrupts()` | `hal_critical_section_enter()` |
| `interrupts()` | `hal_critical_section_exit()` |
| `Serial.begin(baud)` | `hal_serial_begin(baud)` / `hal_debug_init(baud)` |
| `Serial.print(s)` | `hal_serial_print(s)` |
| `Serial.println(s)` | `hal_serial_println(s)` |
| `Serial.available()` | `hal_serial_available()` |
| `Serial.read()` | `hal_serial_read()` |
| `deb(fmt, ...)` | `hal_deb(fmt, ...)` - macro alias still available via tools.h |
| `derr(fmt, ...)` | `hal_derr(fmt, ...)` - macro alias still available via tools.h |
| repeated noisy error logs | `hal_derr_limited(source, fmt, ...)` - per-source rate-limited logging |
| `setDebugPrefix(p)` | `hal_deb_set_prefix(p)` - macro alias via tools.h |
| manual `concatStrings(buf, ..., MODULE_NAME, ":")` + `setDebugPrefix(buf)` | `setDebugPrefixWithColon(MODULE_NAME)` |
| `mutex_t` + pico SDK mutex | `hal_mutex_t` + `hal_mutex_create/lock/unlock/destroy` |
| `constrain(v, lo, hi)` | `hal_constrain(v, lo, hi)` (type-independent macro from `hal/hal_system.h` / `hal/hal_math.h`); `pid_clamp` is a backward-compat alias |
| `map(x, …)` | `hal_map(x, in_min, in_max, out_min, out_max)` (type-independent macro from `hal/hal_system.h` / `hal/hal_math.h`) |
| `min(a, b)` | `hal_min(a, b)` (macro, in `hal/hal_config.h`) |
| `max(a, b)` | `hal_max(a, b)` (macro, in `hal/hal_config.h`) |
| `EEPROM.begin(size)` | `hal_eeprom_init(HAL_EEPROM_RP2040, size)` |
| `EEPROM.read(addr)` | `hal_eeprom_read_byte(addr)` |
| `EEPROM.write(addr, val)` | `hal_eeprom_write_byte(addr, val)` |
| `EEPROM.commit()` | `hal_eeprom_commit()` |
| `writeAT24(addr, val)` | `hal_eeprom_write_byte(addr, val)` |
| `readAT24(addr)` | `hal_eeprom_read_byte(addr)` |
| `writeAT24Int(addr, val)` | `hal_eeprom_write_int(addr, val)` |
| `readAT24Int(addr)` | `hal_eeprom_read_int(addr)` |
| `resetEEPROM()` | `hal_eeprom_reset()` |
| `bitSet(v, b)` | `bitSet(var, bit)` - defined in `hal/hal_bits.h` (guarded with `#ifndef`) |
| `bitClear(v, b)` | `bitClear(var, bit)` - defined in `hal/hal_bits.h` (guarded with `#ifndef`) |
| `bitRead(v, b)` | `bitRead(var, bit)` - defined in `hal/hal_bits.h` (guarded with `#ifndef`) |
| Adafruit_ILI9341 direct | `hal_display_*` functions |

---

## `hal_gpio` - GPIO

```c
#include <hal/hal_gpio.h>

typedef enum {
    HAL_GPIO_INPUT        = 0,
    HAL_GPIO_OUTPUT       = 1,
    HAL_GPIO_INPUT_PULLUP = 2,
} hal_gpio_mode_t;

typedef enum {
    HAL_GPIO_IRQ_FALLING = 0,
    HAL_GPIO_IRQ_RISING  = 1,
    HAL_GPIO_IRQ_CHANGE  = 2,
} hal_gpio_irq_mode_t;

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode);
void hal_gpio_write(uint8_t pin, bool high);
bool hal_gpio_read(uint8_t pin);
void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode);

typedef enum {
    HAL_IRQ_PRIORITY_HIGHEST = 0,
    HAL_IRQ_PRIORITY_HIGH    = 1,
    HAL_IRQ_PRIORITY_DEFAULT = 2,
    HAL_IRQ_PRIORITY_LOW     = 3,
} hal_irq_priority_t;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority);
```

**Note:** The callback passed to `hal_gpio_attach_interrupt` runs in ISR context - avoid `printf`, `malloc`, `Serial`, or any blocking call inside it.
**Thread safety:** `hal_gpio_write` / `hal_gpio_read` are thin pass-throughs. Concurrent access to different pins from different cores is safe. Concurrent access to the same pin from two cores requires external synchronization.
**IRQ priority:** `hal_gpio_set_irq_priority` sets the NVIC priority of the GPIO interrupt bank. On RP2040 all GPIO pins share IO_IRQ_BANK0. Call after `hal_gpio_attach_interrupt()`. Raising priority above other peripherals (e.g. I2C) prevents their ISRs from blocking edge counting. On platforms without configurable IRQ priorities this is a no-op.

---

## `hal_pwm` - PWM (simple, Arduino-level)

```c
#include <hal/hal_pwm.h>

void hal_pwm_set_resolution(uint8_t bits);
void hal_pwm_write(uint8_t pin, uint32_t value);
```

**impl/arduino:** `analogWriteResolution()`, `analogWrite()` (Arduino-pico).
**Thread safety:** Each GPIO pin maps to an independent PWM hardware slice. Concurrent writes to different pins are safe. `hal_pwm_set_resolution` modifies global Arduino-pico state - call it once during init, not concurrently.

---

## `hal_pwm_freq` - PWM with frequency control (pico SDK-backed)  *(optional - `HAL_DISABLE_PWM_FREQ`)*

Use this instead of `hal_pwm` when you need a specific PWM frequency (e.g. 160 Hz, 300 Hz).

```c
#include <hal/hal_pwm_freq.h>

// Opaque handle
typedef hal_pwm_freq_channel_impl_t *hal_pwm_freq_channel_t;

// Create a channel: pin, frequency in Hz, resolution (wrap value, e.g. 2047 for 11-bit = 2^11-1)
hal_pwm_freq_channel_t hal_pwm_freq_create(uint8_t pin,
                                           uint32_t frequency_hz,
                                           uint32_t resolution);

// Write value in [0, resolution] - values outside range are clamped automatically
void hal_pwm_freq_write(hal_pwm_freq_channel_t ch, int value);

// Free resources
void hal_pwm_freq_destroy(hal_pwm_freq_channel_t ch);
```

**impl/arduino:** pico SDK `hardware/pwm.h` + `hardware/clocks.h` - computes clkdiv and wrap
to achieve the exact requested frequency, with pseudo/slow-scale correction for edge cases.
The PWM slice is configured at `hal_pwm_freq_create()` time but **not started** - the GPIO
function and slice enable are deferred until the first `hal_pwm_freq_write()` call. This
prevents a glitch on pins with inverted logic (0 % duty = actuator ON) at power-on.
**impl/.mock:** stores last written value; injectable via mock helpers.

**Mock helpers:**
```c
int      hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch);
uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch);
uint8_t  hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch);
```

**Thread safety:** Arduino backend: `hal_pwm_freq_write()` is thread-safe and multicore-safe (internal mutex). `hal_pwm_freq_create()` and `hal_pwm_freq_destroy()` are not synchronized and should be called from one core during setup/teardown. Mock backend does not provide concurrent-access synchronization.

---

## `hal_adc` - Analog input

```c
#include <hal/hal_adc.h>

void hal_adc_set_resolution(uint8_t bits);
int  hal_adc_read(uint8_t pin);
```

**impl/arduino:** `analogReadResolution()`, `analogRead()` (Arduino-pico).
**impl/.mock:** injectable per-pin values via `hal_mock_adc_inject(pin, value)`.
**Thread safety:** Thread-safe and multicore-safe. An internal mutex protects the RP2040 shared ADC multiplexer - concurrent `hal_adc_read()` calls from different cores are serialized automatically.

---

## `hal_timer` - Hardware alarms

```c
#include <hal/hal_timer.h>

typedef int32_t hal_alarm_id_t;
#define HAL_ALARM_INVALID (-1)
typedef int64_t (*hal_alarm_callback_t)(hal_alarm_id_t id, void *user_data);

hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us, hal_alarm_callback_t callback,
                                      void *user_data, bool fire_if_past);
bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id);
```

**impl/arduino:** pico SDK `add_alarm_in_us` / `cancel_alarm`.
**Thread safety:** Arduino backend is thread-safe and multicore-safe for scheduling/canceling alarms. Alarm callbacks execute in interrupt context - avoid blocking calls and long critical sections inside callbacks. Mock backend is deterministic but not synchronized for concurrent test-thread access.

---

## `hal_system` - Timing, watchdog & system info

```c
#include <hal/hal_system.h>

// Time-conversion macros (also included by SmartTimers.h)
#define SECOND      1000UL
#define SECS(t)     ((unsigned long)((t) * SECOND))
#define MINS(t)     (SECS(t) * 60UL)
#define HOURS(t)    (MINS(t) * 60UL)
#define COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

uint32_t hal_millis(void);
uint32_t hal_micros(void);
uint64_t hal_micros64(void);          // 64-bit timestamp, no overflow
void     hal_delay_ms(uint32_t ms);
void     hal_delay_us(uint32_t us);
void     hal_watchdog_feed(void);
void     hal_watchdog_enable(uint32_t ms, bool pause_on_debug);
bool     hal_watchdog_caused_reboot(void);
void     hal_idle(void);
uint32_t hal_get_free_heap(void);     // available heap in bytes
float    hal_read_chip_temp(void);    // on-die temperature in °C (±2 °C typical)
void     hal_enter_bootloader(void);  // jump to RP2040 USB bootloader (does not return on hardware)
uint32_t hal_get_core_id(void);       // 0 or 1
void     hal_u32_to_bytes_be(uint32_t val, uint8_t *buf); // writes big-endian bytes

// Device unique identifier (RP2040 flash unique id).
#define HAL_DEVICE_UID_BYTES        8u
#define HAL_DEVICE_UID_HEX_BUF_SIZE 17u  // 16 hex chars + NUL

void hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]);
bool hal_get_device_uid_hex(char *buf, size_t buflen);

// Type-independent math helpers (macros)
#define hal_constrain(v, lo, hi) ...
#define hal_map(x, in_min, in_max, out_min, out_max) ...

// NONULL helper macro: if pointer is null, jump to local `error:` label
#define NONULL(x) do { if ((x) == NULL) { goto error; } } while (0)
```

**impl/arduino:** `millis()`, `micros()`, `time_us_64()`, `delay()`, `delayMicroseconds()`, pico SDK `watchdog_*`, `tight_loop_contents()`, `rp2040.getFreeHeap()`, `reset_usb_boot()`, `pico_get_unique_board_id()`.
**impl/.mock:** time driven by mock helpers; `hal_watchdog_caused_reboot`, `hal_get_free_heap`, chip temperature, and the device UID are injectable. `hal_enter_bootloader()` sets an observable flag instead of rebooting.
**Thread safety:** Arduino backend: time/watchdog APIs are safe to call from both cores. `hal_delay_ms` / `hal_delay_us` block only the calling core and can be used concurrently. Mock backend uses shared unsynchronized state and is intended for single-threaded tests.
**Note:** `COUNTOF(arr)` works only with statically-allocated arrays (not pointers).
**Note:** `NONULL(x)` is a null-pointer guard for functions that use a shared
`error:` cleanup path. Uses `NULL` (safe in both C and C++ translation units).
If `x == NULL`, it performs `goto error;`. The surrounding function must define
an `error:` label.

**Mock helpers:**
```c
void hal_mock_set_millis(uint32_t ms);
void hal_mock_advance_millis(uint32_t ms);
void hal_mock_set_micros(uint32_t us);
void hal_mock_advance_micros(uint32_t us);
bool hal_mock_watchdog_was_fed(void);
void hal_mock_watchdog_reset_flag(void);
void hal_mock_set_caused_reboot(bool val);
void hal_mock_set_free_heap(uint32_t bytes);  // default: 256 KB
void hal_mock_set_chip_temp(float celsius);   // default: 25.0 °C
bool hal_mock_bootloader_was_requested(void);
void hal_mock_bootloader_reset_flag(void);
void hal_mock_set_device_uid(const uint8_t uid[8]);  // override UID
void hal_mock_reset_device_uid(void);                // restore default E661A4D1234567AB
```

**Device UID details:**
- `hal_get_device_uid(uid)` fills an exactly 8-byte output buffer. Passing
  `NULL` is a safe no-op.
- `hal_get_device_uid_hex(buf, buflen)` writes 16 uppercase hex characters
  followed by a NUL terminator (17 bytes total). Returns `false` and writes
  nothing when `buf` is `NULL` or `buflen < HAL_DEVICE_UID_HEX_BUF_SIZE`.
- On RP2040 hardware the source is the 64-bit unique identifier stored in
  the external QSPI flash chip, read via `pico_get_unique_board_id()`.
  This identifier is persistent across reboots, unique per device, and the
  same value that arduino-pico exposes as USB iSerialNumber.
- In the mock backend the default value is deterministic
  (`0xE6 0x61 0xA4 0xD1 0x23 0x45 0x67 0xAB` → `"E661A4D1234567AB"`) so
  tests that compare the UID string can hard-code the expected value.
  Use `hal_mock_set_device_uid()` to simulate a second board.

---

## `hal_bits` - Bit helpers

```c
#include <hal/hal_bits.h>

#define is_set(x, mask)      ...
#define set_bit(var, mask)   ...
#define clr_bit(var, mask)   ...
#define bitSet(var, bit)     ...
#define bitClear(var, bit)   ...
#define bitRead(var, bit)    ...
#define set_bit_v(reg, mask) ...
#define clr_bit_v(reg, mask) ...
```

**Note:** All helpers are macros (type-width independent). Avoid passing expressions with side effects (`i++`, stateful function calls), because arguments may be evaluated more than once. `bitSet/bitClear/bitRead` remain guarded with `#ifndef` for Arduino compatibility.
**Thread safety:** Stateless helpers; thread-safe by themselves. When multiple contexts touch the same variable/register, synchronization is the caller's responsibility.

---

## `hal_crypto` - Base64, MD5, SHA-256 / HMAC-SHA256, ChaCha20, ChaCha20-Poly1305  *(opt-in - `HAL_ENABLE_CRYPTO`)*

This module is **opt-in**. Define `HAL_ENABLE_CRYPTO` in
`hal_project_config.h` (or via `-D`) to compile it in. Without the
flag the header below expands to nothing, `hal_crypto.cpp` becomes an
empty translation unit, and any caller of these helpers fails at link
time with an undefined-reference error.

```c
#include <hal/hal_crypto.h>

// ChaCha20 / AEAD constants
#define HAL_CHACHA20_KEY_BYTES            32u
#define HAL_CHACHA20_NONCE_BYTES          12u
#define HAL_CHACHA20_BLOCK_BYTES          64u
#define HAL_CHACHA20_POLY1305_TAG_BYTES   16u

// MD5 constants
#define HAL_MD5_DIGEST_BYTES              16u
#define HAL_MD5_HEX_BUF_SIZE              33u  // 32 hex chars + NUL

// SHA-256 / HMAC-SHA256 constants
#define HAL_SHA256_DIGEST_BYTES           32u
#define HAL_SHA256_HEX_BUF_SIZE           65u  // 64 hex chars + NUL
#define HAL_HMAC_SHA256_BLOCK_BYTES       64u

// Base64 helpers
size_t hal_base64_encoded_len(size_t input_len);
size_t hal_base64_decoded_max_len(size_t input_len);
bool hal_base64_encode(const uint8_t *input, size_t input_len,
                       char *output, size_t out_size, size_t *out_len);
bool hal_base64_decode(const char *input, size_t input_len,
                       uint8_t *output, size_t out_size, size_t *out_len);

// MD5 helpers
bool hal_md5(const uint8_t *input, size_t input_len,
             uint8_t out_digest[HAL_MD5_DIGEST_BYTES]);
bool hal_md5_hex(const uint8_t *input, size_t input_len,
                 char *output, size_t out_size);

// ChaCha20 stream helpers (IETF RFC 8439)
bool hal_chacha20_block(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                        uint32_t counter,
                        const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                        uint8_t out_block[HAL_CHACHA20_BLOCK_BYTES]);
bool hal_chacha20_xor(const uint8_t key[HAL_CHACHA20_KEY_BYTES],
                      uint32_t counter,
                      const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
                      const uint8_t *input,
                      size_t input_len,
                      uint8_t *output);

// ChaCha20-Poly1305 AEAD (RFC 8439)
bool hal_chacha20_poly1305_encrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *plaintext, size_t text_len,
    uint8_t *ciphertext,
    uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES]);

bool hal_chacha20_poly1305_decrypt(
    const uint8_t key[HAL_CHACHA20_KEY_BYTES],
    const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES],
    const uint8_t *aad, size_t aad_len,
    const uint8_t *ciphertext, size_t text_len,
    const uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES],
    uint8_t *plaintext);

// SHA-256 / HMAC-SHA256 (FIPS 180-4 + RFC 2104)
bool hal_sha256(const uint8_t *input, size_t input_len,
                uint8_t out_digest[HAL_SHA256_DIGEST_BYTES]);
bool hal_sha256_hex(const uint8_t *input, size_t input_len,
                    char *output, size_t out_size);
bool hal_hmac_sha256(const uint8_t *key, size_t key_len,
                     const uint8_t *message, size_t message_len,
                     uint8_t out_mac[HAL_SHA256_DIGEST_BYTES]);
bool hal_hmac_sha256_hex(const uint8_t *key, size_t key_len,
                         const uint8_t *message, size_t message_len,
                         char *output, size_t out_size);
```

**Behavior notes:**
- Base64 is strict RFC 4648 (`A-Z a-z 0-9 + /` and `=` padding), no whitespace tolerance.
- `hal_md5_hex(...)` and `hal_sha256_hex(...)` / `hal_hmac_sha256_hex(...)` output lowercase hex.
- `hal_chacha20_xor(...)` supports in-place processing (`output == input`).
- `hal_chacha20_poly1305_decrypt(...)` verifies tag before decryption and returns `false` on mismatch.
- For ChaCha20/AEAD, nonce must be unique per key; nonce reuse breaks security.
- `hal_hmac_sha256(...)` follows RFC 2104 — keys longer than the block size (64 B) are pre-hashed; shorter keys are zero-padded.
- SHA-256 / HMAC-SHA256 are validated against FIPS 180-2 and RFC 4231 vectors and stay bit-stable with the host-side mirror in SerialConfigurator (`sc_sha256.c`).

**Security note:** MD5 is provided for legacy checksum compatibility and non-security fingerprints. Do not use MD5 where collision resistance is required. Prefer SHA-256 / HMAC-SHA256 for any new integrity or authentication need.

**Thread safety:** Stateless implementation; safe for multicore use when caller-provided buffers do not alias across threads unexpectedly.

---

## `hal_sync` - Mutex

```c
#include <hal/hal_sync.h>

typedef hal_mutex_impl_t* hal_mutex_t;   // opaque

hal_mutex_t hal_mutex_create(void);
void        hal_mutex_lock(hal_mutex_t mutex);
void        hal_mutex_unlock(hal_mutex_t mutex);
void        hal_mutex_destroy(hal_mutex_t mutex);
```

**impl/arduino:** pico SDK `mutex_t` - synchronizes core0/core1.
**impl/.mock:** `std::mutex`.
**Note:** Not FreeRTOS-safe. If FreeRTOS is needed, provide `impl/freertos/hal_sync.cpp` using `xSemaphoreCreateMutex`.

### Macros (tools.h)

```c
m_mutex_def(name)            // static hal_mutex_t name = NULL
m_mutex_init(name)           // name = hal_mutex_create()
m_mutex_enter_blocking(name) // hal_mutex_lock(name)
m_mutex_exit(name)           // hal_mutex_unlock(name)
```

### Critical section (ISR-safe)

```c
void hal_critical_section_enter(void);  // disable interrupts
void hal_critical_section_exit(void);   // re-enable interrupts
```

**impl/arduino:** `noInterrupts()` / `interrupts()` (Arduino-pico).
**impl/.mock:** no-ops.
**Note:** Not re-entrant. Use only for short, non-nested critical sections protecting ISR-shared data.

---

## `hal_serial` - Serial & debug output

```c
#include <hal/hal_serial.h>

// Configurable buffer sizes (define before including if needed):
// #define HAL_DEBUG_BUF_SIZE    512
// #define HAL_DEBUG_PREFIX_SIZE  16
// #define HAL_DEBUG_DEFAULT_BAUD 9600   // used by lazy init
// #define HAL_DEBUG_RATE_LIMIT_SOURCES_MAX 16
// #define HAL_DEBUG_RATE_LIMIT_SOURCE_NAME_MAX 24

typedef struct {
    uint16_t full_logs_limit;   // default 5
    uint32_t min_gap_ms;        // default 1000 ms
    uint32_t summary_every_ms;  // default 30000 ms
} hal_debug_rate_limit_t;

void hal_serial_begin(uint32_t baud);
void hal_serial_print(const char *s);
void hal_serial_println(const char *s);
int  hal_serial_available(void);   // bytes waiting in RX buffer
int  hal_serial_read(void);        // read one byte, or -1 if empty

hal_debug_rate_limit_t hal_debug_rate_limit_defaults(void);
const hal_debug_rate_limit_t *hal_debug_get_rate_limit(void);

void hal_debug_init(uint32_t baud, const hal_debug_rate_limit_t *cfg = 0);
// cfg == 0 -> defaults

bool hal_deb_is_initialized(void);        // query init state
void hal_deb_set_prefix(const char *prefix);
void hal_deb(const char *format, ...);    // printf-style, thread-safe, with prefix
void hal_derr(const char *format, ...);   // same but prefixes "ERROR! "
void hal_derr_limited(const char *source, const char *format, ...);
// source is caller-defined free-form tag (e.g. "gps", "can"); 0 -> "global"
void hal_deb_hex(const char *prefix, const uint8_t *buf, int len, int maxBytes);
// logs: "<prefix> len=<n> bytes: XX XX ...", maxBytes is clamped to 1..48
```

### Lazy initialisation

`hal_deb()` and `hal_derr()` use **lazy init** - if `hal_debug_init()` has not been called
before the first debug print, it is invoked automatically with `HAL_DEBUG_DEFAULT_BAUD`
(default 9600, overridable via `-D`). Calling `debugInit()` is no longer mandatory.

`hal_derr_limited()` reuses the same lazy init and applies global rate-limit config per
error source tag (`source`) so errors from different modules do not suppress each other.

Limiter implementation details:
- source matching uses `hash + source string` (collision-safe lookup)
- limiter state is protected by an internal mutex (thread-safe)
- when `HAL_DEBUG_RATE_LIMIT_SOURCES_MAX` is exhausted, new sources are grouped into
    an internal `overflow` bucket instead of reusing unrelated source state

**Debug helpers in `tools.h` / `tools_c.h`:**
```c
void  debugInit(void);                          // wrapper around hal_debug_init(HAL_DEBUG_DEFAULT_BAUD)
void  setDebugPrefixWithColon(const char *moduleName); // appends ':' and forwards to hal_deb_set_prefix()

#define deb            hal_deb
#define derr           hal_derr
#define derr_limited   hal_derr_limited
#define setDebugPrefix hal_deb_set_prefix
```

`setDebugPrefixWithColon(...)` truncates the module name if needed so the
generated `<module>:` prefix always fits inside `HAL_DEBUG_PREFIX_SIZE`.

**impl/arduino:** Arduino `Serial`.
**impl/.mock:** `printf`; last line injectable via `hal_mock_deb_last_line()`.
RX input injectable via `hal_mock_serial_inject_rx(data, len)` for testing
`hal_serial_available()` / `hal_serial_read()`.

### Error Handling Policy

- `HAL_ASSERT(...)` is used for critical programming errors in core primitives
    (e.g. NULL mutex in sync paths).
- Soft validation + error log is used for noncritical runtime misuse in peripheral
    APIs where continuing execution is acceptable.
- `hal_derr(...)` prints every error (no suppression).
- `hal_derr_limited(source, ...)` should be preferred for potentially repetitive
    noncritical errors to avoid log flooding.

---

## `hal_serial_session` - Framed serial session helper

```c
#include <hal/hal_serial_session.h>

#define HAL_SERIAL_SESSION_PROTOCOL_VERSION 1u
#define HAL_SERIAL_SESSION_MAX_LINE         128u
#define HAL_SERIAL_SESSION_UNKNOWN          "unknown"

typedef void (*hal_serial_session_unknown_cb_t)(const char *line, void *user);

typedef struct {
    bool        active;
    uint32_t    session_id;
    uint32_t    hello_counter;
    uint32_t    last_activity_ms;
    uint8_t     line_len;
    char        line[HAL_SERIAL_SESSION_MAX_LINE + 1u];
    const char *module_tag;   // bound at init
    const char *fw_version;   // bound at init (may be NULL → "unknown")
    const char *build_id;     // bound at init (may be NULL → "unknown")
    uint8_t     uid_bytes[HAL_DEVICE_UID_BYTES];       // captured at init (auth)
    char        uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE];  // captured at init
    hal_serial_session_unknown_cb_t unknown_handler;   // optional sink
    void       *unknown_user;
    bool        in_request;   // gates `hal_serial_session_println`
    uint16_t    request_seq;  // seq echoed in framed replies
    // Authentication state (Phase 3)
    bool        authenticated;
    bool        challenge_pending;
    uint8_t     challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    uint32_t    auth_counter;
    uint32_t    auth_failures;
} hal_serial_session_t;

void     hal_serial_session_init(hal_serial_session_t *session,
                                 const char *module_tag,
                                 const char *fw_version,
                                 const char *build_id);
void     hal_serial_session_set_unknown_handler(hal_serial_session_t *s,
                                                hal_serial_session_unknown_cb_t cb,
                                                void *user);
bool     hal_serial_session_is_active(const hal_serial_session_t *session);
bool     hal_serial_session_is_authenticated(const hal_serial_session_t *session);
uint32_t hal_serial_session_id(const hal_serial_session_t *session);
void     hal_serial_session_poll(hal_serial_session_t *session);
void     hal_serial_session_println(hal_serial_session_t *session,
                                    const char *payload);
```

Wire protocol (both directions):

    $SC,<seq>,<inner>*<crc8>\n

See [`hal_serial_frame`](#hal_serial_frame---wire-framing-helpers) for the
frame codec.

Built-in commands (inside the frame envelope):
- `HELLO` — activates the session and reports identity.
- `SC_AUTH_BEGIN` — mints a fresh 16-byte challenge for the active session.
- `SC_AUTH_PROVE <64 hex chars>` — proves the host knows `K_device` for
  this UID; on success the session becomes authenticated.

Built-in responses (inside the frame envelope, each echoes the inbound `<seq>`):
- `OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>`
- `SC_OK AUTH_CHALLENGE <32 hex chars>` (after `SC_AUTH_BEGIN`)
- `SC_OK AUTH_OK` (after `SC_AUTH_PROVE` with the correct MAC)
- `SC_AUTH_FAILED <reason>` — `bad_mac`, `bad_length`, `bad_hex`,
  `no_challenge`, `key_derivation`, `mac_compute`
- `SC_NOT_READY HELLO_REQUIRED` (AUTH_BEGIN/PROVE before HELLO)

Unrecognised inner payloads:
- if a user callback is registered via
  `hal_serial_session_set_unknown_handler`, it receives the unwrapped
  inner line and is responsible for any reply (use
  `hal_serial_session_println` so the reply inherits the request's `<seq>`).
- otherwise the helper emits `SC_UNKNOWN_CMD` (still framed).

Non-framed input is silently dropped — there is no plain-text
fall-through. This is intentional: the SerialConfigurator host always
frames its requests, and removing the legacy path eliminates substring
mismatches against debug-log lines.

Identity binding model:
- `module_tag` must not be NULL and must reference a string with static
  lifetime (typically the module's compile-time `MODULE_NAME`).
- `fw_version` and `build_id` may be NULL or empty at init; both are reported
  as `unknown` in that case. When non-NULL, they are captured by pointer and
  must likewise remain valid for the lifetime of the session.
- The device UID hex string is captured by value at init via
  `hal_get_device_uid_hex()` and stored inside the session struct.
- All identity is immutable after init; `hal_serial_session_poll()` takes no
  identity arguments.

Reply gating:
- `hal_serial_session_println` is a no-op outside the request-dispatch
  window (`session->in_request == false`). Modules cannot accidentally
  inject unsolicited bytes into the framed stream; if you need to send
  state asynchronously, do it from the unknown-handler callback in
  response to a request.

Authentication (Phase 3) — opt-in:
- The whole AUTH path is compiled in only when `HAL_ENABLE_CRYPTO` is
  defined. Without it the session struct loses the auth fields, the
  `SC_AUTH_*` handlers are not dispatched, and
  `hal_serial_session_is_authenticated()` always returns false. The
  rest of the framed session (HELLO + module-defined SC_* commands)
  is unaffected.
- See [`hal_sc_auth`](#hal_sc_auth---serialconfigurator-authentication-helper--opt-in---hal_enable_crypto)
  for the salt + key-derivation primitives.
- `SC_AUTH_BEGIN` requires an active (HELLO-acknowledged) session and
  mints a fresh challenge derived from
  `SHA-256(uid || hal_micros64() || session_id || hello_counter || auth_counter)`,
  taking the first 16 bytes.
- `SC_AUTH_PROVE` is one-shot per challenge: success or failure both
  consume the pending challenge, so a captured valid response cannot be
  replayed against the same `SC_AUTH_BEGIN`.
- A new HELLO mints a new `session_id` and clears `authenticated` /
  `challenge_pending`. Module code that gates sensitive operations must
  re-check `hal_serial_session_is_authenticated(session)` after every
  command, not just once.
- `auth_failures` counts failed `SC_AUTH_PROVE` attempts; rate-limit and
  lockout policies on top of it are deferred to Phase 7.

Notes:
- parser is line-based (`\r` / `\n` terminate a frame),
- helpers are header-only (`static inline`) and state lives in `hal_serial_session_t`,
- session id is non-cryptographic and intended for bootstrap tracking only,
- the HELLO inner-payload buffer is sized for the six mandatory fields plus
  reasonable slack; the implementation uses a 192-byte internal buffer.

Typical wiring (firmware module):
```c
#include <hal/hal_serial_session.h>

static hal_serial_session_t s_session;

static void on_unknown(const char *inner, void *user) {
    (void)user;
    if (strcmp(inner, "SC_GET_META") == 0) {
        hal_serial_session_println(&s_session, "SC_OK META ...");
    }
}

void configSessionInit(void) {
    hal_serial_session_init(&s_session, MODULE_NAME, FW_VERSION, BUILD_ID);
    hal_serial_session_set_unknown_handler(&s_session, on_unknown, NULL);
}

void configSessionTick(void) {
    hal_serial_session_poll(&s_session);
}
```

Test observability (mock backend):
- Build a framed request with `hal_serial_frame_encode(seq, "HELLO", buf,
  sizeof(buf), NULL)`, append `\n`, and feed it via
  `hal_mock_serial_inject_rx(buf, -1)`.
- Inspect `hal_mock_serial_last_line()` and decode it with
  `hal_serial_frame_decode(...)` to assert HELLO response fields
  (`module=`, `proto=`, `session=`, `fw=`, `build=`, `uid=`) and that the
  reply seq matches the request seq.
- Use `hal_mock_set_device_uid(...)` to simulate a different physical board
  when asserting `uid=` values.

---

## `hal_serial_frame` - Wire framing helpers

```c
#include <hal/hal_serial_frame.h>

#define HAL_SERIAL_FRAME_PREFIX        "$SC,"
#define HAL_SERIAL_FRAME_PREFIX_LEN    4u
#define HAL_SERIAL_FRAME_PAYLOAD_MAX   256u
#define HAL_SERIAL_FRAME_LINE_MAX      (HAL_SERIAL_FRAME_PAYLOAD_MAX + 32u)

uint8_t hal_serial_frame_crc8(const uint8_t *data, size_t len);

bool    hal_serial_frame_encode(uint16_t seq,
                                const char *payload,
                                char *out, size_t out_size,
                                size_t *out_len);

bool    hal_serial_frame_decode(const char *line,
                                uint16_t *seq_out,
                                char *payload_out,
                                size_t payload_out_size);
```

Frame format (both directions):

    $SC,<seq>,<payload>*<crc8>\n

- Literal start sentinel `$SC,`.
- `<seq>`: ASCII unsigned decimal in `[0..65535]`. The response always
  echoes the request's seq so the host can correlate.
- `<payload>`: free-form ASCII text. Must not contain `*`, `\r` or `\n`.
- `<crc8>`: two uppercase hex digits. CRC-8/CCITT (poly `0x07`, init
  `0x00`, no reflect, no xor-out) over the bytes between (but excluding)
  the leading `$` and the `*` separator. Reference vector:
  `"123456789" → 0xF4`.
- `\n` line terminator (encode helpers do **not** append it; use
  `hal_serial_println()` which already does).

This header is byte-for-byte compatible with the host-side mirror at
`Fiesta/src/SerialConfigurator/src/core/sc_frame.h`. Do not change one
without updating the other; both sides assert the same CRC reference
vector in their test suites.

---

## `hal_sc_auth` - SerialConfigurator authentication helper  *(opt-in - `HAL_ENABLE_CRYPTO`)*

Pulled in by the same `HAL_ENABLE_CRYPTO` flag as `hal_crypto`. The
module depends on `hal_hmac_sha256`, so enabling auth without crypto
is not a meaningful configuration. When the flag is off
`hal_serial_session` keeps working — the `SC_AUTH_BEGIN` /
`SC_AUTH_PROVE` handlers are compiled out and
`hal_serial_session_is_authenticated()` returns `false`.

```c
#include <hal/hal_sc_auth.h>

#define HAL_SC_AUTH_SCHEME_TAG          "FIESTA-SC-AUTH-v1"
#define HAL_SC_AUTH_SCHEME_TAG_LEN      17u
#define HAL_SC_AUTH_SALT                ((const uint8_t *)HAL_SC_AUTH_SCHEME_TAG)
#define HAL_SC_AUTH_SALT_LEN            HAL_SC_AUTH_SCHEME_TAG_LEN
#define HAL_SC_AUTH_KEY_BYTES           HAL_SHA256_DIGEST_BYTES   // 32
#define HAL_SC_AUTH_CHALLENGE_BYTES     16u
#define HAL_SC_AUTH_CHALLENGE_HEX_BUF_SIZE  33u                   // 32 hex + NUL
#define HAL_SC_AUTH_RESPONSE_BYTES      HAL_SHA256_DIGEST_BYTES   // 32
#define HAL_SC_AUTH_RESPONSE_HEX_BUF_SIZE   HAL_SHA256_HEX_BUF_SIZE

bool hal_sc_auth_derive_device_key(
    const uint8_t *uid, size_t uid_len,
    uint8_t out_key[HAL_SC_AUTH_KEY_BYTES]);

bool hal_sc_auth_compute_response(
    const uint8_t device_key[HAL_SC_AUTH_KEY_BYTES],
    const uint8_t *challenge, size_t challenge_len,
    uint32_t session_id,
    uint8_t out_response[HAL_SC_AUTH_RESPONSE_BYTES]);

bool hal_sc_auth_macs_equal(const uint8_t *a, const uint8_t *b, size_t len);
```

Constructions:

- `K_device  = HMAC-SHA256(key=salt, message=uid_bytes)`
- `response  = HMAC-SHA256(key=K_device, message=challenge || session_id_be32)`

The session id is serialised big-endian via `hal_u32_to_bytes_be` so the
firmware and host MAC the exact same byte sequence regardless of host
endianness.

`hal_sc_auth_macs_equal` is a constant-time OR-accumulator comparison
intended for MAC verification — it does not short-circuit on first
difference, so timing does not leak where the bytes diverged.

The salt is a public, project-wide compile-time constant. Secrecy of the
scheme rests on HMAC-SHA256 + the per-device UID, **not** on salt
secrecy. Treating the salt as confidential would only obscure design
intent.

This header is byte-for-byte compatible with the host-side mirror at
`Fiesta/src/SerialConfigurator/src/core/sc_auth.{h,c}`. Both sides ship
their own test for key derivation and response MAC computation; the
host suite includes a hand-anchored cross-vector check that recomputes
the MAC from `sc_crypto` primitives, so any divergence between the two
copies surfaces as a test failure rather than a runtime AUTH_FAILED on
the bench.

The handshake itself is wired in
[`hal_serial_session`](#hal_serial_session---framed-serial-session-helper)
as built-in `SC_AUTH_BEGIN` / `SC_AUTH_PROVE` commands; modules consume
authentication state through `hal_serial_session_is_authenticated(...)`
and do not need to call the helpers below directly.

---

## `hal_can` - CAN bus  *(optional - `HAL_DISABLE_CAN`)*

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

**impl/arduino:** MCP2515 via bundled `drivers/MCP2515/mcp_can`.
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

## `hal_display` - TFT / OLED display  *(optional - `HAL_DISABLE_DISPLAY`)*

Supports SPI TFT displays (ILI9341, ST7789, ST7735, ST7796S) and SSD1306 OLED over I2C.

```c
// Define ONE of these before including hal_display.h (or in build flags):
#define HAL_DISPLAY_ILI9341
#define HAL_DISPLAY_ST7789
#define HAL_DISPLAY_ST7735
#define HAL_DISPLAY_ST7796S

// Optional per-driver excludes:
// #define HAL_DISABLE_ILI9341
// #define HAL_DISABLE_ST7789
// #define HAL_DISABLE_ST7735
// #define HAL_DISABLE_ST7796S
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

// ILI9341 only: re-send extended register-init sequence (post power-on glitch fix).
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
**impl/arduino:** Adafruit driver selected by compile-time define + `Adafruit_GFX` + `Adafruit_SSD1306`. Fonts: `FreeSansBold9pt7b`, `FreeSerif9pt7b`.
**impl/.mock:** deterministic host mock with inspectable state for tests.
**Thread safety:** Arduino backend: thread-safe and multicore-safe. An internal `hal_mutex_t` serializes all display operations automatically. Mock backend is unsynchronized and intended for single-threaded tests.

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

## `hal_spi` - SPI bus init

```c
#include <hal/hal_spi.h>

// Configure pins and start the SPI bus in master mode.
// bus: 0 = SPI (default), 1 = SPI1 (second controller, RP2040)
void hal_spi_init(uint8_t bus, uint8_t rx_pin, uint8_t tx_pin, uint8_t sck_pin);

// Optional runtime synchronization for shared SPI buses.
void hal_spi_lock(uint8_t bus);
void hal_spi_unlock(uint8_t bus);
```

**impl/arduino:** Arduino-pico `SPI` / `SPI1`; per-bus mutex for `hal_spi_lock()` / `hal_spi_unlock()`.
**impl/.mock:** stores init parameters and lock-depth counters for tests.
**Thread safety:** Arduino backend provides multicore-safe per-bus locking via `hal_spi_lock()` / `hal_spi_unlock()`. `hal_spi_init()` reconfigures shared bus objects and should be called during single-core setup. Mock backend does not provide real cross-thread synchronization.
**Note:** `hal_spi_init()` is usually called once during setup. For shared-bus access at runtime (multiple modules/tasks/cores), guard SPI transactions with `hal_spi_lock()`/`hal_spi_unlock()`.

---

## `hal_i2c` - I2C bus  *(optional - `HAL_DISABLE_I2C`)*

```c
#include <hal/hal_i2c.h>

// Init bus, set clock, start Wire/Wire1 (mutex is lazy-initialized on use)
void    hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz);
void    hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz); // bus: 0=Wire, 1=Wire1
void    hal_i2c_deinit(void);
void    hal_i2c_deinit_bus(uint8_t bus);

// Manual lock/unlock - use when wrapping a third-party library that calls Wire directly
void    hal_i2c_lock(void);
void    hal_i2c_unlock(void);
void    hal_i2c_lock_bus(uint8_t bus);
void    hal_i2c_unlock_bus(uint8_t bus);

// Transaction primitives (begin/write/end acquire/release the mutex automatically)
void    hal_i2c_begin_transmission(uint8_t address);
size_t  hal_i2c_write(uint8_t data);        // returns 1 on success, 0 on failure
uint8_t hal_i2c_end_transmission(void);     // returns 0 on success, non-zero on error
void    hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address);
size_t  hal_i2c_write_bus(uint8_t bus, uint8_t data);
uint8_t hal_i2c_end_transmission_bus(uint8_t bus);

// One-shot "begin + write one byte + end" helper (acquires/releases mutex internally).
// *outWriteOk (optional) receives the hal_i2c_write() queued-bytes status.
// Return value is the end_transmission status (0 on success).
uint8_t hal_i2c_write_byte(uint8_t address, uint8_t data, bool *outWriteOk);
uint8_t hal_i2c_write_byte_bus(uint8_t bus, uint8_t address, uint8_t data, bool *outWriteOk);

// Symmetric one-shot "request + read 1 byte" helper.
// The internal mutex is held across the full request+read sequence.
// *outReadOk (optional) receives true when exactly one byte was received.
// Returns the byte read, or 0 on failure - inspect *outReadOk to distinguish
// a genuine 0x00 from a communication error.
uint8_t hal_i2c_read_byte(uint8_t address, bool *outReadOk);
uint8_t hal_i2c_read_byte_bus(uint8_t bus, uint8_t address, bool *outReadOk);

// Request + read (acquires/releases mutex around the request; read is unlocked)
uint8_t hal_i2c_request_from(uint8_t address, uint8_t count);  // returns bytes received
int     hal_i2c_available(void);    // bytes in receive buffer
int     hal_i2c_read(void);         // one byte, or -1 if empty
uint8_t hal_i2c_request_from_bus(uint8_t bus, uint8_t address, uint8_t count);
int     hal_i2c_available_bus(uint8_t bus);
int     hal_i2c_read_bus(uint8_t bus);

// Transaction counter - counts completed write (end_transmission) and read
// (request_from) transactions since init. Resets on init. Wraps at UINT32_MAX.
uint32_t hal_i2c_get_transaction_count(void);
uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus);

// Device-busy probe - send address, check ACK/NACK immediately.
// Returns true if the device did NOT ACK (busy or absent).
// Typical use: poll after an AT24C256 write until the chip is ready.
bool    hal_i2c_is_busy(uint8_t address);
bool    hal_i2c_is_busy_bus(uint8_t bus, uint8_t address);

// I2C bus clear (per I2C specification §3.1.16).
// Toggles SCL up to 9 times at GPIO level to release a slave holding SDA
// low (e.g. after master reset mid-transaction), then generates a STOP
// condition.  Leaves SDA/SCL as inputs with pull-ups.
// Must be called BEFORE hal_i2c_init() - the bus is not usable for Wire
// transactions during this procedure.
void    hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin);
void    hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin);
```

**Init behavior:** The I2C mutex is created lazily on first lock/transfer call.
`hal_i2c_init*()` configures SDA/SCL, clock and starts `Wire`/`Wire1`, and should
still be called during setup before normal I2C traffic.

**impl/arduino:** Arduino-pico `Wire.h` / `Wire1`; per-bus mutex guards all transactions. `hal_i2c_bus_clear()` uses native Arduino GPIO primitives (`pinMode`, `digitalWrite`, `digitalRead`, `delayMicroseconds`).
**impl/.mock:** ring buffer; injectable via mock helpers. `hal_i2c_end_transmission()` returns 2 (NACK) when the mock busy flag is set, 0 otherwise. `hal_i2c_bus_clear()` increments an internal counter (query via `hal_mock_i2c_get_bus_clear_count()`); counter resets on `hal_i2c_init()`.
**Thread safety:** Arduino backend: transfer APIs are thread-safe and multicore-safe. An internal per-bus `hal_mutex_t` serializes transactions; use `hal_i2c_lock` / `hal_i2c_unlock` to extend critical regions around third-party `Wire` calls. `hal_i2c_init*()` / `hal_i2c_deinit*()` reconfigure shared bus objects and should be called from one core during setup/teardown. Mock backend does not synchronize concurrent access.

**Mock helpers:**
```c
void    hal_mock_i2c_inject_rx(const uint8_t *data, int len);                    // pre-load receive buffer on bus 0
void    hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len);   // pre-load receive buffer on selected bus
uint8_t hal_mock_i2c_get_last_addr(void);                                         // last address on bus 0
uint8_t hal_mock_i2c_get_last_addr_bus(uint8_t bus);                              // last address on selected bus
int     hal_mock_i2c_get_lock_depth(void);                                        // current lock depth on bus 0
int     hal_mock_i2c_get_lock_depth_bus(uint8_t bus);                             // current lock depth on selected bus
int     hal_mock_i2c_get_read_byte_lock_depth(void);                              // lock depth captured at the byte-read point in hal_i2c_read_byte() on bus 0
int     hal_mock_i2c_get_read_byte_lock_depth_bus(uint8_t bus);                   // lock depth captured at the byte-read point in hal_i2c_read_byte_bus() on selected bus
bool    hal_mock_i2c_is_initialized(void);                                        // init state for bus 0
bool    hal_mock_i2c_is_initialized_bus(uint8_t bus);                             // init state for selected bus
void    hal_mock_i2c_set_busy(bool busy);                                         // control hal_i2c_is_busy() + end_transmission NACK on bus 0
void    hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy);                        // control hal_i2c_is_busy() + end_transmission NACK on selected bus
uint32_t hal_mock_i2c_get_bus_clear_count(void);                                  // number of bus_clear calls on bus 0
uint32_t hal_mock_i2c_get_bus_clear_count_bus(uint8_t bus);                       // number of bus_clear calls on selected bus
```

**Example - PCF8574 8-bit I/O expander using the one-shot helpers:**

PCF8574 is addressed once and has no register layout: a single write byte
drives all 8 output latches; a single read byte returns the current port
value. Using `hal_i2c_write_byte()` and `hal_i2c_read_byte()` keeps the
driver code free of explicit begin/write/end or request/read sequences.

```c
#include <hal/hal_i2c.h>

#define PCF8574_ADDR 0x38   // 7-bit address (A2..A0 = 0)

static uint8_t s_portLatch;  // shadow of the 8 output bits

/** Initialize the expander to all-zero outputs. */
bool pcf8574_init(void) {
    s_portLatch = 0x00;
    bool writeOk = false;
    uint8_t endTx = hal_i2c_write_byte(PCF8574_ADDR, s_portLatch, &writeOk);
    return writeOk && (endTx == 0);
}

/** Drive one output pin (0..7). */
bool pcf8574_write_pin(uint8_t pin, bool high) {
    if (pin > 7) return false;
    if (high) s_portLatch |=  (uint8_t)(1u << pin);
    else      s_portLatch &= (uint8_t)~(1u << pin);

    bool writeOk = false;
    uint8_t endTx = hal_i2c_write_byte(PCF8574_ADDR, s_portLatch, &writeOk);
    return writeOk && (endTx == 0);
}

/** Sample one input pin (0..7). Returns false on I2C error too. */
bool pcf8574_read_pin(uint8_t pin) {
    if (pin > 7) return false;
    bool readOk = false;
    uint8_t port = hal_i2c_read_byte(PCF8574_ADDR, &readOk);
    if (!readOk) return false;
    return (port & (uint8_t)(1u << pin)) != 0;
}
```

Note: the helpers rely on the HAL's *internal* per-bus mutex, which covers
a single begin/end pair. Code that interleaves a write-then-read against
another multi-step transaction on the same bus (e.g. set register pointer
→ request N bytes) must serialize the two sequences with a caller-owned
mutex in addition, since the HAL mutex is released at each `end_transmission`.

---

## `hal_i2c_slave` - I2C slave/target with register map  *(optional - `HAL_DISABLE_I2C_SLAVE`)*

Exposes a fixed-size register map over I2C slave mode. A remote master writes
a one-byte register pointer, then reads N bytes starting from that address.
The register pointer auto-increments on each byte read.

This is independent of the I2C master module (`hal_i2c`) - both can be
disabled/enabled separately, but they cannot share the same bus simultaneously.

```c
#include <hal/hal_i2c_slave.h>

// Default register map size (override in hal_project_config.h)
#ifndef HAL_I2C_SLAVE_REG_MAP_SIZE
#define HAL_I2C_SLAVE_REG_MAP_SIZE 32U
#endif

// Init / deinit
void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address);
void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin, uint8_t address);
void hal_i2c_slave_deinit(void);
void hal_i2c_slave_deinit_bus(uint8_t bus);

// Write to register map (application → slave buffer).
// Out-of-range registers are silently ignored.
void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value);
void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value);   // big-endian: MSB at reg, LSB at reg+1
void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value);

// Read from register map
uint8_t  hal_i2c_slave_reg_read8(uint8_t reg);
uint8_t  hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg);
uint16_t hal_i2c_slave_reg_read16(uint8_t reg);
uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg);

// Query address
uint8_t hal_i2c_slave_get_address(void);
uint8_t hal_i2c_slave_get_address_bus(uint8_t bus);

// Transaction counter - counts completed master reads and writes since init.
// Useful for detecting live bus activity without polling reg_write return values.
// Resets on init. Wraps at UINT32_MAX. Thread-safe (atomic).
uint32_t hal_i2c_slave_get_transaction_count(void);
uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus);
```

**Register map protocol (I2C):**
1. Master writes: `[reg_address]` - sets the register pointer
2. Master reads N bytes - slave responds with `regs[ptr], regs[ptr+1], ...`
3. Master writes: `[reg_address, data0, data1, ...]` - sets pointer, then writes data sequentially

**impl/arduino:** Arduino-pico `Wire.h` / `Wire1` in slave mode (`Wire.begin(address)`).  `onReceive` / `onRequest` callbacks handle the register-map protocol.
**impl/.mock:** direct register-map access; simulation helpers for master write/read.
**Thread safety:** Arduino backend: `reg_write*` / `reg_read*` are thread-safe and multicore-safe (internal per-bus mutex). `init` / `deinit` must be called from one core during setup/teardown. Mock backend does not synchronize concurrent access.

**Mock helpers:**
```c
bool    hal_mock_i2c_slave_is_initialized(void);                                       // init state for bus 0
bool    hal_mock_i2c_slave_is_initialized_bus(uint8_t bus);
uint8_t hal_mock_i2c_slave_get_reg(uint8_t reg);                                       // read register directly (bus 0)
uint8_t hal_mock_i2c_slave_get_reg_bus(uint8_t bus, uint8_t reg);
void    hal_mock_i2c_slave_set_reg(uint8_t reg, uint8_t value);                         // write register directly (bus 0)
void    hal_mock_i2c_slave_set_reg_bus(uint8_t bus, uint8_t reg, uint8_t value);
uint8_t hal_mock_i2c_slave_get_reg_ptr(void);                                          // current pointer (bus 0)
uint8_t hal_mock_i2c_slave_get_reg_ptr_bus(uint8_t bus);
void    hal_mock_i2c_slave_simulate_receive(const uint8_t *data, int len);              // simulate master-write
void    hal_mock_i2c_slave_simulate_receive_bus(uint8_t bus, const uint8_t *data, int len);
int     hal_mock_i2c_slave_simulate_request(uint8_t *out_buf, int max_len);             // simulate master-read
int     hal_mock_i2c_slave_simulate_request_bus(uint8_t bus, uint8_t *out_buf, int max_len);
```

---

## `hal_swserial` - Software serial  *(optional - `HAL_DISABLE_SWSERIAL`)*

UART frame-format constants for `config` are defined in `hal/hal_uart_config.h`.

```c
#include <hal/hal_uart_config.h>

// 5/6/7/8 data bits, N/E/O parity, 1/2 stop bits
HAL_UART_CFG_5N1  HAL_UART_CFG_6N1  HAL_UART_CFG_7N1  HAL_UART_CFG_8N1
HAL_UART_CFG_5N2  HAL_UART_CFG_6N2  HAL_UART_CFG_7N2  HAL_UART_CFG_8N2
HAL_UART_CFG_5E1  HAL_UART_CFG_6E1  HAL_UART_CFG_7E1  HAL_UART_CFG_8E1
HAL_UART_CFG_5E2  HAL_UART_CFG_6E2  HAL_UART_CFG_7E2  HAL_UART_CFG_8E2
HAL_UART_CFG_5O1  HAL_UART_CFG_6O1  HAL_UART_CFG_7O1  HAL_UART_CFG_8O1
HAL_UART_CFG_5O2  HAL_UART_CFG_6O2  HAL_UART_CFG_7O2  HAL_UART_CFG_8O2
```

All values are Arduino-compatible. On Arduino targets HAL maps directly to
core `SERIAL_*` constants; on host/mock builds HAL uses fallback values that
match ArduinoCore-API.

```c
#include <hal/hal_swserial.h>

typedef hal_swserial_impl_t *hal_swserial_t;  // opaque handle

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin);
bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin);
bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin);
void hal_swserial_begin(hal_swserial_t h, uint32_t baud, uint16_t config);  // e.g. HAL_UART_CFG_8N1
int  hal_swserial_available(hal_swserial_t h);
int  hal_swserial_read(hal_swserial_t h);     // returns byte (0-255) or -1 if empty
size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len);
size_t hal_swserial_println(hal_swserial_t h, const char *s);
void hal_swserial_flush(hal_swserial_t h);       // block until TX complete
void hal_swserial_destroy(hal_swserial_t h);
```

**impl/arduino:** `SoftwareSerial` (Arduino-pico).
**impl/.mock:** ring buffer plus last-write capture; injectable via mock helpers.
**Thread safety:** Not thread-safe. All calls must be made from the same core that created the handle.

**Mock helpers:**
```c
void        hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len);
void        hal_mock_swserial_reset(hal_swserial_t h);
const char *hal_mock_swserial_last_write(hal_swserial_t h);
```

---

## `hal_uart` - Hardware UART  *(optional - `HAL_DISABLE_UART`)*

```c
#include <hal/hal_uart.h>

typedef enum {
    HAL_UART_PORT_1 = 1,
    HAL_UART_PORT_2 = 2,
} hal_uart_port_t;

typedef hal_uart_impl_t *hal_uart_t;

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin);
bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin);
bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin);
void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config);
int  hal_uart_available(hal_uart_t h);
int  hal_uart_read(hal_uart_t h);
size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len);
size_t hal_uart_println(hal_uart_t h, const char *s);
void hal_uart_flush(hal_uart_t h);       // block until TX complete
void hal_uart_destroy(hal_uart_t h);
```

**impl/arduino:** RP2040 Arduino-pico `Serial1` / `Serial2`.
**impl/.mock:** ring buffer plus last-write capture; injectable via mock helpers.
**Thread safety:** Not thread-safe. All calls must be serialized by the caller.

**Mock helpers:**
```c
void        hal_mock_uart_push(hal_uart_t h, const uint8_t *data, int len);
void        hal_mock_uart_reset(hal_uart_t h);
const char *hal_mock_uart_last_write(hal_uart_t h);
uint8_t     hal_mock_uart_get_rx_pin(hal_uart_t h);
uint8_t     hal_mock_uart_get_tx_pin(hal_uart_t h);
```

---

## `hal_rgb_led` - NeoPixel status LED  *(optional - `HAL_DISABLE_RGB_LED`)*

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

// Set colour. Repeated calls with the same colour are suppressed (no SPI traffic).
void hal_rgb_led_set_color(hal_rgb_led_color_t color);

// Turn LED off (equivalent to set_color(HAL_RGB_LED_NONE))
void hal_rgb_led_off(void);
```

**impl/arduino:** `Adafruit_NeoPixel`.
**impl/.mock:** records init parameters, pixel type, brightness and last colour; injectable via mock helpers.
**Thread safety:** Arduino backend: thread-safe and multicore-safe for HAL calls. A HAL mutex serializes access to singleton state and underlying `Adafruit_NeoPixel`. Mock backend is unsynchronized and intended for single-threaded tests.

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

## `hal_thermocouple` - Thermocouple amplifier  *(optional - `HAL_DISABLE_THERMOCOUPLE`)*

Supports MCP9600 (I2C) and MAX6675 (SPI bit-bang). Functions not available on the
selected chip return a safe default (NAN / 0 / false) and print an error.

```c
#include <hal/hal_thermocouple.h>

// Chip selector
typedef enum {
    HAL_THERMOCOUPLE_CHIP_MCP9600,  // MCP9600/MCP9601 via I2C
    HAL_THERMOCOUPLE_CHIP_MAX6675,  // MAX6675 via SPI bit-bang (K-type only)
} hal_thermocouple_chip_t;

// Wire type (MCP9600 supports all; MAX6675 is fixed K-type)
typedef enum {
    HAL_THERMOCOUPLE_TYPE_K = 0, HAL_THERMOCOUPLE_TYPE_J, HAL_THERMOCOUPLE_TYPE_T,
    HAL_THERMOCOUPLE_TYPE_N,     HAL_THERMOCOUPLE_TYPE_S, HAL_THERMOCOUPLE_TYPE_E,
    HAL_THERMOCOUPLE_TYPE_B,     HAL_THERMOCOUPLE_TYPE_R,
} hal_thermocouple_type_t;

// ADC resolution (MCP9600 only)
typedef enum {
    HAL_THERMOCOUPLE_ADC_RES_18 = 0,  // 18-bit, ~320 ms/conv
    HAL_THERMOCOUPLE_ADC_RES_16,      // 16-bit, ~80 ms/conv
    HAL_THERMOCOUPLE_ADC_RES_14,      // 14-bit, ~20 ms/conv
    HAL_THERMOCOUPLE_ADC_RES_12,      // 12-bit, ~5 ms/conv
} hal_thermocouple_adc_res_t;

// Ambient (cold-junction) resolution (MCP9600 only)
typedef enum {
    HAL_THERMOCOUPLE_AMBIENT_RES_0_25    = 0,  // 0.25 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_125,         // 0.125 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_0625,        // 0.0625 °C/LSB
    HAL_THERMOCOUPLE_AMBIENT_RES_0_03125,       // 0.03125 °C/LSB
} hal_thermocouple_ambient_res_t;

// Config struct - fill chip, then the matching bus union member
typedef struct {
    hal_thermocouple_chip_t chip;
    union {
        struct { uint8_t sda_pin; uint8_t scl_pin; uint32_t clock_hz; uint8_t i2c_addr; } i2c;
        struct { uint8_t sclk_pin; uint8_t cs_pin; uint8_t miso_pin; } spi;
    } bus;
} hal_thermocouple_config_t;

typedef hal_thermocouple_impl_t *hal_thermocouple_t;  // opaque handle

// Lifecycle
hal_thermocouple_t hal_thermocouple_init(const hal_thermocouple_config_t *cfg);
void               hal_thermocouple_deinit(hal_thermocouple_t h);  // NULL-safe

// Readings
float   hal_thermocouple_read(hal_thermocouple_t h);          // hot junction °C, NAN on fault
float   hal_thermocouple_read_ambient(hal_thermocouple_t h);  // cold junction °C (MCP9600 only)
int32_t hal_thermocouple_read_adc_raw(hal_thermocouple_t h);  // raw µV (MCP9600 only); 0 if unsupported

// Configuration (MCP9600 only unless noted)
void hal_thermocouple_set_type(hal_thermocouple_t h, hal_thermocouple_type_t type);
hal_thermocouple_type_t hal_thermocouple_get_type(hal_thermocouple_t h);  // MAX6675 always returns K

void hal_thermocouple_set_filter(hal_thermocouple_t h, uint8_t coeff);    // IIR coeff [0,7]
uint8_t hal_thermocouple_get_filter(hal_thermocouple_t h);

void hal_thermocouple_set_adc_resolution(hal_thermocouple_t h, hal_thermocouple_adc_res_t res);
hal_thermocouple_adc_res_t hal_thermocouple_get_adc_resolution(hal_thermocouple_t h);

void hal_thermocouple_set_ambient_resolution(hal_thermocouple_t h, hal_thermocouple_ambient_res_t res);

void hal_thermocouple_enable(hal_thermocouple_t h, bool enable);  // false = sleep (MCP9600 only)
bool hal_thermocouple_is_enabled(hal_thermocouple_t h);           // MAX6675 always returns true

// Alert channels 1–4 (MCP9600 only)
typedef struct {
    float temperature; bool rising; bool alert_cold_junction;
    bool active_high;  bool interrupt_mode;
} hal_thermocouple_alert_cfg_t;

void  hal_thermocouple_set_alert(hal_thermocouple_t h, uint8_t alert_num,
                                  bool enabled, const hal_thermocouple_alert_cfg_t *cfg);
float hal_thermocouple_get_alert_temp(hal_thermocouple_t h, uint8_t alert_num);

uint8_t hal_thermocouple_get_status(hal_thermocouple_t h);  // raw status register
```

**impl/arduino:** `Adafruit_MCP9600` (I2C) / `MAX6675` library (SPI).
**Thread safety:** Thread-safe and multicore-safe. Each instance has its own per-instance `hal_mutex_t`. All read, configuration, and deinit operations are protected under this mutex.

---

## `hal_external_adc` - ADS1115 external ADC  *(optional - `HAL_DISABLE_EXTERNAL_ADC`)*

```c
#include <hal/hal_external_adc.h>

// Init ADS1115 at the given 7-bit I2C address.
// adc_range: LSB size in millivolts (e.g. 0.1875 for ±6.144 V full-scale).
//            Stored internally for hal_ext_adc_read_scaled().
void    hal_ext_adc_init(uint8_t address, float adc_range);
void    hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range); // i2c_bus: 0=Wire, 1=Wire1

// Read raw signed 16-bit value from channel 0–3.
// Sets gain to 0 (±6.144 V) before each conversion; blocks until result ready.
int16_t hal_ext_adc_read(uint8_t channel);

// Read channel and return (raw * adc_range) / 1000.0f.
// Apply further project-specific corrections (voltage divider etc.) on top.
float   hal_ext_adc_read_scaled(uint8_t channel);
```

**impl/arduino:** bundled `ADS1X15` driver; protected by a dedicated `hal_mutex_t` plus the I2C HAL bus mutex.
**Thread safety:** Arduino backend: thread-safe and multicore-safe. A dedicated internal `hal_mutex_t` serializes ADC channel selection and range access; the I2C bus mutex protects the underlying Wire transaction. `hal_ext_adc_init()` / `hal_ext_adc_init_bus()` modify global singleton state and must be called from one core during init. Mock backend does not synchronize concurrent access.

**Mock helpers:**
```c
void  hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value);   // inject raw 16-bit result for channel 0-3
void  hal_mock_ext_adc_inject_scaled(uint8_t channel, float value);  // inject pre-scaled float for channel 0-3
float hal_mock_ext_adc_get_range(void);                               // return adc_range set by hal_ext_adc_init()
```

---

## `hal_gps` - GPS NMEA receiver  *(optional - `HAL_DISABLE_GPS`)*

Singleton GPS subsystem. Wraps TinyGPS++ behind a platform-independent API.
The real implementation feeds the parser from a PIO-based SoftwareSerial port;
the mock lets tests inject position, speed, date and time directly.

**Auto-detect framing:** After ~500 received characters, if every NMEA sentence
failed its checksum, the implementation automatically toggles between 8N1 and
7N1 and re-initialises the serial port.  This handles both genuine u-blox modules
(8N1) and clone NEO-6M boards that ship as 7N1 - no user intervention required.

```c
#include <hal/hal_gps.h>

// Initialise the GPS subsystem (only first call has effect - singleton guard).
// config: UART frame format - HAL_UART_CFG_8N1 (recommended default) or
//         HAL_UART_CFG_7N1.  Auto-detect will try the other if checksums fail.
void hal_gps_init(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud, uint16_t config);

// Drain available serial bytes into the parser.
// Must be called frequently (every main-loop iteration) to prevent the
// PIO SoftwareSerial 32-byte FIFO from overflowing at 9600 baud.
// No-op in the mock build - use inject helpers directly.
void hal_gps_update(void);

// Feed one raw NMEA byte into the parser manually (alternative to hal_gps_update).
// In mock builds this is a no-op.
void hal_gps_encode(char c);

// Fix state
bool     hal_gps_location_is_valid(void);    // true when a valid fix is available
bool     hal_gps_location_is_updated(void);  // true when new data arrived since last query
uint32_t hal_gps_location_age(void);         // ms since last valid fix; UINT32_MAX if no fix

// Position
double hal_gps_latitude(void);   // degrees, negative = south
double hal_gps_longitude(void);  // degrees, negative = west

// Speed
double hal_gps_speed_kmph(void); // ground speed in km/h; 0.0 when no fix

// UTC date
int hal_gps_date_year(void);   // four-digit year
int hal_gps_date_month(void);  // 1-12
int hal_gps_date_day(void);    // 1-31

// UTC time
int hal_gps_time_hour(void);   // 0-23
int hal_gps_time_minute(void); // 0-59
int hal_gps_time_second(void); // 0-59

// Diagnostics
uint32_t hal_gps_chars_processed(void);    // total bytes fed into the parser
uint32_t hal_gps_passed_checksum(void);    // NMEA sentences that passed checksum
uint32_t hal_gps_failed_checksum(void);    // NMEA sentences that failed checksum
uint32_t hal_gps_sentences_with_fix(void); // valid sentences containing a location fix
int      hal_gps_serial_available(void);   // bytes waiting in the serial RX buffer
```

**impl/arduino:** `TinyGPS++` library + `hal_swserial`.  `hal_gps_update()` must be
polled from the main loop every iteration; on RP2040 the PIO SoftwareSerial buffer
is only 32 bytes and overflows in ~33 ms at 9600 baud.
**impl/.mock:** internal state struct; inject helpers set values directly.
**Thread safety:** Arduino backend: thread-safe and multicore-safe. An internal `hal_mutex_t` protects the TinyGPS++ parser state, serial update, and all accessor calls. Mock backend is unsynchronized and intended for single-threaded tests.

**UART config default:**

`HAL_GPS_DEFAULT_UART_CONFIG` (defined in `hal/hal_config.h`) defaults to
`HAL_UART_CFG_8N1` (the NMEA 0183 standard).  The auto-detect mechanism makes
this safe for clone modules too - it will switch to 7N1 automatically if needed.

```c
// hal/hal_config.h default (can be overridden in build flags):
#define HAL_GPS_DEFAULT_UART_CONFIG  HAL_UART_CFG_8N1

// Usage:
hal_gps_init(GPS_RX_PIN, GPS_TX_PIN, 9600, HAL_UART_CFG_8N1);
```

**Mock helpers:**
```c
void hal_mock_gps_set_location(double lat, double lng);        // inject latitude and longitude
void hal_mock_gps_set_valid(bool valid);                       // control hal_gps_location_is_valid()
void hal_mock_gps_set_updated(bool updated);                   // control hal_gps_location_is_updated()
void hal_mock_gps_set_age(uint32_t age_ms);                    // control hal_gps_location_age()
void hal_mock_gps_set_speed(double kmph);                      // control hal_gps_speed_kmph()
void hal_mock_gps_set_date(int year, int month, int day);      // control date accessors
void hal_mock_gps_set_time(int hour, int minute, int second);  // control time accessors
void hal_mock_gps_reset(void);                                 // zero all state
```

---

## `hal_eeprom` - Unified EEPROM  *(optional - `HAL_DISABLE_EEPROM`)*

Single API that works with both the RP2040 internal flash-backed EEPROM and the
external AT24C256 I2C EEPROM.  The back-end is selected at runtime in
`hal_eeprom_init()`.

```c
#include <hal/hal_eeprom.h>

typedef enum {
    HAL_EEPROM_AT24C256 = 1, // External AT24C256 I2C EEPROM - 32 KB, address 0x50
    HAL_EEPROM_RP2040   = 2, // RP2040 internal flash-backed EEPROM emulation
} hal_eeprom_type_t;

// Initialise EEPROM. Call before any other hal_eeprom_* function.
// size:     used only for HAL_EEPROM_RP2040 (passed to EEPROM.begin());
//           ignored for HAL_EEPROM_AT24C256 (always 32768 bytes).
// i2c_addr: 7-bit I2C address of the AT24C256 chip; ignored for RP2040.
//           Pass 0 to use the default EEPROM_I2C_ADDRESS (0x50 from hal_config.h).
void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr);

// Byte-level access
void    hal_eeprom_write_byte(uint16_t addr, uint8_t val);
uint8_t hal_eeprom_read_byte(uint16_t addr);

// 32-bit integer access (little-endian, 4 bytes starting at addr)
void    hal_eeprom_write_int(uint16_t addr, int32_t val);
int32_t hal_eeprom_read_int(uint16_t addr);

// Flush buffered writes to non-volatile storage.
// HAL_EEPROM_RP2040: calls EEPROM.commit().
// HAL_EEPROM_AT24C256: no-op.
void hal_eeprom_commit(void);

// Zero-fill entire EEPROM (slow - do not use in time-critical code).
void hal_eeprom_reset(void);

// Return EEPROM size in bytes.
uint16_t hal_eeprom_size(void);
```

**Integer byte order:** `hal_eeprom_write_int` / `hal_eeprom_read_int` use
**little-endian** order (LSB at the lowest address).

**Commit semantics:** For `HAL_EEPROM_RP2040`, `hal_eeprom_write_byte` and
`hal_eeprom_write_int` only update the RAM buffer - call `hal_eeprom_commit()`
once after a group of writes to persist them to flash.  For `HAL_EEPROM_AT24C256`
each byte write is committed immediately to the chip; `hal_eeprom_commit()` is
a no-op.

**impl/arduino:** `HAL_EEPROM_RP2040` uses `<EEPROM.h>` (Arduino-pico).
`HAL_EEPROM_AT24C256` drives the chip via `hal_i2c_*` primitives with
write-cycle polling and watchdog feeding.  The AT24C256 I2C address is
`EEPROM_I2C_ADDRESS` (default `0x50`, defined in `hal_config.h`).
**impl/.mock:** in-memory byte array (`MOCK_EEPROM_BUF_SIZE`, default 32768).
**Thread safety:** Thread-safe and multicore-safe for both back-ends. `HAL_EEPROM_AT24C256` operations are protected by the `hal_i2c` bus mutex. `HAL_EEPROM_RP2040` operations are protected by a dedicated internal mutex. `hal_eeprom_init()` must be called from one core only.

### Mock helpers

```c
#include <hal/impl/.mock/hal_mock.h>

// Read a byte directly from the mock backing store.
uint8_t           hal_mock_eeprom_get_byte(uint16_t addr);
// Return the type set by hal_eeprom_init().
hal_eeprom_type_t hal_mock_eeprom_get_type(void);
// True if hal_eeprom_commit() was called since the last reset.
bool              hal_mock_eeprom_was_committed(void);
// Clear the committed flag (re-arm the check).
void              hal_mock_eeprom_clear_committed_flag(void);
// Return number of byte writes since last reset/counter clear.
uint32_t          hal_mock_eeprom_get_write_count(void);
// Clear the byte-write counter.
void              hal_mock_eeprom_clear_write_count(void);
// Reset all mock state to defaults (zeroed memory, no type, not committed).
void              hal_mock_eeprom_reset(void);
```

### Usage example

```c
// RP2040 internal EEPROM (i2c_addr ignored - pass 0):
hal_eeprom_init(HAL_EEPROM_RP2040, 512, 0);
hal_eeprom_write_int(0, my_value);
hal_eeprom_commit();

// AT24C256 at default address 0x50 (I2C must already be initialised via hal_i2c_init):
hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0);            // 0 → use EEPROM_I2C_ADDRESS
// or with an explicit address (e.g. A0 pin tied high → 0x51):
hal_eeprom_init(HAL_EEPROM_AT24C256, 0, 0x51);
hal_eeprom_write_byte(0, 0xAB);
// no commit needed for AT24C256
```

---

## `hal_wifi` - WiFi  *(optional - `HAL_DISABLE_WIFI`)*

```c
#include <hal/hal_wifi.h>

typedef enum {
    HAL_WIFI_MODE_OFF    = 0,
    HAL_WIFI_MODE_STA    = 1,
    HAL_WIFI_MODE_AP     = 2,
    HAL_WIFI_MODE_AP_STA = 3,
} hal_wifi_mode_t;

bool    hal_wifi_set_mode(hal_wifi_mode_t mode);
bool    hal_wifi_disconnect(bool erase_credentials);
bool    hal_wifi_set_hostname(const char *hostname);
bool    hal_wifi_begin_station(const char *ssid, const char *password, bool non_blocking);
bool    hal_wifi_set_timeout_ms(uint32_t timeout_ms);
bool    hal_wifi_is_connected(void);
int     hal_wifi_status(void);
bool    hal_wifi_has_local_ip(void);
int32_t hal_wifi_rssi(void);                        // dBm
int     hal_wifi_get_strength(void);                // 0..5 bars
bool    hal_wifi_get_local_ip(char *out, size_t out_size);
bool    hal_wifi_get_dns_ip(char *out, size_t out_size);
bool    hal_wifi_get_mac(char *out, size_t out_size);
int     hal_wifi_ping(const char *host_or_ip);      // >=0 ok, <0 error
```

**impl/arduino:** Arduino-pico WiFi stack (`WiFi.h`).
**impl/.mock:** state injection via mock helpers.
**Thread safety:** Arduino backend is thread-safe and multicore-safe for public
HAL calls. An internal lazily initialized `hal_mutex_t` serializes access to
the underlying `WiFi` object. The mock backend is a simple state-injection test
double and does not provide the same synchronization guarantee for concurrent
test-thread access.

**Mock helpers:**
```c
void        hal_mock_wifi_reset(void);
void        hal_mock_wifi_set_connected(bool connected);
void        hal_mock_wifi_set_status(int status);
void        hal_mock_wifi_set_rssi(int32_t rssi);
void        hal_mock_wifi_set_local_ip(const char *ip);
void        hal_mock_wifi_set_dns_ip(const char *ip);
void        hal_mock_wifi_set_mac(const char *mac);
void        hal_mock_wifi_set_ping_result(int result);
const char *hal_mock_wifi_get_hostname(void);
uint32_t    hal_mock_wifi_get_timeout_ms(void);
```

---

## `hal_time` - System time & NTP  *(optional - `HAL_DISABLE_TIME`)*

```c
#include <hal/hal_time.h>

bool     hal_time_set_timezone(const char *tz);     // POSIX TZ string
bool     hal_time_sync_ntp(const char *primary_server, const char *secondary_server);
uint64_t hal_time_unix(void);                       // seconds since epoch (Y2038-safe)
bool     hal_time_is_synced(uint64_t min_unix);     // true when time >= min_unix
bool     hal_time_get_local(struct tm *out_tm);
bool     hal_time_format_local(char *out, size_t out_size, const char *format);
```

**impl/arduino:** `configTime()` / `time()` / `localtime_r()` (Arduino-pico / lwIP SNTP).
**impl/.mock:** state injection via mock helpers.
**Thread safety:** Not thread-safe. Serialize all calls from the caller side.

**Mock helpers:**
```c
void        hal_mock_time_reset(void);
void        hal_mock_time_set_unix(uint64_t unix_time);
void        hal_mock_time_set_local(const struct tm *tm_local);
const char *hal_mock_time_get_timezone(void);
const char *hal_mock_time_get_ntp_primary(void);
const char *hal_mock_time_get_ntp_secondary(void);
```

---

## `hal_kv` - Key-value storage on EEPROM  *(optional - `HAL_DISABLE_KV`)*

Thread-safe append-only KV/record storage on top of `hal_eeprom`.
Uses dual-bank layout with CRC16-protected headers and records.
Automatic garbage-collection (GC) compacts live records into the alternate bank
when the active bank runs out of space.

```c
#include <hal/hal_kv.h>

typedef struct {
    uint32_t generation;       // bank generation counter
    uint16_t used_bytes;       // bytes used in active bank
    uint16_t capacity_bytes;   // single-bank capacity
    uint16_t key_count;        // number of live keys
    uint32_t next_sequence;    // next record sequence number
} hal_kv_stats_t;

bool hal_kv_init(uint16_t base_addr, uint16_t size_bytes);
bool hal_kv_set_u32(uint16_t key, uint32_t value);
bool hal_kv_get_u32(uint16_t key, uint32_t *out_value);
bool hal_kv_set_blob(uint16_t key, const uint8_t *data, uint16_t len);
bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size, uint16_t *out_len);
bool hal_kv_delete(uint16_t key);
bool hal_kv_gc(void);
bool hal_kv_get_stats(hal_kv_stats_t *out_stats);
```

**Dependencies:** `hal_eeprom`, `hal_sync`, `hal_serial`.
**Thread safety:** Thread-safe and multicore-safe. An internal mutex (lazy-initialized via `kv_ensure_mutex()`) protects all operations. `hal_kv_init()` must be called after `hal_eeprom_init()`.

**Deduplication:** `hal_kv_set_u32` / `hal_kv_set_blob` skip the EEPROM write when the
value is unchanged, avoiding unnecessary flash wear.

---

## `hal_math` - Platform-independent math helpers

```c
#include <hal/hal_math.h>

// Clamp to [lo, hi] - type-independent macro
#define hal_constrain(v, lo, hi) ...
// Re-map value from one range to another - type-independent macro
// When in_min == in_max, returns out_min (safe: no division by zero).
#define hal_map(x, in_min, in_max, out_min, out_max) ...
```

**Note:** Macros are available in both C and C++ and are re-exported via
`hal/hal_system.h`. `hal_constrain` is also re-exported as `pid_clamp` for
backward compatibility.
Macro arguments may be evaluated more than once, so avoid side effects in
arguments (for example `i++` or function calls that modify state).
**Note:** `hal_map` returns `out_min` when `in_min == in_max` to avoid
integer division by zero. This matches the behaviour of `mapfloat()`.
**Thread safety:** Thread-safe. Helpers are pure expressions (no shared state).

---

## `hal_soft_timer` - C wrapper over `SmartTimers`

```c
#include <hal/hal_soft_timer.h>

typedef struct hal_soft_timer_impl_s *hal_soft_timer_t;
typedef void (*hal_soft_timer_callback_t)(void);

hal_soft_timer_t hal_soft_timer_create(void);
void             hal_soft_timer_destroy(hal_soft_timer_t timer);

bool     hal_soft_timer_begin(hal_soft_timer_t timer, hal_soft_timer_callback_t callback, uint32_t interval_ms);
void     hal_soft_timer_restart(hal_soft_timer_t timer);
bool     hal_soft_timer_available(hal_soft_timer_t timer);
uint32_t hal_soft_timer_time_left(hal_soft_timer_t timer);
void     hal_soft_timer_set_interval(hal_soft_timer_t timer, uint32_t interval_ms);
void     hal_soft_timer_tick(hal_soft_timer_t timer);
void     hal_soft_timer_abort(hal_soft_timer_t timer);

typedef struct {
    hal_soft_timer_t         *timer;
    hal_soft_timer_callback_t callback;
    uint32_t                  intervalMs;
} hal_soft_timer_table_entry_t;

bool hal_soft_timer_setup_table(const hal_soft_timer_table_entry_t *table,
                                uint32_t count,
                                void (*idle_cb)(void),
                                uint32_t delay_ms);
bool hal_soft_timer_tick_table(const hal_soft_timer_table_entry_t *table,
                               uint32_t count);
```

**Purpose:** lets C-style modules consume timer functionality without direct `SmartTimers` class coupling.
**Implementation:** delegates to `SmartTimers` internally (same runtime semantics).
**Thread safety:** Thread-safe and multicore-safe (inherits `SmartTimers` per-instance mutex protection).
**Table helpers:** `hal_soft_timer_setup_table(...)` creates/configures timers from a descriptor array and optionally calls `idle_cb` + inter-entry delay. `hal_soft_timer_tick_table(...)` ticks all entries from the same array.
**Validation contract:** table helpers validate `table != NULL` and `count > 0`. For invalid input they log via `hal_derr(...)` and return `false`.

---

## `hal_pid_controller` - C wrapper over `pidController`

```c
#include <hal/hal_pid_controller.h>

typedef struct hal_pid_controller_impl_s *hal_pid_controller_t;
typedef enum {
  HAL_PID_DIRECTION_FORWARD = 0,
  HAL_PID_DIRECTION_BACKWARD = 1
} hal_pid_direction_t;

hal_pid_controller_t hal_pid_controller_create(void);
hal_pid_controller_t hal_pid_controller_create_with_gains(float kp, float ki, float kd, float max_integral);
void                 hal_pid_controller_destroy(hal_pid_controller_t controller);

void  hal_pid_controller_set_kp(hal_pid_controller_t controller, float kp);
void  hal_pid_controller_set_ki(hal_pid_controller_t controller, float ki);
void  hal_pid_controller_set_kd(hal_pid_controller_t controller, float kd);
void  hal_pid_controller_set_tf(hal_pid_controller_t controller, float tf);
void  hal_pid_controller_set_max_integral(hal_pid_controller_t controller, float max_integral);

float hal_pid_controller_get_kp(hal_pid_controller_t controller);
float hal_pid_controller_get_ki(hal_pid_controller_t controller);
float hal_pid_controller_get_kd(hal_pid_controller_t controller);
float hal_pid_controller_get_tf(hal_pid_controller_t controller);

void  hal_pid_controller_update_time(hal_pid_controller_t controller, float time_divider);
float hal_pid_controller_update(hal_pid_controller_t controller, float error);
void  hal_pid_controller_set_output_limits(hal_pid_controller_t controller, float min_output, float max_output);
void  hal_pid_controller_reset(hal_pid_controller_t controller);
void  hal_pid_controller_set_direction(hal_pid_controller_t controller, hal_pid_direction_t direction);
bool  hal_pid_controller_is_error_stable(hal_pid_controller_t controller, float error, float tolerance, int stability_threshold);
bool  hal_pid_controller_is_oscillating(hal_pid_controller_t controller, float current_error, int window_size);
```

**Purpose:** exposes PID control through C functions and opaque handles, enabling incremental migration from class-based usage.
**Implementation:** delegates to `PIDController` internally (same runtime semantics).
**Thread safety:** Not thread-safe. Use one controller instance per control loop or serialize externally.

**Anti-windup:** two complementary mechanisms:
1. **Integral hard clamp** - `setMaxIntegral()` / `hal_pid_controller_set_max_integral()` limits `|integral|`.
2. **Clamping anti-windup** - integral accumulation is skipped when the output
   is saturated in the direction of the error (i.e. output ≥ max and error > 0,
   or output ≤ min and error < 0). This prevents integral windup at output
   limits without requiring manual tuning of the integral cap.

---

## Higher-level utilities (tools.h / tools.cpp)

Physical location: `src/utils/*`.

Recommended include options:
- `#include <tools.h>` (aggregator include in `src/`)
- `#include <tools_c.h>` (C-compatible utility declarations from `src/`)
- direct include from `utils/`, for example `#include <utils/SmartTimers.h>`

Utilities depend on HAL internally.

### Bit-manipulation helpers (`hal_bits`)

Bit aliases are defined with `#ifndef` guards, so they are no-ops when the
Arduino core already provides them.

```c
#include <hal/hal_bits.h>

#define is_set(x, mask)      // true when any bit in mask is set in x
#define set_bit(var, mask)   // OR mask into var
#define clr_bit(var, mask)   // AND ~mask into var

// Bit-index variants (Arduino-compatible names)
#define bitSet(var, bit)     // set bit number 'bit' in var
#define bitClear(var, bit)   // clear bit number 'bit' in var
#define bitRead(var, bit)    // read bit number 'bit' from var (returns 0 or 1)

// Volatile register helpers (macro pointer form)
#define set_bit_v(reg, mask) // set mask in *reg
#define clr_bit_v(reg, mask) // clear mask in *reg

```

Avoid passing expressions with side effects (for example `i++` or function calls
that modify state), because macro arguments may be evaluated more than once.

### Functions

```c
void  debugInit(void);                  // optional - hal_deb() lazy-inits automatically
void  setDebugPrefixWithColon(const char *moduleName); // builds "<module>:" within HAL_DEBUG_PREFIX_SIZE and forwards to hal_deb_set_prefix
void  floatToDec(float val, int *hi, int *lo);
float decToFloat(int hi, int lo);
float adcToVolt(int adc, float r1, float r2);
float ntcToTemp(int tpin, int thermistor, int r);
float steinhart(float val, float thermistor, int r, bool characteristic);
int   percentToGivenVal(float percent, int maxWidth);
int   percentFrom(int givenVal, int maxVal);
float getAverageValueFrom(int tpin);  // includes dummy read (RP2040 ADC mux cross-talk fix)
float filter(float alpha, float input, float previous_output);
float filterValue(float currentValue, float newValue, float alpha);
int   adcCompe(int x);
float getAverageForTable(int *idx, int *overall, float val, float *table);
int   getAverageFrom(int *table, int size);
int   getMinimumFrom(int *table, int size);
int   getHalfwayBetweenMinMax(int *array, int n);
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
static inline uint32_t float_to_u32(float f);   // bitcast float → uint32_t (memcpy)
static inline float    u32_to_float(uint32_t u); // bitcast uint32_t → float (memcpy)
unsigned long getSeconds(void);
bool  isDaylightSavingTime(int year, int month, int day);
void  adjustTime(int *year, int *month, int *day, int *hour, int *minute);
bool  is_time_in_range(long now, long start, long end);
void  extract_time(long timeInMinutes, int *hours, int *minutes);
unsigned short byteArrayToWord(unsigned char *bytes);
void     wordToByteArray(unsigned short word, unsigned char *bytes);
uint8_t  MSB(unsigned short value);
uint8_t  LSB(unsigned short value);
int      MsbLsbToInt(uint8_t msb, uint8_t lsb);
float rroundf(float val);
float roundfWithPrecisionTo(float value, int precision);
char *printBinaryAndSize(int number, char *buf, size_t bufSize);  // writes binary string into buf
bool  concatStrings(char *dest, size_t destSize, const char *src1, const char *src2); // false on NULL args or too-small dest
bool  isValidString(const char *s, int maxBufSize);
char  hexToChar(char high, char low);
void  urlDecode(const char *src, char *dst);
void  removeSpaces(char *str);
bool  startsWith(const char *str, const char *prefix);
void  remove_non_ascii(const char *input, char *output, size_t outputSize);  // replaces UTF-8 with ASCII
void  hal_pack_field_pad(uint8_t *buf, const char *str, int width, uint8_t pad);
void  hal_pack_field(uint8_t *buf, const char *str, int width); // zero padding
unsigned short rgbToRgb565(unsigned char r, unsigned char g, unsigned char b);
const char *macToString(uint8_t mac[6], char *buf, size_t bufSize);
const char *encToString(uint8_t enc);       // WiFi encryption type (requires PICO_W)
bool  scanNetworks(const char *networkToFind);  // requires PICO_W
int   getRandomEverySomeMillis(uint32_t time, int maxValue);
float getRandomFloatEverySomeMillis(uint32_t time, float maxValue);
void  i2cScanner(void);

// SD Logger (requires SD card, enable SD_LOGGER in hal/hal_config.h)
int  getSDLoggerNumber(void);
int  getSDCrashNumber(void);
bool initSDLogger(int cs);
bool initCrashLogger(const char *addToName, int cs);
bool isSDLoggerInitialized(void);
bool isCrashLoggerInitialized(void);
void saveLoggerAndClose(void);
void saveCrashLoggerAndClose(void);
void crashReport(const char *format, ...);
```

---

## SmartTimers

```c
#include <utils/SmartTimers.h>

SmartTimers timer;

// Configure and start - callback signature: void f(void)
timer.begin(callback, interval_ms);

// Advance timer; fires callback when interval elapsed, then restarts automatically.
// Call in your main loop.
timer.tick();

// true if the interval has elapsed since the last restart (non-blocking)
bool ready = timer.available();

// Remaining milliseconds until the next callback.
// Returns 0 when already elapsed or stopped. Does NOT return the configured interval.
uint32_t remaining = timer.time();

// Change the interval without resetting the timestamp.
// Call restart() afterwards if you want counting to begin from now.
timer.time(new_interval_ms);

// Reset the internal timestamp to now (interval starts counting from this moment)
timer.restart();

// Stop the timer
timer.abort();
```

**Note:** `SECOND`, `SECS()`, `MINS()`, `HOURS()` macros are defined in `hal/hal_system.h`
(included automatically by `utils/SmartTimers.h`).
**Thread safety:** Thread-safe and multicore-safe. Each instance has a per-instance `hal_mutex_t` that serializes all method calls. Callbacks passed to `begin()` are invoked outside the mutex to prevent deadlock.

---

## pidController

```c
#include <utils/pidController.h>

// Tuning parameters struct
typedef struct { float kP; float kI; float kD; float Tf; } PIDValues;

enum Direction { FORWARD, BACKWARD };

// Construct with initial gains and anti-windup integral limit
PIDController pid(float kp, float ki, float kd, float mi);
PIDController pid;  // default constructor - all gains 0, limits uninitialised

// Tuning setters / getters
pid.setKp(kp);  pid.getKp();
pid.setKi(ki);  pid.getKi();
pid.setKd(kd);  pid.getKd();
pid.setTf(tf);  pid.getTf();   // derivative low-pass filter time constant
pid.setMaxIntegral(mi);        // anti-windup: maximum |integral| value

// Output clamping
pid.setOutputLimits(float min, float max);

// Time step - call this when dt changes (timeDivider = 1/dt in ms)
pid.updatePIDtime(float timeDivider);

// Run one PID iteration; error = setpoint - measurement
float out = pid.updatePIDcontroller(float error);

// Direction
pid.setDirection(FORWARD);   // or BACKWARD

// Analysis
bool stable = pid.isErrorStable(error, tolerance, stabilityThreshold);
bool osc    = pid.isOscillating(currentError, windowSize);  // default windowSize = 20

// Reset integral, derivative, history
pid.reset();

// Backward-compat clamp alias (use hal_constrain() in new code)
template<typename T> T pid_clamp(T v, T lo, T hi);
```

**Thread safety:** Not thread-safe. Each `PIDController` instance should be used from one core/thread.

---

## multicoreWatchdog

```c
#include <utils/multicoreWatchdog.h>

// Initialise hardware watchdog (timeout = time ms; liveness check every time/10 ms).
// Returns true if the system was rebooted by the watchdog (false = clean boot).
// 'function' callback is invoked once on watchdog-reboot with WATCHDOG_VALUES_AMOUNT
// diagnostic integers (NOINIT core-alive flags). May be NULL.
bool setupWatchdog(void(*function)(int *values, int size), unsigned int time);

// Call from core 0 main loop to signal liveness + tick the internal timer
void updateWatchdogCore0(void);

// Call from core 1 main loop to signal liveness + tick the internal timer
void updateWatchdogCore1(void);

// Call once at the end of each core's setup function
void setStartedCore0(void);
void setStartedCore1(void);

// Returns true when both setStartedCore0() and setStartedCore1() have been called
bool isEnvironmentStarted(void);

// Schedule an immediate hardware reset
void triggerSystemReset(void);

// Feed the hardware watchdog (wraps hal_watchdog_feed; safe to call before setupWatchdog)
void watchdog_feed(void);
```

**impl:** `hal_watchdog_enable` / `hal_watchdog_feed` / `hal_watchdog_caused_reboot`.
**Note:** The internal timer uses `SmartTimers` and is guarded by a HAL mutex to prevent
double-fire from concurrent `updateWatchdogCore0/1` calls.

---

## draw7Segment - 7-segment style display rendering

```c
#include <utils/draw7Segment.h>

// Draw a 7-segment string at (x, y).
// str          - null-terminated string to render.
// digitWidth   - width of a single digit cell in pixels.
// digitHeight  - height of a single digit cell in pixels.
// thickness    - segment thickness in pixels.
// color        - RGB565 colour.
void draw7SegString(const char* str, int x, int y, int digitWidth, int digitHeight, float thickness, uint16_t color);

// Calculate the pixel width of a 7-segment string without drawing it.
// Returns the total width in pixels.
int get7SegStringWidth(const char* str, int digitWidth, float thickness);
```

**Supported characters:** `0`–`9`, hex `A`–`F`, space, `+`, `-`, `.`, `:`, `%`, `^`.

Characters have proportional widths: `1` and space are narrower, `^` slightly wider.

**Dependencies:** `hal_display.h` only. No Arduino SDK, no platform-specific types - `const char*` throughout.

**Thread safety:** Thread-safe when `hal_display` is thread-safe (Arduino backend). Delegates all drawing to `hal_display_*` functions which are mutex-protected.

---

## Dependencies (hardware build)

| HAL module | External dependency |
|---|---|
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system`, `hal_serial` | Arduino-pico core (`Arduino.h`) |
| `hal_sync` | pico SDK `pico/mutex.h` |
| `hal_timer` | pico SDK `pico/time.h`, `hardware/timer.h` |
| `hal_soft_timer` | internal `SmartTimers` utility |
| `hal_pid_controller` | internal `pidController` utility |
| `hal_can` | bundled MCP2515 driver (`drivers/MCP2515/mcp_can`) |
| `hal_display` | `Adafruit_ILI9341` / `Adafruit_ST7789` / `Adafruit_ST7735` / `Adafruit_ST7796S` / `Adafruit_SSD1306` + `Adafruit_GFX_Library` |
| `hal_spi` | Arduino-pico `SPI.h` / `SPI1` |
| `hal_i2c` | Arduino-pico `Wire.h` |
| `hal_swserial` | `SoftwareSerial` (Arduino-pico) |
| `hal_gps` | `TinyGPS++` library + `hal_swserial` |
| `hal_rgb_led` | `Adafruit_NeoPixel` |
| `hal_thermocouple` (MCP9600) | bundled MCP9600 driver (`drivers/Adafruit_MCP9600`) |
| `hal_thermocouple` (MAX6675) | bundled MAX6675 driver (`drivers/MAX6675`) |
| `hal_external_adc` | bundled `ADS1X15` driver |
| `hal_wifi` | Arduino-pico WiFi stack (`WiFi.h`) |
| `hal_time` | Arduino-pico / lwIP SNTP (`configTime`) |
| `hal_kv` | internal `hal_eeprom` + `hal_sync` |
| `tools` | `EEPROM.h`, `SD.h`, `SPI.h`, `Wire.h` (Arduino-pico) |
| `multicoreWatchdog` | internal `SmartTimers` + `hal_sync` mutex |

## Dependencies (mock / PC build)

All `impl/.mock/` files depend only on: `<cstdio>`, `<cstring>`, `<mutex>`, `<queue>`, `<stdarg.h>`.
No Arduino SDK, no pico SDK required.

---

## Unit tests

### Requirements

- CMake ≥ 3.16
- GCC / Clang with C++17

### Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

All 28 test suites complete in < 1 s on a standard desktop machine.

### How it works

The CMake build at the project root compiles a static library `hal_mock` from:

- all `src/hal/impl/.mock/*.cpp` stubs,
- `src/hal/hal_config.cpp`,
- `src/hal/hal_can_util.cpp`,
- `src/utils/SmartTimers.cpp`, `src/utils/pidController.cpp`,
- `src/utils/unity.c` (Unity framework).

Each test executable in `tests/` links against `hal_mock` only - no Arduino
headers, no pico SDK, no hardware.

`tools.cpp` is covered by `test_tools` using Arduino host stubs in
`src/arduino_host_stubs/`. `multicoreWatchdog.cpp` is covered by
`test_multicoreWatchdog` using the same host stubs plus HAL mocks.
`utils/draw7Segment.cpp` has no platform dependencies
(pure `const char*` + `hal_display`).

### Test suites

| Suite | What it covers |
|---|---|
| `test_hal_gpio` | pin modes, read/write, level injection |
| `test_hal_adc` | resolution config, inject + read |
| `test_hal_pwm` | resolution config, write |
| `test_hal_timer` | alarm add/cancel, `advance_us` dispatch |
| `test_hal_eeprom` | byte/int write–read, `commit` flag |
| `test_hal_serial` | `println` capture, `deb` capture, `reset`, RX inject + `available`/`read` |
| `test_hal_serial_session` | Framed HELLO handshake (encode/decode + CRC), unknown-payload reply (`SC_UNKNOWN_CMD`) and custom unknown-handler dispatch, request<->response seq echo, non-framed input is silently dropped, multi-frame RX handling, null-arg safety |
| `test_hal_swserial` | software UART RX inject, TX capture, pin reassignment |
| `test_hal_uart` | hardware UART RX inject, TX capture, pin reassignment |
| `test_hal_spi` | SPI init/reinit, reset, per-bus lock-depth coverage |
| `test_hal_i2c` | bus0/bus1 begin/request/read flow, address capture, busy helper, lock-depth and init/deinit state coverage |
| `test_hal_rgb_led` | init/init_ex, brightness clamp, off path, pre-init set_color guard |
| `test_hal_display` | display helper API (text sizing/formatting, presets, draw image, SSD1306 init + `hal_display_init_ssd1306_i2c_ex`, text-line helpers) |
| `test_hal_can` | send/receive, ring buffer, null-data guard, payload clamp, filter API, `hal_can_process_all`, `hal_can_create_with_retry`, `hal_can_encode_temp_i8` |
| `test_hal_thermocouple` | MCP9600 + MAX6675 inject, unsupported-op NAN returns, ADC resolution, enable/disable, alert/status |
| `test_hal_external_adc` | ADS1115 range setup, per-channel raw/scaled reads, out-of-range safety |
| `test_hal_gps` | location/speed/date/time inject, valid/updated/age flags, init reset, diagnostics getters |
| `test_hal_system` | delay/millis/micros behavior, watchdog flags, heap/chip-temp helpers, type-independent `hal_constrain`/`hal_map` (incl. equal-range guard), `COUNTOF`, `hal_u32_to_bytes_be`, `NONULL` |
| `test_hal_bits` | bit helper macros (`is_set`, `set_bit`, `clr_bit`, `bitSet`, `bitClear`, `bitRead`, `set_bit_v`, `clr_bit_v`) |
| `test_hal_wifi` | mode/hostname/RSSI/ping, IP/DNS/MAC inject, input validation |
| `test_hal_time` | timezone, NTP sync, Unix time, local time formatting |
| `test_hal_kv` | u32/blob CRUD, delete, unchanged-skip, GC, concurrent updates |
| `test_hal_soft_timer` | C wrapper coverage: create/begin/tick/abort/restart, table setup/tick helpers, delay/idle callback path, invalid input validation (`NULL` table / `count==0`) |
| `test_SmartTimers` | `tick`, callback firing, `abort`, `restart` (core behavior used by `hal_soft_timer_*`) |
| `test_pidController` | P output, output clamping, integral reset, stability detection (core behavior used by `hal_pid_controller_*`) |
| `test_multicoreWatchdog` | dual-core liveness gating, external reset path, pre-setup no-op safety |
| `test_tools` | host-stubbed utility coverage from `tools.cpp`, including `debugInit`, `setDebugPrefixWithColon`, numeric/time/string helpers, and buffer-safe formatting helpers |

### Adding a new test suite

1. Create `tests/test_<name>.cpp` with Unity `setUp`, `tearDown`, and `RUN_TEST` calls.
2. Add `add_hal_test(test_<name>)` to `tests/CMakeLists.txt`.
    For suites that compile extra sources (for example `test_tools` and
    `test_multicoreWatchdog`), create a dedicated `add_executable(...)` entry.
3. Rebuild: `cmake --build build && ctest --test-dir build`.

### Mock time control

SmartTimers and PIDController depend on `hal_millis()`.
The mock clock starts at 0 and is driven by:

```cpp
hal_mock_set_millis(uint32_t ms);     // set absolute time
hal_mock_advance_millis(uint32_t ms); // advance relative to now
hal_mock_timer_advance_us(uint64_t us); // fires pending hal_timer alarms
```

**Important:** `SmartTimers` uses `_lastTime == 0` as an "uninitialized" sentinel.
Start mock time at a non-zero value (e.g. `hal_mock_set_millis(1000)`) before
calling `SmartTimers::begin()` to avoid the guard triggering in tests.
