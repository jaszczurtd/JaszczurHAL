# Multicore safety, drivers, and logging

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## Multicore safety policy

JaszczurHAL supports RP2040/RP2350 and ESP32-S3 dual-core systems, using both
core 0 and core 1 where available. STM32G474 is supported as well, and general
mutex protection is available through the FreeRTOS-enabled path.

The following design rules apply:

### Portable application entry

The native runtime owns `main()` when `HAL_PROVIDE_APP_ENTRY` is enabled.
`app_start()` runs once before task dispatch. On bare-metal RP,
`app_task0()` runs as the core-0 super-loop; `HAL_ENABLE_APP_TASK1` launches
core 1 through `multicore_launch_core1()`, registers both participating cores
with the flash transaction coordinator, and repeatedly dispatches
`app_task1()`. On RP FreeRTOS SMP, the same hooks become tasks pinned to cores
0 and 1. On STM32G474, bare-metal dispatch is cooperative in one super-loop,
while FreeRTOS creates independent task0 and optional task1 tasks. ESP-IDF
already owns the scheduler; HAL creates task0 on core 0 by default and optional
task1 on core 1, with explicit core overrides or `-1` for no affinity.

The coordinator serializes native flash mutations, makes the other core safe,
pauses TinyUSB, rejects active DMA and XIP-resident operation state, masks local
interrupts and restores acquired runtime state on every cleanup path. It uses
the Pico SDK multicore helper in bare-metal firmware and its scheduler-aware
helper under FreeRTOS SMP. Native EEPROM commits and all LittleFS program/erase
callbacks use this shared transaction path exclusively.

### Initialisation: single-core only

All `*_init()`, `*_create()`, and `*_deinit()` / `*_destroy()` functions must be
called from **one core only** (typically core 0 during `app_start()`). These
functions allocate from static pools, configure hardware peripherals, and
establish internal state.  They are **not** protected by mutexes because:

- pool allocation is inherently single-shot (done once at boot),
- hardware peripheral setup must complete before use,
- adding mutex overhead to init paths provides no practical benefit when the
  documented guarantee is respected.

### Runtime: concurrent hardware backends

After initialisation, most HAL runtime APIs support concurrent callers on
RP2040/RP2350 dual-core firmware and on supported FreeRTOS builds, including
STM32G474 and the delivered ESP32-S3 backend set. Each module documents its
exact thread-safety guarantee in the per-module section below. The general
pattern is:

- **Per-instance mutexes** protect handle-based APIs (`hal_can`, `hal_thermocouple`, `hal_rtc`, `SmartTimers`).
- **Per-bus mutexes** protect shared communication buses (`hal_spi`, `hal_i2c`).
- **Singleton mutexes** protect global modules (`hal_eeprom`, `hal_display`, `hal_gps`, `hal_external_adc`, `hal_wifi`, `hal_udp`, `hal_wireguard`, `hal_mqtt`, `hal_kv`, debug serial).
- **Stateless helpers** (`hal_bits`, `hal_math`, pure `hal_time` helpers,
  `hal_crypto`, `hal_constrain`, `hal_map`) are inherently thread-safe.

Singleton and per-bus mutexes use an internal atomic create-once fallback, so
two FreeRTOS tasks or hardware cores cannot publish different locks for the
same module. Module init/begin calls still remain the preferred place to create
those locks before normal runtime sharing.

Modules documented as **"Not thread-safe"** (`hal_uart`, the optional
`hal_time` NTP/system-time surface, `pidController`) must be serialized by the
caller or used from a single core.

### Mock backend

Mock implementations (`impl/.mock/`) are designed for deterministic
single-threaded unit tests and do not provide hardware-equivalent cross-thread
synchronization. The optional FreeRTOS POSIX runtime test separately exercises
the scheduler, mutex, delay, and create-once integration on the host.

---

## Drivers and frameworks

Bundled or ported low-level drivers live under `src/hal/impl/rp2040/drivers/`
or the relevant thematic directory under `src/hal/`.
Bundled high-level integration frameworks live under the relevant thematic
directory under `src/hal/`. Target-specific glue remains under `src/hal/impl/`.
These sources are HAL-internal implementation details (not public API).

### Inventory, authors and license paths

