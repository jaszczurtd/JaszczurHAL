# JaszczurHAL Examples

The `examples/` tree contains 27 dispatcher-backed firmware projects that
preserve the coverage of the former 60 examples. Each project has its own
generated `.vscode/jaszczurhal.project.json`; opening that directory directly
in VS Code exposes the same Build, Upload, Serial Monitor, Clean, Config Dump,
OTA, and board-selection tasks as a standalone firmware project.

The registry in `scripts/examples_dispatcher.py` is the source of truth for
project coverage, supported targets, default-gate targets, board profiles,
variants, sources, and feature definitions. Generated manifests are consumed
by `vscode/entry/jh-vscode` and `cmake/jh_firmware_project`.

## Matrix and gate policy

A configuration is one base project or project variant built for one target.
The complete supported matrix contains 104 configurations:

| Matrix | `rp2040` | `rp2350-arm` | `rp2350-riscv` | `stm32g474` | Total |
|---|---:|---:|---:|---:|---:|
| Full supported matrix | 29 | 26 | 22 | 27 | **104** |
| Default examples gate | 29 | 0 | 0 | 27 | **56** |
| Representative Gate 6 builds | 2 | 2 | 2 | 0 | **6** |
| Example-related default HAL gate builds | 31 | 2 | 2 | 27 | **62** |

The six Gate 6 invocations build the core-runtime and FreeRTOS representative
firmware once with each RP toolchain/architecture. They deliberately exercise
the direct native build path in addition to the dispatcher-backed examples
gate, so they are counted as build invocations rather than additional example
configurations.

Generated manifests distinguish two target lists:

- `example.targets` contains every target on which a base project is supported;
- `example.gateTargets` is a validated subset selected by the default examples
  gate. Unless a registry entry overrides it, generation selects supported
  `rp2040` and `stm32g474` targets.

Variants have their own `targets` and `gateTargets`. A target absent from
`targets` is unsupported; a target present in `targets` but absent from
`gateTargets` remains available for the full matrix without extending the
default gate.

`scripts/examples_dispatcher.py build` without `--gate` builds every supported
base/variant configuration for the requested target. Adding `--gate` restricts
the run to configurations whose `gateTargets` contain that target:

```bash
# Complete matrix for one target.
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"

# Default examples gate: 29 RP2040 plus 27 STM32G474 configurations.
scripts/examples_dispatcher.py build \
  --target rp2040 --gate --jobs "$(nproc)"
scripts/examples_dispatcher.py build \
  --target stm32g474 --gate --jobs "$(nproc)"
```

## Project catalog and coverage

The `covers` registry field records the legacy coverage IDs owned by each
project. These values are audit identifiers, not current directories or link
targets. Consolidated projects keep those behaviors in one firmware image or
in a small set of explicit variants instead of recompiling the complete HAL for
every individual demonstration.

Target abbreviations used below are `R0` = `rp2040`, `RA` = `rp2350-arm`,
`RV` = `rp2350-riscv`, and `S` = `stm32g474`.

