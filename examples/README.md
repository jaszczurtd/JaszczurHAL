# JaszczurHAL Examples

## Build System Overview

Each numbered example directory is a dispatcher-backed firmware project with its
own `.vscode/jaszczurhal.project.json`. You can open any `examples/NN_name/`
folder directly in VS Code and use the same JaszczurHAL tasks as downstream
projects: Build, Upload, Monitor, Clean, Config Dump, and GUI/terminal board
selection.

The quality gate uses `scripts/examples_dispatcher.py` to discover those
manifests and build the examples through `vscode/entry/jh-vscode` and
`cmake/jh_firmware_project`. Two concrete targets are supported today:

| Backend | Toolchain | Method |
|---------|-----------|--------|
| **RP2040/RP2350** | `arduino-cli` + `rp2040:rp2040` core | Dispatcher auto-generates the Arduino compatibility sketch and invokes `arduino-cli compile` |
| **STM32G474** | `arm-none-eabi-gcc` | Dispatcher builds a bare-metal ELF/BIN/HEX through the reusable STM32 recipe |

`examples/CMakeLists.txt` remains only as a compatibility wrapper around the
same runner. It no longer contains a separate examples-local build recipe.

### Requirements

**RP2040:**
- `arduino-cli` in `$PATH`
- Arduino RP2040 core installed: `arduino-cli core install rp2040:rp2040`
- JaszczurHAL repo cloned (examples reference `../src` via symlinks)

**STM32G474:**
- `arm-none-eabi-gcc` toolchain (13.x+ recommended)
- CMake 3.16+

## How to Build

### Build all examples for RP2040

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
```

### Build all examples for STM32G474

```bash
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Examples that do not declare support for the selected target are reported as
skipped. For example, `38_stm32g474_fdcan_native` is STM32-only, while WiFi/HTTP
examples are RP2040/Pico W-only until STM32 network backends exist.

### Build a single example

```bash
vscode/entry/jh-vscode build --project examples/01_blink --target rp2040
vscode/entry/jh-vscode build --project examples/01_blink --target stm32g474
```

The runner can do the same for quality-gate logs:

```bash
scripts/examples_dispatcher.py build --target rp2040 --example 01_blink
```

### Compatibility CMake Wrapper

```bash
cmake -S examples -B build_examples_rp2040 -DJH_EXAMPLE_TARGET=rp2040
cmake --build build_examples_rp2040

cmake -S examples -B build_examples_stm32g474 -DJH_EXAMPLE_TARGET=stm32g474
cmake --build build_examples_stm32g474
```

This wrapper delegates to `scripts/examples_dispatcher.py`; it exists for older
commands and CI muscle memory, not as a second build system.

## Application Structure

Each example follows the same portable layout:

```
NN_example_name/
├── app.c (or app.cpp)          ← application logic
├── hal_project_config.h        ← feature flags + backend/module config
└── .vscode/                    ← dispatcher manifest, tasks, settings
```

There is no `main()`, no `setup()`/`loop()`, and no `.ino` file written by the
developer. The build system handles all platform boilerplate.

## Entry-Point Contract (`hal_app.h`)

Examples expose portable application functions and let the selected build
backend decide how to enter them:

| Function | Role | Required |
|----------|------|----------|
| `app_start()` | One-time initialization (pin setup, serial begin, etc.) | **Yes** |
| `app_task0()` | Primary super-loop iteration | **Yes** |
| `app_task1()` | Secondary loop iteration | No (`HAL_ENABLE_APP_TASK1` opt-in) |

### Backend mapping

| Backend | Entry implementation | `app_start()` | `app_task0()` | `app_task1()` |
|---------|----------------------|---------------|---------------|---------------|
| RP2040 examples (arduino-pico) | CMake-generated `.ino` | `setup()` | `loop()` (core 0) | `loop1()` only with `HAL_ENABLE_APP_TASK1` |
| STM32G474 examples (bare-metal) | `HAL_PROVIDE_APP_ENTRY` from CMake | Before super-loop | Super-loop body | Only with `HAL_ENABLE_APP_TASK1`, cooperative |
| STM32G474 examples (FreeRTOS) | `HAL_PROVIDE_APP_ENTRY` + `HAL_ENABLE_FREERTOS` | Before scheduler | FreeRTOS task | FreeRTOS task only with `HAL_ENABLE_APP_TASK1` |
| Mock/host apps | `HAL_PROVIDE_APP_ENTRY` when requested | Before super-loop | Super-loop body | Only with `HAL_ENABLE_APP_TASK1`, cooperative |

