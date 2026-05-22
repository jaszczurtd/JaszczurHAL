# JaszczurHAL

Author: Marcin 'Jaszczur' Kielesinski

JaszczurHAL is a hardware abstraction layer and utility library for embedded projects.

Today the most complete backend targets RP2040 boards through Arduino-pico (STM32G474 target is in the works), but the long-term goal is to bring more targets.

## Why this exists

Typical embedded projects start as quick written code and later become harder to evolve because hardware access is mixed with business logic.

JaszczurHAL introduces a practical boundary:

- application layer: portable logic,
- HAL layer: one place for hardware/API details,
- mock layer: deterministic host testing.

This reduces lock-in to one runtime and makes migration to other SDKs much easier.

## Public include

Use:

```cpp
#include <JaszczurHAL.h>
```

The internal header:

```cpp
#include <hal/hal.h>
```

is still available for advanced/internal usage.

Utility-only includes are also available:

```cpp
#include <tools.h>    // C++ utility aggregator
```

```c
#include <tools_c.h>  // C-compatible utility API
```

## What you get (high-level)

- Hardware abstraction layer for common embedded peripherals and system services
- Consistent, portable APIs that keep hardware details separate from application logic
- Optional modules controlled by compile-time flags (`HAL_DISABLE_*`, `HAL_ENABLE_*`)
- Built-in mock backend for deterministic host/unit testing
- Utility toolkit for common embedded patterns (timers, PID, watchdog, helpers)
- Optional connectivity/security/storage stack for connected firmware projects

## Supported modules and drivers (overview)

- Core HAL domains: GPIO, ADC, PWM, timers, system, synchronization, serial I/O
- Peripheral domains: SPI/I2C/UART, CAN, displays, RGB LEDs, thermocouples, digital temperature sensors, GPS, external ADC, EEPROM and key-value storage
- Connected domains (opt-in): WiFi, NTP/system time, UDP, WireGuard, MQTT, OTA, LittleFS, crypto/auth helpers
- Third-party drivers/frameworks are bundled inside the library and compiled only when related modules are enabled

## Thread safety (overview)

- On Arduino backend, runtime HAL calls are generally multicore-safe and internally synchronized
- As a project rule, initialization and teardown (`init/create/destroy/deinit`) should be done from one core
- Mock backend targets deterministic single-threaded tests rather than true concurrent synchronization
- Exact guarantees are documented per module in `JaszczurHAL_API.md`

For detailed signatures, module contracts, backend notes, and test coverage, see `JaszczurHAL_API.md`.

## Library structure

```text
src/
  JaszczurHAL.h            # primary public include
  HAL_FLAGS.txt            # HAL_DISABLE_* / HAL_ENABLE_* summary
  libConfig.h              # backward-compat include
  tools.h, tools_c.h       # utility aggregators (C++ / C)
  arduino_host_stubs/      # host-build Arduino compatibility headers
  hal/                     # HAL headers + common wrappers + backend dispatch
    impl/
      arduino/             # Arduino/RP2040 backend
      .mock/               # deterministic host/test backend
      drivers/             # bundled third-party, multithread safe Arduino drivers, (pico compatible)
      frameworks/          # bundled high-level integrations (WireGuard/MQTT/GPS parser, etc)
  utils/                   # helper modules and bundled optional utilities
tests/                     # host unit tests (CMake + Unity)
vscode-templates/          # ready-to-use VS Code project configurations
  windows/                 # Windows template (Python + Arduino CLI)
  linux/                   # Linux/macOS template (Bash)
```

Detailed per-file layout is maintained in `JaszczurHAL_API.md` (`## Library structure`).

## Quick start

```cpp
#include <JaszczurHAL.h>

void setup() {
    hal_debug_init(115200);
    hal_gpio_set_mode(25, HAL_GPIO_OUTPUT);
}

void loop() {
    hal_gpio_write(25, true);
    hal_delay_ms(200);
    hal_gpio_write(25, false);
    hal_delay_ms(200);
}
```

## Debug helper quick example

