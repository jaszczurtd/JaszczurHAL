# JaszczurHAL Examples

The `examples/` tree contains a set of simple projects that demonstrate the core capabilities of JaszczurHAL. Each project has its own
generated `.vscode/jaszczurhal.project.json`; opening that directory directly
in VS Code exposes the same Build, Upload, Serial Monitor, Clean, Config Dump,
OTA, and board-selection tasks as a standalone firmware project.

The registry in `scripts/examples_dispatcher.py` is the source of truth for
project coverage, supported targets, default-gate targets, board profiles,
variants, sources, and feature definitions. Generated manifests are consumed
by `vscode/entry/jh-vscode` and `cmake/jh_firmware_project`.

## Project catalog


Target abbreviations used below are `R0` = `rp2040`, `RA` = `rp2350-arm`,
`RV` = `rp2350-riscv`, and `S` = `stm32g474`.

| Project | Purpose | Supported targets | `gateTargets` | Variants |
|---|---|---|---|---|
| `01_core_runtime` | LED blink, debug/architecture report, soft-timer table, PID controller, managed timer | R0, RA, RV, S | R0, S | - |
| `02_crypto` | Hashing, authentication, encryption, and Base64 primitives | R0, RA, RV, S | R0, S | - |
| `03_modem_A7670E` | SIMCom A7670/A7672 modem lifecycle and AT services | R0, RA, RV | R0 | - |
| `04_sensor_hub` | DS18B20, BH1750, and DHT temperature/humidity sensors | R0, RA, RV, S | R0, S | - |
| `05_serial_gps` | UART, GPS parsing/transport, and software-serial loopback | R0, RA, RV, S | R0, S | `swserial` on R0, RA, RV; gate on R0 |
| `06_thermocouple` | Thermocouple facade and supported backends | R0, RA, RV, S | R0, S | - |
| `07_display_media` | ILI9341 graphics, PNG/JPEG codecs, Base64 conversion, and RGB565 rendering | R0, RA, RV, S | R0, S | - |
| `08_mqtt` | MQTT over the selected CYW43 network backend | R0, RA, S | R0, S | - |
| `09_wireguard` | WireGuard tunnel setup over the selected network backend | R0, RA, S | R0, S | - |
| `10_storage` | KV store, LittleFS, SD/FatFs logging, and persistent counters | R0, RA, RV, S | R0, S | - |
| `11_i2c_slave` | I2C slave register-map operation | R0, RA, RV, S | R0, S | - |
| `12_i2c_scan` | Bounded I2C bus scanning | R0, RA, RV, S | R0, S | - |
| `13_adc` | Internal ADC sampling and external ADS1115 conversion | R0, RA, RV, S | R0, S | - |
| `14_can_mcp2515` | MCP2515 classic CAN backend | R0, RA, RV, S | R0, S | - |
| `15_display_oled_lcd` | SSD1306 OLED and HD44780 character LCD | R0, RA, RV, S | R0, S | - |
| `16_rtc_backends` | RTC facade, target-native relative wake-up, portable low-power transitions, and a DS3231/ILI9341 retention clock | R0, RA, RV, S | R0, S | manual `display-clock` on S |
| `17_audio_output` | PGA2311 volume control and DMA/PWM audio output | R0, RA, RV, S | R0, S | - |
| `18_freertos_suite` | FreeRTOS tasks/affinity, WiFi, cJSON, BSD sockets, HTTP/HTTPS client/server, files, WebSocket, network console, commands, and Telegram notifications | R0, RA, RV, S | R0, S | `network` on R0, RA, S; gate on R0, S |
| `19_touch` | TSC2007 and STMPE610 touch controllers | R0, RA, RV, S | R0, S | - |
| `20_irsmall_decoder` | IRsmall protocol decoding | R0, RA, RV, S | R0, S | - |
| `21_stm32g474_fdcan_native` | Native STM32G474 FDCAN | S | S | - |
| `22_rfid_nfc` | MFRC522 RFID and PN532 NFC/RFID readers | R0, RA, RV, S | R0, S | - |
| `23_io_pmic` | RGB LED, simple I/O expanders/DAC, and ADP5360 PMIC | R0, RA, RV, S | R0, S | - |
| `24_epd_display` | E-paper display facade and refresh path | R0, RA, RV, S | R0, S | - |
| `25_ota` | Discovery, authenticated OTA staging, trial confirmation, rollback, and BOOTSEL recovery | R0, RA | R0 | - |
| `26_ble_stream` | BLE Peripheral lifecycle, authenticated JH BLE Stream v1, and command-router adapter | R0, RA, S | R0, RA, S | `commands` and `commands-freertos` on R0, RA, S; gate on R0 |
| `27_lora_point_to_point` | Raw SX1262 ping/pong plus fragmented command-router request/response over `hal_lora_link` | R0, S | R0, S | `probe`, `responder`, `link` and `link-responder` on R0, S; manual hardware variants `sf7` and `responder-sf7` |
| `28_serial_commands` | Framed Serial Session dispatch through an independent command router | R0, RA, RV, S | R0, S | - |
| `29_bluetooth_gamepad` | Classic discovery, raw HID Host, and normalized gamepad adapter | R0, RA, S | R0 | `classic-scan`, `hid-host`, and `ble` on R0, RA, S; gate on R0 |

