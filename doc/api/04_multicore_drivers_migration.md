# Multicore safety, drivers, logging, migration

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

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

### Runtime: multicore-safe (RP2040 backend)

After initialisation, most HAL runtime APIs are multicore-safe on the RP2040
backend (dual-core core0/core1).  Each module documents its thread-safety
guarantee in the per-module section below.  The general pattern is:

- **Per-instance mutexes** protect handle-based APIs (`hal_can`, `hal_thermocouple`, `hal_rtc`, `SmartTimers`).
- **Per-bus mutexes** protect shared communication buses (`hal_spi`, `hal_i2c`).
- **Singleton mutexes** protect global modules (`hal_eeprom`, `hal_display`, `hal_gps`, `hal_external_adc`, `hal_wifi`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_kv`, debug serial).
- **Stateless helpers** (`hal_bits`, `hal_math`, `hal_crypto`, `hal_constrain`, `hal_map`) are inherently thread-safe.

Singleton and per-bus mutexes use an internal atomic create-once fallback, so
two FreeRTOS tasks or RP2040 cores cannot publish different locks for the same
module. Module init/begin calls still remain the preferred place to create
those locks before normal runtime sharing.

Modules documented as **"Not thread-safe"** (`hal_uart`, `hal_time`, `pidController`)
must be serialized by the caller or used from a single core.

### Mock backend

Mock implementations (`impl/.mock/`) are designed for deterministic single-threaded
unit tests and do **not** provide real cross-thread synchronization.  Thread-safety
guarantees listed in per-module sections apply to the **RP2040 backend only**
unless explicitly stated otherwise.

---

## Drivers and frameworks

Bundled or ported low-level drivers live under `src/hal/impl/rp2040/drivers/`
or `src/hal/impl/shared/`.
Bundled high-level integration frameworks live under
`src/hal/impl/rp2040/frameworks/`.
Both are integrated as HAL-internal implementation detail (not public API).

### Inventory, authors and license paths

| Driver folder | HAL usage | Upstream author(s) | License | License path in repo |
|---|---|---|---|---|
| GFX engine (ported) | `hal_display` rendering (geometry, text, bitmaps) ported into `impl/shared/drivers/display/jh_gfx.*` | Limor Fried (Ladyada) + contributors (Adafruit GFX) | BSD-2-Clause (attribution in source headers; library no longer bundled/linked) | `src/hal/impl/shared/drivers/display/jh_gfx.h` |
| ILI9341 driver (ported) | TFT backend (`HAL_DISPLAY_ILI9341`) ported into `impl/shared/drivers/display/ili9341_driver.*` | Limor Fried (Ladyada) (Adafruit ILI9341) | BSD-2-Clause (attribution in source headers) | `src/hal/impl/shared/drivers/display/ili9341_driver.h` |
| ST77xx/GC9A01 driver (ported) | ST7735/ST7789/ST7796S backends plus Zephyr-informed GC9A01 round-TFT support ported into `impl/shared/drivers/display/st77xx_driver.*` | Limor Fried (Ladyada) (Adafruit ST7735/ST7789), Zephyr GC9x01x driver used as the GC9A01 reference checklist | BSD-2-Clause for the original ST77xx path; GC9A01 behavior port notes reference Apache-2.0 Zephyr sources | `src/hal/impl/shared/drivers/display/st77xx_driver.h` |
| SSD1306-family driver (ported) | OLED backend (`HAL_ENABLE_SSD1306`) ported into `impl/shared/drivers/display/ssd1306_driver.*` and extended for SSD1309/SSD1315/SH1106/CH1115 variants | Limor Fried (Ladyada) + contributors (Adafruit SSD1306), Zephyr display-driver behavior used as a reference checklist | BSD-2-Clause (attribution in source headers) | `src/hal/impl/shared/drivers/display/ssd1306_driver.h` |
| SSD1331/SSD135x RGB OLED drivers (ported) | Shared RGB565 OLED drivers over HAL SPI/GPIO (`HAL_ENABLE_SSD1331`, `HAL_ENABLE_SSD135X`) | Zephyr `display_ssd1331.c` and `display_ssd135x.c` behavior used as the local reference checklist | Apache-2.0 reference behavior, implemented in-tree against HAL transport | `src/hal/impl/shared/drivers/display/rgb_oled_driver.h` |
| ST7567 LCD driver (ported) | Shared monochrome LCD driver over HAL I2C or SPI/GPIO (`HAL_ENABLE_ST7567`) | Zephyr `display_st7567.c` and `display_st7567_regs.h` behavior used as the local reference checklist | Apache-2.0 reference behavior, implemented in-tree against HAL transport | `src/hal/impl/shared/drivers/display/st7567_driver.h` |
| NeoPixel core (ported) | `hal_rgb_led` | Phil "Paint Your Dragon" Burgess + contributors (Adafruit_NeoPixel) | LGPL (attribution in source headers) | `src/hal/impl/shared/drivers/neopixel/COPYING`, `src/hal/impl/shared/drivers/neopixel/jh_neopixel.h` |
| `DS3231` | RTC DS3231 backend (`hal_rtc`) | Eric Ayars, Andrew Wickert, Jean-Claude Wippler, Northern Widget contributors | Public domain declarations in source headers | `src/hal/impl/shared/drivers/ds3231/ds3231.h`, `src/hal/impl/shared/drivers/ds3231/ds3231.cpp` |
| DHT11/DHT22 driver (ported) | `hal_dht` | Bonezegei (Jofel Batutay) | Attribution in source header | `src/hal/impl/shared/drivers/dht/hal_dht.cpp` |
| `MCP2515` | `hal_can` backend | Seeed Technology (Loovee), Cory J. Fowler | LGPL (`license.txt` included) | `src/hal/impl/shared/drivers/mcp2515/license.txt` and `src/hal/impl/shared/drivers/mcp2515/mcp2515_driver.h` |
| `arduino-wireguard-pico-w` | `hal_wireguard` backend | Kenta Ida (original API), Daniel Hope (core), Marcin Kielesiński (RP2040/Pico W port) | BSD-3-Clause | `src/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/LICENSE` |
| `PubSubClient` | `hal_mqtt` backend | Nick O'Leary | MIT | `src/hal/impl/rp2040/frameworks/PubSubClient/LICENSE.txt` |
| TinyGPS++ (ported) | `hal_gps` NMEA parsing logic ported into `gps_nmea_parser` | Mikal Hart | LGPL-2.1+ (attribution in source headers; library no longer bundled/linked) | `src/hal/impl/shared/frameworks/gps/gps_nmea_parser.cpp` |

Note: `impl/shared/drivers/display/Fonts/` includes additional per-font notices in
font headers (e.g. `TomThumb.h`, `Tiny3x3a2pt7b.h`).

### Integration changes and rationale

| Area | What changed | Why |
|---|---|---|
| Include wiring | HAL modules include bundled dependencies from local `drivers/` and `frameworks/` paths; intra-module includes were rewired to local relative paths. | Keeps third-party code encapsulated inside HAL internals and avoids global include namespace leaks. |
| Conditional compilation | Driver `.cpp` files are wrapped with module-level `HAL_ENABLE_*` guards. | Disabled-by-default modules remove both HAL wrappers and third-party backend code from the build. |
| SPI synchronization | Drivers using SPI transactions now integrate `hal_spi_lock`/`hal_spi_unlock` where needed (CAN, shared TFT panel drivers). | Prevents cross-thread/cross-core SPI transaction interleaving. |
| I2C synchronization | Drivers doing I2C traffic integrate `hal_i2c_lock_bus`/`hal_i2c_unlock_bus` and bus mapping where needed. | Prevents mixed bus-0/bus-1 transactions and improves determinism under concurrency. |
| Per-driver mutexes | Selected drivers/wrappers now own mutexes for multi-step operations (`MCP2515`, `MAX6675`, `MCP9600`, HAL wrappers). | Reduces race conditions in read/modify/write and multi-call command sequences. |
| Second I2C controller support | HAL I2C APIs and driver adapters use bus index 0/1 for the target's first and second hardware controllers. | Allows second controller usage without bypassing HAL thread-safety. |
| Shared display stack | The vendored Adafruit GFX/ILI9341/ST77xx/SSD1306/BusIO libraries were replaced by a portable in-tree display stack (`impl/shared/drivers/display/`) built only on HAL SPI/I2C/GPIO. The public facade covers ILI9341, ST77xx/GC9A01, SSD1306-family, SSD1331/SSD135x and ST7567 displays through legacy GFX and/or capability-advertised raw writes. | One Arduino-free implementation drives every backend (RP2040, STM32G474) identically and compiles out when the display module is disabled. |
| Portable NMEA engine | `hal_gps` uses an in-tree NMEA parser (`impl/shared/frameworks/gps/gps_nmea_parser.cpp`), with parsing logic ported from TinyGPS++ (LGPL); TinyGPS++ itself is no longer bundled or linked. | No Arduino dependency, so the parser runs on RP2040 and STM32G474 alike; compiles out with the GPS module disabled. |
| WiFiUDP wrapper | `hal_udp` wraps Arduino-pico `WiFiUDP` and is compile-gated by `HAL_ENABLE_UDP`. | UDP support stays opt-in and adds zero code size when disabled. |
| WireGuard bundling | `hal_wireguard` uses bundled `arduino-wireguard-pico-w` sources copied from the local sibling repository and gated by `HAL_ENABLE_WIREGUARD`. | Keeps WireGuard integration deterministic and fully local/offline while preserving opt-in code size. |
| PubSubClient bundling | `hal_mqtt` uses bundled PubSubClient source gated by `HAL_ENABLE_MQTT` in the driver translation unit. | MQTT support is opt-in and adds zero code size when disabled. |

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

This helper is implemented for the RP2040, STM32G474 and mock backends.

---

## Examples

For quick-start usage examples, prefer the examples in [README.md](../README.md).

Typical flows covered there:

- GPIO plus timing
- I2C plus EEPROM
- WiFi plus NTP/system time
- WiFi plus UDP datagrams
- DS18B20 request/poll/read non-blocking workflow
- display initialisation

This file keeps the lower-level API reference and migration mapping.

---

## Host-test coverage

Host/mock tests are built via CMake and validate the desktop-facing mock
backend together with selected utility modules.

Covered test targets include:

- `test_hal_gpio`, `test_hal_adc`, `test_hal_pwm`, `test_hal_spi`,
  `test_hal_timer`, `test_hal_onewire`, `test_hal_ds18b20`, `test_hal_dht`,
  `test_hal_pga2311`
- `test_stm32_hal_timer` validates the real STM32G474 timer backend in a
  host-driven build, including callback rescheduling and managed timers.
- `test_hal_i2c`, `test_hal_i2c_slave`, `test_hal_rgb_led`, `test_hal_external_adc`, `test_ads1x15_driver`, `test_bh1750_driver`, `test_hal_gps`, `test_hal_system`, `test_hal_bits`
- `test_hal_serial`, `test_hal_serial_session`, `test_hal_serial_session_vocabulary`, `test_hal_uart`, `test_hal_swserial`
- `test_hal_can`, `test_hal_thermocouple`, `test_hal_display`
- `test_hal_eeprom`, `test_hal_kv`, `test_hal_wifi`, `test_hal_littlefs`, `test_hal_sdlogger`, `test_hal_udp`, `test_hal_wireguard`, `test_hal_mqtt`, `test_hal_ota`, `test_hal_time`, `test_hal_crypto`
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
| `Serial.flush()` after every write | `hal_serial_set_flush(true)` when an extra RP2040 USB CDC flush/task poll is explicitly wanted; default is `false` |
| `Serial.available()` | `hal_serial_available()` |
| `Serial.read()` | `hal_serial_read()` |
| `deb(fmt, ...)` | `hal_deb(fmt, ...)` - macro alias still available via tools.h |
| `derr(fmt, ...)` | `hal_derr(fmt, ...)` - macro alias still available via tools.h |
| repeated noisy error logs | `hal_derr_limited(source, fmt, ...)` - per-source rate-limited logging |
| `setDebugPrefix(p)` | `hal_deb_set_prefix(p)` - macro alias via tools.h |
| manual `concatStrings(buf, ..., MODULE_NAME, ":")` + `setDebugPrefix(buf)` | `setDebugPrefixWithColon(MODULE_NAME)` |
| `mutex_t` + pico SDK mutex | `hal_mutex_t` + `hal_mutex_create/lock/unlock/destroy` |
| `constrain(v, lo, hi)` | `hal_constrain(v, lo, hi)` (type-independent macro from `hal/hal_system.h` / `hal/hal_math.h`); `pid_clamp` is a backward-compat alias |
| `map(x, ...)` | `hal_map(x, in_min, in_max, out_min, out_max)` (type-independent macro from `hal/hal_system.h` / `hal/hal_math.h`) |
| `min(a, b)` | `hal_min(a, b)` (macro, in `hal/hal_config.h`) |
| `max(a, b)` | `hal_max(a, b)` (macro, in `hal/hal_config.h`) |
| `EEPROM.begin(size)` | `hal_eeprom_init(HAL_EEPROM_FLASH, size, 0)` |
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


---

*Next: [GPIO, ADC, PWM](05_gpio_adc_pwm.md)*