For codebases that already use `debugInit()`, `deb(...)`, and module prefixes,
the utility layer provides a shorthand that replaces manual
`concatStrings(..., MODULE_NAME, ":")` setup:

```c
#include <tools_c.h>

void setup(void) {
  debugInit();
  setDebugPrefixWithColon("ECU");
  deb("ready");
}
```

`setDebugPrefixWithColon(...)` appends `:` to the provided module name and
forwards the final prefix to `hal_deb_set_prefix(...)`.

## Soft timer table quick example

```c
#include <hal/hal_soft_timer.h>
#include <hal/hal_system.h>

static hal_soft_timer_t timerFast = NULL;
static hal_soft_timer_t timerSlow = NULL;

static void onFast(void) {
  // fast periodic work
}

static void onSlow(void) {
  // slow periodic work
}

static const hal_soft_timer_table_entry_t timers[] = {
  { &timerFast, onFast, 50 },
  { &timerSlow, onSlow, 1000 }
};

void setup(void) {
  bool ok = hal_soft_timer_setup_table(timers,
                     COUNTOF(timers),
                     hal_watchdog_feed,
                     2);
  if (!ok) {
    hal_derr("timer table setup failed");
  }
}

void loop(void) {
  (void)hal_soft_timer_tick_table(timers, COUNTOF(timers));
}
```

`hal_soft_timer_setup_table(...)` and `hal_soft_timer_tick_table(...)` return `false` for invalid input (`table == NULL` or `count == 0`) and log via `hal_derr(...)`.

## Crypto quick example

```c
#include <JaszczurHAL.h>

static const uint8_t key[HAL_CHACHA20_KEY_BYTES] = {0};
static const uint8_t nonce[HAL_CHACHA20_NONCE_BYTES] = {0};

void demo_crypto(void) {
  const uint8_t msg[] = "hello";
  uint8_t cipher[sizeof(msg)] = {0};
  uint8_t plain[sizeof(msg)] = {0};
  uint8_t tag[HAL_CHACHA20_POLY1305_TAG_BYTES] = {0};
  char md5_hex[HAL_MD5_HEX_BUF_SIZE] = {0};

  (void)hal_md5_hex(msg, sizeof(msg) - 1u, md5_hex, sizeof(md5_hex));
  (void)hal_chacha20_poly1305_encrypt(key, nonce, NULL, 0u,
                                      msg, sizeof(msg), cipher, tag);
  (void)hal_chacha20_poly1305_decrypt(key, nonce, NULL, 0u,
                                      cipher, sizeof(msg), tag, plain);
}
```

## Module selection (quick)

To exclude optional subsystems, define `HAL_DISABLE_*` flags in a project-local
`hal_project_config.h`:

```c
#pragma once
#define HAL_DISABLE_WIFI
#define HAL_DISABLE_TIME
#define HAL_DISABLE_GPS
```

For the complete flag matrix, dependency propagation rules, and `HAL_ENABLE_*` options,
see:

- `JaszczurHAL_API.md`
- `src/HAL_FLAGS.txt`

## Host tests (quick)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Detailed suite coverage, mock behavior notes, and testing workflow are in
`JaszczurHAL_API.md`.

## VS Code Development Environment

`vscode-templates/` contains ready-to-use VS Code project configurations for Arduino development on Windows and Linux/macOS:

### Features

- One-key build, upload, and debugging
- Persistent serial monitor (auto-reconnect on device replug)
- Cortex-Debug live debugging with breakpoints
- IntelliSense with full RP2040/RP2350 API
- Support for 6+ Raspberry Pi Pico variants
- UF2 bootloader upload mode
- Board selection with custom clock/optimization settings

### Platform-Specific Templates

- **[Linux/macOS](vscode-templates/linux/)** - Bash-based build and deployment scripts
  - Lightweight bash implementation
  - Compatible with standard GNU toolchains
  - Same feature set as Windows (build, upload, debug, monitor)