RP-family network builds use `picow` for RP2040 and `pico2w` for RP2350 ARM.
RP2350 RISC-V configurations that require CYW43 are unsupported. STM32G474
network and Bluetooth projects select the NUCLEO-G474RE plus the external
PIM730/RM2 profile.

The LoRa project defaults to the fixed `pico-core1262-hf` and
`nucleo-g474re-core1262-hf` fixtures. Use `rp2040-lora-lf` explicitly for the
integrated Waveshare LF board; LF and HF devices use different frequency bands
and belong to separate physical radio pairs. The `probe` variant validates
capabilities, calibration, current RSSI and CAD without transmitting. The base
and `responder` variants use SF9/10 dBm, while `sf7` and `responder-sf7` provide
the deterministic SF7/6 dBm hardware-test pair.

The `link` and `link-responder` variants exchange a correlated binary 500-byte
`echo` command and response through the shared command router. Both directions
exercise addressing, request identifiers, three-fragment reassembly, duplicate
suppression and retransmission. The handler route also allows the implemented
`BLE_STREAM` source without adding BLE transport code to this example.

SX1261, SX1276 and SX1278 remain experimental software-only integrations and
do not add example board profiles or claim physical support for this fixture.

## Supported build targets

| Target | Default board | Toolchain | Firmware artifacts |
|---|---|---|---|
| `rp2040` | `pico` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | official Pico SDK + pinned Hazard3 toolchain | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |

ESP32-S3 currently uses dedicated ESP-IDF projects instead of this
CMake-native example dispatcher. `tests/fixtures/esp32s3_phase3` compiles and
links the complete Phase 2/3 backend graph, while
`tests/hardware/esp32s3_phase1` and `tests/hardware/esp32s3_phase2` retain the
available hardware reports. Adding dispatcher-backed ESP32-S3 examples also
requires an ESP-IDF build mode and per-example board/resource validation.

## Requirements

- CMake 3.20 or newer for dispatcher-backed firmware builds;
- Python 3;
- `arm-none-eabi-gcc` for RP2040, RP2350 ARM, and STM32G474;
- managed Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, LittleFS, and RISC-V
  components prepared by `./runmefirst.sh` or
  `./third_party/update_components.sh`.

## Adding an example

Before creating another directory, check whether the new behavior can extend
an existing project or one of its variants. Consolidation is the default: it
keeps related runtime paths together and avoids rebuilding the complete HAL in
many small firmware projects on every target.

A separate project is appropriate when target, toolchain, runtime, board
profile, mutually exclusive resources, or hardware requirements prevent a useful
combined image. Document that constraint here and declare the exact `targets`
and `gateTargets` in `config/tooling/examples.json`. Use a variant only when the
behavior cannot be selected at runtime. Every change must review the
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

Inspect the generated matrix or refresh all tracked artifacts after changing
the registry:

```bash
scripts/examples_dispatcher.py list
python3 scripts/sync_generated.py --write
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
from the selected generated profile. Application-owned wiring remains possible
through an explicit hardware descriptor when no fixed composite profile fits.

## VS Code

Generated task labels and keyboard shortcuts are documented in
[JaszczurHAL VS Code Entry](../vscode/README.md). Project configuration,
target/board resolution, source discovery, and artifact paths are documented in
[Firmware Project Workflow](../doc/en/FwProjectWorkflow.md).