| Driver folder | HAL usage | Upstream author(s) | License | License path in repo |
|---|---|---|---|---|
| GFX engine (ported) | `hal_display` rendering (geometry, text, bitmaps) ported into `hal/display/drivers/jh_gfx.*` | Limor Fried (Ladyada) + contributors (Adafruit GFX) | BSD-2-Clause (attribution in source headers; library no longer bundled/linked) | `src/hal/display/drivers/jh_gfx.h` |
| ILI9341 driver (ported) | TFT backend (`HAL_DISPLAY_ILI9341`) ported into `hal/display/drivers/ili9341_driver.*` | Limor Fried (Ladyada) (Adafruit ILI9341) | BSD-2-Clause (attribution in source headers) | `src/hal/display/drivers/ili9341_driver.h` |
| ST77xx/GC9A01 driver (ported) | ST7735/ST7789/ST7796S backends plus Zephyr-informed GC9A01 round-TFT support ported into `hal/display/drivers/st77xx_driver.*` | Limor Fried (Ladyada) (Adafruit ST7735/ST7789), Zephyr GC9x01x driver used as the GC9A01 reference checklist | BSD-2-Clause for the original ST77xx path; GC9A01 behavior port notes reference Apache-2.0 Zephyr sources | `src/hal/display/drivers/st77xx_driver.h` |
| SSD1306-family driver (ported) | OLED backend (`HAL_ENABLE_SSD1306`) ported into `hal/display/drivers/ssd1306_driver.*` and extended for SSD1309/SSD1315/SH1106/CH1115 variants | Limor Fried (Ladyada) + contributors (Adafruit SSD1306), Zephyr display-driver behavior used as a reference checklist | BSD-2-Clause (attribution in source headers) | `src/hal/display/drivers/ssd1306_driver.h` |
| SSD1331/SSD135x RGB OLED drivers (ported) | Shared RGB565 OLED drivers over HAL SPI/GPIO (`HAL_ENABLE_SSD1331`, `HAL_ENABLE_SSD135X`) | Zephyr `display_ssd1331.c` and `display_ssd135x.c` behavior used as the local reference checklist | Apache-2.0 reference behavior, implemented in-tree against HAL transport | `src/hal/display/drivers/rgb_oled_driver.h` |
| ST7567 LCD driver (ported) | Shared monochrome LCD driver over HAL I2C or SPI/GPIO (`HAL_ENABLE_ST7567`) | Zephyr `display_st7567.c` and `display_st7567_regs.h` behavior used as the local reference checklist | Apache-2.0 reference behavior, implemented in-tree against HAL transport | `src/hal/display/drivers/st7567_driver.h` |
| SSD16xx/UC81xx EPD drivers (ported) | Shared monochrome e-paper drivers over HAL SPI/GPIO (`HAL_ENABLE_SSD16XX`, `HAL_ENABLE_UC81XX`) | Zephyr `ssd16xx.c`, `ssd16xx_regs.h`, `uc81xx.c` and `uc81xx_regs.h` used as the local protocol/state-machine reference | Apache-2.0, adapted to status-first HAL transport and public configs | `src/hal/display/drivers/ssd16xx_driver.h`, `src/hal/display/drivers/uc81xx_driver.h` |
| NeoPixel core (ported) | `hal_rgb_led` | Phil "Paint Your Dragon" Burgess + contributors (Adafruit_NeoPixel) | LGPL (attribution in source headers) | `src/hal/gpio/neopixel/COPYING`, `src/hal/gpio/neopixel/jh_neopixel.h` |
| `DS3231` | RTC DS3231 backend (`hal_rtc`) | Eric Ayars, Andrew Wickert, Jean-Claude Wippler, Northern Widget contributors | Public domain declarations in source headers | `src/hal/rtc/ds3231/ds3231.h`, `src/hal/rtc/ds3231/ds3231.cpp` |
| DHT11/DHT22 driver (ported) | `hal_dht` | Bonezegei (Jofel Batutay) | Attribution in source header | `src/hal/temperature/dht/hal_dht.cpp` |
| `MCP2515` | `hal_can` backend | Seeed Technology (Loovee), Cory J. Fowler | LGPL (`license.txt` included) | `src/hal/can/mcp2515/license.txt` and `src/hal/can/mcp2515/mcp2515_driver.h` |
| Shared WireGuard/lwIP engine | `hal_wireguard` backend | Kenta Ida (original API), Daniel Hope (core), Marcin Kielesiński (RP2040/Pico W and shared HAL ports) | BSD-3-Clause | `src/hal/network/wireguard/core/LICENSE` |
| `PubSubClient` | `hal_mqtt` backend | Nick O'Leary | MIT | `src/hal/network/mqtt/PubSubClient/LICENSE.txt` |
| TinyGPS++ (ported) | `hal_gps` NMEA parsing logic ported into `gps_nmea_parser` | Mikal Hart | LGPL-2.1+ (attribution in source headers; library no longer bundled/linked) | `src/hal/gps/gps_nmea_parser.cpp` |