| Project | Purpose | Registry `covers` IDs | Supported targets | `gateTargets` | Variants |
|---|---|---|---|---|---|
| `01_core_runtime` | LED blink, debug/architecture report, soft-timer table, PID controller, managed timer | `01_blink`, `02_debug_helper`, `03_soft_timer_table`, `17_pid_controller`, `19_timer_ext` | R0, RA, RV, S | R0, S | - |
| `02_crypto` | Hashing, authentication, encryption, and Base64 primitives | `04_crypto` | R0, RA, RV, S | R0, S | - |
| `03_modem_A7670E` | SIMCom A7670/A7672 modem lifecycle and AT services | `05_modem_A7670E` | R0, RA, RV | R0 | - |
| `04_sensor_hub` | DS18B20, BH1750, and DHT temperature/humidity sensors | `06_ds18b20`, `30_bh1750_light`, `43_dht_temperature_humidity` | R0, RA, RV, S | R0, S | - |
| `05_serial_gps` | UART, GPS parsing/transport, and software-serial loopback | `07_gps`, `14_uart`, `22_gps_uart`, `45_swserial_loopback` | R0, RA, RV, S | R0, S | `swserial` on R0, RA, RV; gate on R0 |
| `06_thermocouple` | Thermocouple facade and supported backends | `08_thermocouple` | R0, RA, RV, S | R0, S | - |
| `07_display_media` | ILI9341 graphics, PNG/JPEG codecs, Base64 conversion, and RGB565 rendering | `09_display_tft`, `36_lodePNG`, `37_lodePNG_ili9341_base64`, `40_jpeg`, `41_jpeg_ili931_base64` | R0, RA, RV, S | R0, S | - |
| `08_mqtt` | MQTT over the selected CYW43 network backend | `10_mqtt` | R0, RA, S | R0, S | - |
| `09_wireguard` | WireGuard tunnel setup over the selected network backend | `11_wireguard` | R0, RA, S | R0, S | - |
| `10_storage` | KV store, LittleFS, SD/FatFs logging, and persistent counters | `12_kv_store`, `16_littlefs`, `39_sdlogger` | R0, RA, RV, S | R0, S | - |
| `11_i2c_slave` | I2C slave register-map operation | `13_i2c_slave` | R0, RA, RV, S | R0, S | - |
| `12_i2c_scan` | Bounded I2C bus scanning | `20_i2c_scan` | R0, RA, RV, S | R0, S | - |
| `13_adc` | Internal ADC sampling and external ADS1115 conversion | `21_adc_read`, `23_external_adc_ads1115` | R0, RA, RV, S | R0, S | - |
| `14_can_mcp2515` | MCP2515 classic CAN backend | `24_can_mcp2515` | R0, RA, RV, S | R0, S | - |
| `15_display_oled_lcd` | SSD1306 OLED and HD44780 character LCD | `25_display_oled`, `31_hd44780` | R0, RA, RV, S | R0, S | - |
| `16_rtc_backends` | RTC facade with PCF8563 and DS3231 behavior | `26_rtc_clock`, `27_rtc_ds3231` | R0, RA, RV, S | R0, S | - |
| `17_audio_output` | PGA2311 volume control and DMA/PWM audio output | `28_pga2311`, `44_dacless_audio` | R0, RA, RV, S | R0, S | - |
| `18_freertos_suite` | FreeRTOS tasks/affinity, WiFi, cJSON, BSD sockets, HTTP/HTTPS client/server, files, WebSocket, network console, and commands | `15_wifi`, `29_freertos_smoke`, `35_cJSON`, `42_bsd_sockets_tcp_udp`, `48_http_server`, `49_websocket`, `50_net_console`, `51_net_commands`, `52_http_files`, `56_http_https_client` | R0, RA, RV, S | R0, S | `network` on R0, RA, S; gate on R0, S |
| `19_touch` | TSC2007 and STMPE610 touch controllers | `32_tsc2007_touch`, `33_stmpe610_touch` | R0, RA, RV, S | R0, S | - |
| `20_irsmall_decoder` | IRsmall protocol decoding | `34_irsmall_decoder` | R0, RA, RV, S | R0, S | - |
| `21_stm32g474_fdcan_native` | Native STM32G474 FDCAN | `38_stm32g474_fdcan_native` | S | S | - |
| `22_rfid_nfc` | MFRC522 RFID and PN532 NFC/RFID readers | `46_mfrc522_rfid`, `47_pn532_nfc` | R0, RA, RV, S | R0, S | - |
| `23_io_pmic` | RGB LED, simple I/O expanders/DAC, and ADP5360 PMIC | `18_rgb_led`, `53_simple_io_chips`, `54_adp5360_pmic` | R0, RA, RV, S | R0, S | - |
| `24_epd_display` | E-paper display facade and refresh path | `55_epd_display` | R0, RA, RV, S | R0, S | - |
| `25_ota` | Discovery, authenticated OTA staging, trial confirmation, rollback, and BOOTSEL recovery | `57_ota` | R0, RA | R0 | - |
| `26_ble_stream` | Experimental BLE Peripheral lifecycle and authenticated JH BLE Stream v1 | `58_ble_peripheral`, `59_ble_stream` | R0, S | R0, S | - |
| `27_lora_point_to_point` | Raw SX1262 ping/pong with DIO1-driven asynchronous TX/RX, callbacks, diagnostics and radio power/lifecycle recovery | `60_lora_point_to_point` | R0, S | R0, S | `responder` on R0, S; gate on R0, S |

