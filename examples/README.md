# JaszczurHAL Examples

## Build System Overview

The examples use a unified CMake build system (`examples/CMakeLists.txt`) that
compiles all examples for a selected backend. Two backends are supported:

| Backend | Toolchain | Method |
|---------|-----------|--------|
| **RP2040** | `arduino-cli` + `rp2040:rp2040` core | Auto-generates a single-core `.ino` wrapper, symlinks sources, invokes `arduino-cli compile` |
| **STM32G474** | `arm-none-eabi-gcc` | Builds a bare-metal ELF with linker script, startup files, and all HAL sources |

### Requirements

**RP2040:**
- `arduino-cli` in `$PATH`
- Arduino RP2040 core installed: `arduino-cli core install rp2040:rp2040`
- JaszczurHAL repo cloned (examples reference `../src` via symlinks)

**STM32G474:**
- `arm-none-eabi-gcc` toolchain (13.x+ recommended)
- CMake 3.16+

## How to Build

### Configure + build all examples for RP2040

```bash
cmake -S examples -B build_examples_rp2040 -DJH_EXAMPLE_TARGET=rp2040
cmake --build build_examples_rp2040
```

### Configure + build all examples for STM32G474

```bash
cmake -S examples -B build_examples_stm32 \
      -DJH_EXAMPLE_TARGET=stm32g474 \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"
cmake --build build_examples_stm32
```

### Build a single example

```bash
# RP2040
cmake --build build_examples_rp2040 --target 01_blink_rp2040

# STM32G474
cmake --build build_examples_stm32 --target 01_blink_stm32g474
```

Target names follow the pattern: `<folder_name>_<backend>`.

### Using CMake presets (alternative)

```bash
cmake --preset rp2040 -S examples
cmake --build --preset rp2040

cmake --preset stm32g474 -S examples
cmake --build --preset stm32g474
```

## Application Structure

Each example follows the same portable layout:

```
NN_example_name/
├── app.c (or app.cpp)          ← application logic
├── hal_project_config.h        ← feature flags + backend/module config
└── .vscode/                    ← (optional) VS Code direct-build tasks
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
| `app_task1()` | Secondary loop iteration (optional) | No (weak-linked) |

### Backend mapping

| Backend | Entry implementation | `app_start()` | `app_task0()` | `app_task1()` |
|---------|----------------------|---------------|---------------|---------------|
| RP2040 examples (arduino-pico) | CMake-generated `.ino` | `setup()` | `loop()` (core 0) | Not called by default |
| STM32G474 examples (bare-metal) | `HAL_PROVIDE_APP_ENTRY` from CMake | Before super-loop | Super-loop body | Cooperative, same loop (pending FreeRTOS) |
| Mock/host apps | `HAL_PROVIDE_APP_ENTRY` when requested | Before super-loop | Super-loop body | Cooperative, same loop |

### RP2040 `loop1()` caution

On arduino-pico, defining `loop1()` is not a harmless placeholder: the core
starts RP2040 core 1 whenever `setup1()` or `loop1()` is linked. The
library-provided entry shim emits `loop1()` when `HAL_PROVIDE_APP_ENTRY` is
defined, even if `app_task1()` only resolves to the weak empty default.

That can silently change an existing single-core sketch into a dual-core
program and has been seen as repeated USB disconnect/reconnect or reset-like
behavior after upload. For RP2040 examples, prefer the generated `.ino` wrapper
that calls only `app_start()` and `app_task0()`. Use `HAL_PROVIDE_APP_ENTRY` on
RP2040 only when the application intentionally wants the `app_task1()` /
`loop1()` core-1 path.

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

## WiFi-Capable Examples

Examples 10, 11, and 15 require a WiFi-capable board. The CMake system
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
| 08 | thermocouple | rp2040 | MAX6675/MCP9600 |
| 09 | display_tft | rp2040, stm32g474 | ILI9341/ST7789, draw7Segment |
| 10 | mqtt | rp2040 (WiFi) | WiFi, MQTT |
| 11 | wireguard | rp2040 (WiFi) | WiFi, WireGuard |
| 12 | kv_store | rp2040 | Key-value storage |
| 13 | i2c_slave | rp2040 | I2C slave |
| 14 | uart | rp2040, stm32g474 | UART |
| 15 | wifi | rp2040 (WiFi) | WiFi scan/connect |
| 16 | littlefs | rp2040 | LittleFS |
| 17 | pid_controller | rp2040, stm32g474 | PID controller |
| 18 | rgb_led | rp2040 | NeoPixel/WS2812 |
| 19 | timer_ext | rp2040, stm32g474 | Extended timers |
| 20 | i2c_scan | rp2040, stm32g474 | I2C bus scan |
| 21 | adc_read | rp2040, stm32g474 | ADC |
| 22 | gps_uart | rp2040, stm32g474 | GPS (UART transport) |
| 23 | external_adc_ads1115 | rp2040, stm32g474 | I2C, ADS1115 external ADC |
| 24 | can_mcp2515 | rp2040, stm32g474 | SPI, MCP2515 CAN |
| 25 | display_oled | rp2040, stm32g474 | I2C, SSD1306 OLED |