Note: `hal/display/drivers/Fonts/` includes additional per-font notices in
font headers (e.g. `TomThumb.h`, `Tiny3x3a2pt7b.h`).

### Integration changes and rationale

| Area | What changed | Why |
|---|---|---|
| Include wiring | HAL modules include bundled dependencies from local `drivers/` and `frameworks/` paths; intra-module includes were rewired to local relative paths. | Keeps third-party code encapsulated inside HAL internals and avoids global include namespace leaks. |
| Conditional compilation | Driver `.cpp` files are wrapped with module-level `HAL_ENABLE_*` guards. | Disabled-by-default modules remove both HAL wrappers and third-party backend code from the build. |
| SPI synchronization | Drivers using SPI transactions now integrate `hal_spi_lock`/`hal_spi_unlock` where needed (CAN, shared TFT panel drivers). | Prevents cross-thread/cross-core SPI transaction interleaving. |
| I2C synchronization | Drivers doing I2C traffic integrate `hal_i2c_lock_bus`/`hal_i2c_unlock_bus` and bus mapping where needed. | Prevents mixed bus-0/bus-1 transactions and improves determinism under concurrency. |
| Per-driver mutexes | Selected drivers/wrappers now own mutexes for multi-step operations (`MCP2515`, `MAX6675`, `MCP9600`, HAL wrappers). | Reduces race conditions in read/modify/write and multi-call command sequences. |
| Shared RTC facade | `hal_rtc.cpp` owns the handle pool, validation, mutexes, epoch conversion, status mapping, and compatibility wrappers; link-time providers supply shared PCF8563/DS3231 I2C behavior, the STM32G474 backup-domain RTC, the RP AON timer, or mock storage. | Keeps chip protocol, target registers, and test injection behind provider operations while allowing I2C and native providers to share one facade. |
| Separate low-power API | `hal_power.h` owns portable states, policies, capabilities, wake reasons, and prepare/resume callbacks; target backends own WFI/STOP/Standby details and reuse the internal RTC relative-wake interface. | Prevents RTC device ownership from absorbing processor, clock-tree, peripheral-suspend, reset, or scheduler policy. |
| Shared GPS facade | `hal_gps.cpp` selects HAL UART or SoftwareSerial at compile time and owns transport initialization, polling, framing fallback and availability. The shared GPS engine owns parsing, locking, diagnostics and every fix getter, including the mock injection path. | Removes identical RP2040/STM32G474 transport facades and the mock getter copy while preserving transport selection and deterministic injection. |
| Shared serial/debug core | `hal_serial.cpp` owns formatting, prefixes, timestamps, mute/rate-limit state, the ISR SPSC ring, net-console mirroring, lazy create-once mutexes and public serial/debug entry points. Link-time ports own only RP USB CDC, ESP32-S3 USB Serial/JTAG VFS, STM32 USART2/stdout, or mock capture/RX transport. | Keeps one formatting/state implementation while preserving target line endings, transport-specific flush behavior and atomic cross-task/cross-core message boundaries. |
| Second I2C controller support | HAL I2C APIs and driver adapters use bus index 0/1 for the target's first and second hardware controllers. | Allows second controller usage without bypassing HAL thread-safety. |
| Shared display stack | The vendored Adafruit GFX/ILI9341/ST77xx/SSD1306/BusIO libraries were replaced by a portable in-tree display stack (`hal/display/drivers/`) built only on HAL SPI/I2C/GPIO. The public facade covers ILI9341, ST77xx/GC9A01, SSD1306-family, SSD1331/SSD135x and ST7567 displays through GFX and capability-advertised raw writes. | One shared implementation drives RP2040 and STM32G474 identically and compiles out when the display module is disabled. |
| Portable NMEA engine | `hal_gps` uses an in-tree NMEA parser (`hal/gps/gps_nmea_parser.cpp`), with parsing logic ported from TinyGPS++ (LGPL); TinyGPS++ itself is no longer bundled or linked. | The same parser/getter engine runs on RP2040, STM32G474 and mock, and compiles out with the GPS module disabled. |
| UDP transport | `hal_udp` uses the shared lwIP raw engine and is compile-gated by `HAL_ENABLE_UDP`. | UDP support stays opt-in and adds zero code size when disabled. |
| WireGuard bundling | `hal_wireguard` uses a shared lwIP engine gated by `HAL_ENABLE_WIREGUARD`; target hooks provide the underlay netif, stack context, entropy and time. | Keeps WireGuard deterministic and offline while sharing route/timer/teardown behavior between supported host-lwIP targets. |
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