RP-family network builds use `picow` for RP2040 and `pico2w` for RP2350 ARM.
RP2350 RISC-V configurations that require CYW43 are unsupported. STM32G474
network and Bluetooth projects select the NUCLEO-G474RE plus the external
PIM730/RM2 profile.

The LoRa project selects the integrated Waveshare RP2040-LoRa-LF profile for
RP2040 and an explicitly wired Core1262-HF on NUCLEO-G474RE. These target
configurations use different frequency bands and belong to separate physical
radio pairs.

## Supported build targets

| Target | Default board | Toolchain | Firmware artifacts |
|---|---|---|---|
| `rp2040` | `pico` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | official Pico SDK + pinned Hazard3 toolchain | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |

## Requirements

- CMake 3.20 or newer for dispatcher-backed firmware builds;
- Python 3;
- `arm-none-eabi-gcc` for RP2040, RP2350 ARM, and STM32G474;
- managed Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, LittleFS, and RISC-V
  components prepared by `./runmefirst.sh` or
  `./third_party/update_components.sh`.

## Adding example coverage

Before creating another directory, check whether the new behavior can extend
an existing project or one of its variants. Consolidation is the default: it
keeps related runtime paths together and avoids rebuilding the complete HAL in
many small firmware projects on every target.

A separate project is appropriate when target, toolchain, runtime, board
profile, mutually exclusive resources, or hardware contracts prevent a useful
combined image. Document that constraint here and declare the exact `targets`,
`gateTargets`, and legacy `covers` ownership in
`scripts/examples_dispatcher.py`. Use a variant only when the behavior cannot
be selected at runtime. Every change must retain unique coverage and review the
full-matrix and default-gate configuration counts.

## Build commands

Build the full supported matrix for a selected target:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Build one project through the same CLI used by VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/01_core_runtime --target rp2040 --board pico
```

Limit the registry runner to one or more projects:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 \
  --example 01_core_runtime --example 10_storage
```

Inspect the generated matrix or regenerate manifests after changing the
registry:

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py generate
```

Repository-owned final artifacts stay below `.build/examples/<example>/`.
Target-specific CMake trees use
`.build/examples/<example>/cmake/<target>/<board>/`.

`examples/CMakeLists.txt` is a thin entry to the same dispatcher:

```bash
cmake -S examples -B .build/examples-cmake/rp2040 \
  -DJH_EXAMPLE_TARGET=rp2040
cmake --build .build/examples-cmake/rp2040
```

## Application structure

```text
NN_example_name/
  app.c or app.cpp
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    tasks.json
    settings.json
```

Applications expose:

```c
void app_start(void);
void app_task0(void);
void app_task1(void); /* optional with HAL_ENABLE_APP_TASK1 */
```

The selected runtime provides `main()`. Bare-metal RP runs `app_task0()` on
core 0 and starts core 1 for `app_task1()` only when requested. RP FreeRTOS
creates application tasks with matching core affinity. STM32G474 runs both
functions cooperatively in bare-metal mode or as separate tasks in FreeRTOS
mode. Host demo applications use a cooperative loop.

Minimal application:

```c
#include <hal/core/hal_app.h>
#include <hal/system/hal_board.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_system.h>

void app_start(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT);
}

void app_task0(void) {
  hal_gpio_write(HAL_LED_BUILTIN, true);
  hal_delay_ms(500u);
  hal_gpio_write(HAL_LED_BUILTIN, false);
  hal_delay_ms(500u);
}
```

Feature selection belongs in `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_I2C
#define HAL_ENABLE_BH1750
```

Use bare feature names or an explicit value of `1`. Supported tooling rejects
`HAL_ENABLE_*=0`; omit the macro to disable a feature. Keep the project header
macro-only because it is loaded before target and board normalization. Feature
definitions must be unconditional; only a same-symbol `#ifndef` guard is
supported because the early source-selection collector reads the file
textually.

The build flow supplies `HAL_PROVIDE_APP_ENTRY`. Board-specific pin facts come
from the selected generated profile; project wiring remains in the project
configuration.

## VS Code

Generated task labels and keyboard shortcuts are documented in
[JaszczurHAL VS Code Entry](../vscode/README.md). Project configuration,
target/board resolution, source discovery, and artifact paths are documented in
[Firmware Project Workflow](../doc/FwProjectWorkflow.md).