- **[Windows](vscode-templates/windows/)** - Complete setup with Python build orchestration, Arduino CLI integration, debugging, and serial monitor
  - Python-based build system for cross-platform consistency
  - Smart serial monitor with auto-reconnection (VID:PID aware)
  - Interactive board/options selector
  - Cortex-Debug integration for live debugging

### Quick Start

1. Choose your platform: [Windows](vscode-templates/windows/) or [Linux](vscode-templates/linux/)
2. Copy the template to your project directory
3. Configure environment variables (Arduino CLI path, serial port)
4. Start building with `Ctrl+Shift+1` (or `Ctrl+Shift+2` to upload)

See [vscode-templates/README.md](vscode-templates/README.md) for detailed setup, or visit your platform-specific folder.

## Building as a static library (.a)

The complete guide for compiling JaszczurHAL to a linkable static library
(`libJaszczurHAL.a`) was moved to:

- [lib_compilation.md](lib_compilation.md)

## Documentation

Primary docs:

- API reference: `JaszczurHAL_API.md`
- Changelog: `CHANGELOG.md`
- Build-time flags summary: `src/HAL_FLAGS.txt`
- Linkable static library build guide: [lib_compilation.md](lib_compilation.md)
- VS Code setup (Windows & Linux): `vscode-templates/README.md`
  - Windows template: `vscode-templates/windows/README.md`
  - Linux template: `vscode-templates/linux/README.md`

`JaszczurHAL_API.md` is the canonical source for detailed API signatures,
module semantics, multicore/thread-safety policy, driver inventory/licenses,
examples, and host-test coverage.

## Notes and credits

- SmartTimers is based on [Nettigo Timers](https://github.com/nettigo/Timers)
  (fork of [garthoff/Timers](https://github.com/garthoff/Timers)).
- Unity test framework sources are bundled in src/:
  [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity)
- cJSON/cJSON_Utils are bundled and optional via HAL_ENABLE_CJSON:
  [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
- Bundled dependency authors (from upstream LICENSE/README files in src/hal/impl/arduino/drivers/ and src/hal/impl/arduino/frameworks/):
- [ADS1X15](https://github.com/RobTillaart/ADS1X15) - Rob Tillaart
- [Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO) - Adafruit Industries
- [Adafruit_GFX_Library](https://github.com/adafruit/Adafruit-GFX-Library) - Limor Fried (Ladyada) for Adafruit Industries
- [Adafruit_ILI9341](https://github.com/adafruit/Adafruit_ILI9341) - Limor Fried (Ladyada) for Adafruit Industries
- [Adafruit_MCP9600](https://github.com/adafruit/Adafruit_MCP9600) - Kevin Townsend and Limor Fried for Adafruit Industries
- [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) - Phil "Paint Your Dragon" Burgess (with contributions by PJRC and Michael Miller)
- [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306) - Limor Fried (Ladyada), with contributions by Michael Gregg and Andrew Canaday
- [Adafruit_ST7735_and_ST7789_Library](https://github.com/adafruit/Adafruit-ST7735-Library) - Limor Fried (Ladyada) for Adafruit Industries
- [Adafruit_Zero_DMA_Library](https://github.com/adafruit/Adafruit_ZeroDMA) - Phil "PaintYourDragon" Burgess for Adafruit Industries (with ASF-derived parts from Atmel Corporation)
- [MAX6675](https://github.com/adafruit/MAX6675-library) - Limor Fried for Adafruit Industries
- [OneWire](https://github.com/PaulStoffregen/OneWire) - Jim Studt (original), maintained by Paul Stoffregen
- [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) - Miles Burton
- [MCP2515](https://github.com/coryjfowler/MCP_CAN_lib) - Loovee / Seeed Technology, with contributions by Cory J. Fowler
- [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) - Mikal Hart
- [arduino-wireguard-pico-w](https://github.com/jaszczurtd/arduino-wireguard-pico-w) - Kenta Ida (original WireGuard-ESP32 API), Daniel Hope (upstream WireGuard core), Marcin Kielesiński (RP2040/Pico W port)
- [PubSubClient](https://github.com/knolleary/pubsubclient) - Nick O'Leary