### RP2040 `loop1()` caution

On arduino-pico, defining `loop1()` is not a harmless placeholder: the core
starts RP2040 core 1 whenever `setup1()` or `loop1()` is linked. The
library-provided entry shim emits `loop1()` only when both
`HAL_PROVIDE_APP_ENTRY` and `HAL_ENABLE_APP_TASK1` are defined.

This keeps single-core sketches single-core by default. For RP2040 examples,
the generated `.ino` wrapper calls only `app_start()` and `app_task0()` unless
the example's compile definitions contain `HAL_ENABLE_APP_TASK1`. Define
`HAL_ENABLE_APP_TASK1` only when the application
intentionally wants the `app_task1()` / `loop1()` core-1 path.

### Minimal application skeleton

**`app.c`:**

```c
#include <hal/hal_app.h>
#include <hal/hal_gpio.h>
#include <hal/hal_system.h>

#define LED_PIN 25u

void app_start(void) {
    hal_gpio_set_mode(LED_PIN, HAL_GPIO_OUTPUT);
}

void app_task0(void) {
    hal_gpio_write(LED_PIN, true);
    hal_delay_ms(500);
    hal_gpio_write(LED_PIN, false);
    hal_delay_ms(500);
}
```

**`hal_project_config.h`:**

```c
#pragma once

/* Entry point is selected by the build system:
 * RP2040 generates setup()/loop(); STM32 defines HAL_PROVIDE_APP_ENTRY. */

/* Enable additional HAL modules as needed: */
// #define HAL_ENABLE_GPS
// #define HAL_ENABLE_WIFI
// #define HAL_ENABLE_I2C
```

That's it - no hand-written `main()`, no hand-written `.ino`. The same `app.c`
compiles and runs on both RP2040 and STM32G474 without changes.

## Platform-Specific Pin Selection

For examples that need different pins per target, use compile-time detection:

```c
#if defined(JH_STM32G474_HW)
#  define LED_PIN 5u    /* Nucleo-G474RE LD2 = PA5 */
#else
#  define LED_PIN 25u   /* Raspberry Pi Pico onboard LED */
#endif
```

common definition `LED_BUILTIN` is also supported.

## WiFi-Capable Examples

Examples 10, 11, 15, 42, 48, 49, 50, 51 and 52 require a WiFi-capable board. The CMake system
automatically selects the `rpipicow` FQBN for these:

```
jh_example(10_mqtt TARGETS rp2040 FQBN "${JH_RP2040_WIFI_FQBN}")
```

## Example List