void app_start(void) {
        hal_debug_init(115200, NULL);
        hal_debug_set_timestamp_hook(app_ts_hook, NULL);
}
```

---

## Time conversion helper

`hal_time_from_components(int year, int month, int day, int hour, int minute, int second)`
converts date/time components to Unix epoch seconds.

Validation:

- returns `0` for out-of-range values (year < 1970, invalid month/day/time, or
  Unix epoch beyond `UINT32_MAX`)
- supports leap-year rules (including century exceptions)

The compatibility API returns `0` both for errors and for the valid Unix epoch
start. Internally, the shared `hal/time/jh_calendar` core uses
`hal_status_t` and a 64-bit epoch so callers can distinguish those cases. The
same core validates and converts RTC, PCF8563 and DS3231 dates on RP2040,
STM32G474 and mock builds.

The always-available `hal_time` surface also owns the former `tools.cpp` time
algorithms:

- `hal_time_is_daylight_saving_time(...)` applies the date-only CET/CEST
  last-Sunday policy and rejects invalid Gregorian dates
- `hal_time_adjust_cet_cest(...)` applies the corresponding offset and
  normalizes date rollover
- `hal_time_is_in_range(...)` checks a half-open `[start, end)` interval
- `hal_time_extract_minutes(...)` splits a minute count with optional outputs

The established `isDaylightSavingTime()`, `adjustTime()`,
`is_time_in_range()`, and `extract_time()` utilities remain source-compatible
wrappers and contain no calendar or interval logic.

---

## Examples

For quick-start usage examples, prefer the examples in
[README.md](../../README.md).

Typical flows covered there:

- GPIO plus timing
- I2C plus EEPROM
- WiFi plus NTP/system time
- WiFi plus UDP datagrams
- DS18B20 request/poll/read non-blocking workflow
- display initialisation

This file keeps the lower-level API reference and portable API map.

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
- `test_hal_serial`, `test_hal_sc_auth`, `test_hal_serial_session`,
  `test_hal_serial_session_vocabulary`, `test_jh_security_primitives`,
  `test_security_architecture`, `test_serial_architecture`, `test_hal_uart`,
  `test_hal_swserial`; `test_freertos_posix_runtime` also mixes concurrent
  serial/debug emitters and validates complete message boundaries.
- `test_hal_can`, `test_hal_thermocouple`, `test_hal_display`
- `test_hal_eeprom`, `test_hal_kv`, `test_hal_wifi`, `test_hal_littlefs`, `test_hal_sdlogger`, `test_hal_udp`, `test_hal_wireguard`, `test_hal_mqtt`, `test_hal_ota`, `test_hal_time`, `test_hal_crypto`
- `test_SmartTimers`, `test_pidController`, `test_multicoreWatchdog`, `test_tools`
- `hal_soft_timer_*` and `hal_pid_controller_*` are thin wrappers over these utility cores.

Build/run entry point:

```bash
cmake -S . -B .build/host
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
```

---

## Portable API map

| Domain | Public API |
|---|---|
| Monotonic time and delays | `hal_millis()`, `hal_micros()`, `hal_micros64()`, `hal_delay_ms()`, `hal_delay_us()` |
| System state | `hal_get_free_heap()`, `hal_read_chip_temp()`, `hal_watchdog_*()` |
| GPIO and interrupts | `hal_gpio_set_mode()`, `hal_gpio_write()`, `hal_gpio_read()`, `hal_gpio_attach_interrupt()` |
| ADC and PWM | `hal_adc_*()`, `hal_pwm_*()`, `hal_pwm_freq_*()` |
| Synchronization | `hal_mutex_*()`, `hal_critical_section_enter()`, `hal_critical_section_exit()` |
| USB serial and debug | `hal_serial_*()`, `hal_debug_init()`, `hal_deb()`, `hal_derr()`, `hal_derr_limited()` |
| Persistent bytes | `hal_eeprom_*()` |
| Key/value persistence | `hal_kv_*()` |
| Filesystem and SD logging | `hal_littlefs_*()`, `hal_sdlogger_*()` |
| Math and bit helpers | `hal_constrain()`, `hal_map()`, `hal_min()`, `hal_max()`, `bitSet()`, `bitClear()`, `bitRead()` |
| Displays | `hal_display_*()` |

---


---

*Next: [GPIO, ADC, PWM](05_gpio_adc_pwm.md)*