| # | Name | Targets | Key modules |
|---|------|---------|-------------|
| 01 | blink | rp2040, stm32g474 | GPIO |
| 02 | debug_helper | rp2040, stm32g474 | Serial debug |
| 03 | soft_timer_table | rp2040, stm32g474 | Software timers |
| 04 | crypto | rp2040, stm32g474 | Crypto helpers |
| 05 | modem_A7670E | rp2040 | AT modem, SIMCom |
| 06 | ds18b20 | rp2040, stm32g474 | OneWire, DS18B20 |
| 07 | gps | rp2040 | GPS (SoftwareSerial) |
| 08 | thermocouple | rp2040, stm32g474 | MAX6675/MCP9600 |
| 09 | display_tft | rp2040, stm32g474 | ILI9341/ST7789, draw7Segment |
| 10 | mqtt | rp2040 (WiFi) | WiFi, MQTT |
| 11 | wireguard | rp2040 (WiFi) | WiFi, WireGuard |
| 12 | kv_store | rp2040, stm32g474 | Key-value storage |
| 13 | i2c_slave | rp2040, stm32g474 | I2C slave |
| 14 | uart | rp2040, stm32g474 | UART |
| 15 | wifi | rp2040 (WiFi) | WiFi scan/connect |
| 16 | littlefs | rp2040, stm32g474 | LittleFS |
| 17 | pid_controller | rp2040, stm32g474 | PID controller |
| 18 | rgb_led | rp2040, stm32g474 | NeoPixel/WS2812 |
| 19 | timer_ext | rp2040, stm32g474 | Extended timers |
| 20 | i2c_scan | rp2040, stm32g474 | I2C bus scan |
| 21 | adc_read | rp2040, stm32g474 | ADC |
| 22 | gps_uart | rp2040, stm32g474 | GPS (UART transport) |
| 23 | external_adc_ads1115 | rp2040, stm32g474 | I2C, ADS1115 external ADC |
| 24 | can_mcp2515 | rp2040, stm32g474 | SPI, MCP2515 CAN |
| 25 | display_oled | rp2040, stm32g474 | I2C, SSD1306 OLED |
| 26 | rtc_clock | rp2040, stm32g474 | RTC, PCF8563 |
| 27 | rtc_ds3231 | rp2040, stm32g474 | RTC, DS3231 |
| 28 | pga2311 | rp2040, stm32g474 | SPI, PGA2311 stereo volume |
| 29 | freertos_smoke | rp2040 FreeRTOS, stm32g474 FreeRTOS | Portable app_task0/app_task1 plus native worker tasks, mutex-protected table, delay/idle smoke |
| 30 | bh1750_light | rp2040, stm32g474 | I2C, BH1750 ambient-light sensor |
| 31 | hd44780 | rp2040, stm32g474 | GPIO, HD44780 character LCD |
| 32 | tsc2007_touch | rp2040, stm32g474 | I2C, TSC2007 resistive touch controller |
| 33 | stmpe610_touch | rp2040, stm32g474 | I2C, STMPE610 resistive touch controller |
| 34 | irsmall_decoder | rp2040, stm32g474 | GPIO interrupts, IRsmallDecoder receiver |
| 35 | cJSON | rp2040, stm32g474 | Bundled cJSON parser/generator |
| 36 | lodePNG | rp2040, stm32g474 | Bundled LodePNG memory PNG encode/decode, Base64 helpers, RGB565 conversion |
| 37 | lodePNG_ili9341_base64 | rp2040, stm32g474 | Base64 PNG asset, dimension validation, RGB565 draw on ILI9341 |
| 38 | stm32g474_fdcan_native | stm32g474 | Native FDCAN1 CAN FD TX/RX |
| 39 | sdlogger | rp2040, stm32g474 | SPI SD card, shared FatFs, EEPROM-backed log/crash counters |
| 40 | jpeg | rp2040, stm32g474 | Bundled JPEGDecoder/picojpeg baseline JPEG decode, Base64 helpers, RGB565 output |
| 41 | jpeg_ili931_base64 | rp2040, stm32g474 | Base64 JPEG asset, RGB565 draw on ILI9341 |
| 42 | bsd_sockets_tcp_udp | rp2040 (WiFi) | BSD/POSIX socket compatibility examples: TCP server/client and UDP server/client, including `getaddrinfo()` hostname resolution |
| 43 | dht_temperature_humidity | rp2040, stm32g474 | GPIO, DHT11/DHT22 temperature and humidity |
| 44 | dacless_audio | rp2040, stm32g474 | DACless PWM audio DMA path, block callback, ADC-controlled phase increment |
| 44 | dacless_audio_polling | rp2040, stm32g474 | Same DACless example with `cfg.useDma=false` polling path |
| 45 | swserial_loopback | rp2040, stm32g474 | Software UART loopback/echo |
| 46 | mfrc522_rfid | rp2040, stm32g474 | SPI/I2C, MFRC522 RFID reader |
| 47 | pn532_nfc | rp2040, stm32g474 | SPI/I2C/UART, PN532 NFC/RFID reader |
| 48 | http_server | rp2040 (WiFi) | Small HTTP/1.1 server over HAL TCP with HTML and JSON routes |
| 49 | websocket | rp2040 (WiFi) | HTTP page plus WebSocket telemetry/echo server over HAL TCP |
| 50 | net_console | rp2040 (WiFi) | Password-protected TCP mirror for serial/debug logs plus command input |
| 51 | net_commands | rp2040 (WiFi) | Shared JSON/text command callbacks exposed through HTTP POST and WebSocket messages |
| 52 | http_files | rp2040 (WiFi) | Callback-backed HTTP file serving, ETag and multipart upload demo |
| 53 | simple_io_chips | rp2040, stm32g474 | I2C/SPI, MCP23017/PCA9654E/PCF8574/74HC595/MCP3221/MCP4725 simple external I/O chips |
