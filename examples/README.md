# JaszczurHAL examples

The `examples/` directory contains 30 projects demonstrating JaszczurHAL,
from sensors and displays to networking, Bluetooth, and firmware updates.
Each project has an English `README.md` and a Polish `README.pl.md`.

Open a project directory in VS Code to use its `Build`, `Upload`,
`Serial Monitor`, `Clean`, `Config Dump`, `OTA`, and board-selection tasks.
The generated `.vscode/jaszczurhal.project.json` describes the project
in the same format as a standalone firmware application.

The registry in `config/tooling/examples.json` defines sources, features,
targets, board profiles, and variants. `scripts/examples_dispatcher.py`
reads these settings and generates manifests for `vscode/entry/jh-vscode`
and `cmake/jh_firmware_project`.

## Project catalog

The table uses `R0` for `rp2040`, `RA` for `rp2350-arm`, `RV` for
`rp2350-riscv`, `S` for `stm32g474`, and `E` for `esp32s3`. The `gateTargets` column identifies
the subset included in default build checks. **An available build
configuration is not proof that the example works on every board.**
See each project's README for wiring and hardware test coverage.

| Project | Purpose | Supported targets | `gateTargets` | Variants |
|---|---|---|---|---|
| `01_core_runtime` | Blink an LED, inspect platform diagnostics, run software timers, and calculate PID output. | R0, RA, RV, S, E | R0, S, E | `capture` |
| `02_crypto` | Calculate an MD5 digest and encrypt/decrypt with ChaCha20-Poly1305. | R0, RA, RV, S, E | R0, S, E | - |
| `03_modem_A7670E` | Start an A7670/A7672 modem and publish MQTT messages over a cellular network. | R0, RA, RV, E | R0, E | - |
| `04_sensor_hub` | Read DS18B20 temperature, DHT temperature/humidity, and BH1750 illuminance. | R0, RA, RV, S, E | R0, S, E | - |
| `05_serial_gps` | Read GPS data over UART; test software-serial loopback in a separate variant. | R0, RA, RV, S, E | R0, S, E | `swserial` on R0, RA, RV; default check on R0 |
| `06_thermocouple` | Read thermocouple temperatures through MCP9600 and MAX6675. | R0, RA, RV, S, E | R0, S, E | - |
| `07_display_media` | Display graphics on ILI9341, decode PNG/JPEG, and convert Base64/RGB565 data. | R0, RA, RV, S, E | R0, S, E | - |
| `08_mqtt` | Publish and receive MQTT messages over a CYW43 network connection. | R0, RA, S, E | R0, S, E | - |
| `09_wireguard` | Prepare WireGuard configuration; starting the example alone does not confirm a working tunnel. | R0, RA, S, E | R0, S, E | - |
| `10_storage` | Store settings and a boot counter in KV, write LittleFS files, and log to SD/FatFs. | R0, RA, RV, S | R0, S | - |
| `11_i2c_slave` | Expose status, counter, and time values through I2C slave registers. | R0, RA, RV, S, E | R0, S, E | - |
| `12_i2c_scan` | Discover I2C addresses with time limits; the wiring in the source is configured for STM32G474. | R0, RA, RV, S, E | R0, S, E | - |
| `13_adc` | Read voltage through the internal ADC and an ADS1115 converter; sample internal inputs continuously with a hardware-paced DMA scan in a separate variant. | R0, RA, RV, S, E | R0, S, E | `scan` on R0, RA, RV, S, E; default check on R0, S, E |
| `14_can_mcp2515` | Send and receive classic CAN frames through MCP2515. | R0, RA, RV, S, E | R0, S, E | - |
| `15_display_oled_lcd` | Display text on an SSD1306 OLED and an HD44780 character LCD. | R0, RA, RV, S, E | R0, S, E | - |
| `16_rtc_backends` | Read RTCs, schedule wake-up, and enter low-power modes; display a DS3231 clock on ILI9341 in a separate variant. | R0, RA, RV, S | R0, S | separately selected `display-clock` on S |
| `17_audio_output` | Adjust PGA2311 gain and generate PWM audio with DMA. | R0, RA, RV, S, E | R0, S, E | - |
| `18_freertos_suite` | Run FreeRTOS tasks; add WiFi, cJSON, BSD sockets, an HTTP server, an HTTP/HTTPS client, files, WebSocket, and a console. Telegram support is compiled in but sends no notifications in this example. | R0, RA, RV, S, E | R0, S, E | `network` on R0, RA, S, E; default check on R0, S, E |
| `19_touch` | Read touch input from TSC2007 and STMPE610. | R0, RA, RV, S, E | R0, S, E | - |
| `20_irsmall_decoder` | Receive and decode infrared signals with IRsmall. | R0, RA, RV, S, E | R0, S, E | - |
| `21_stm32g474_fdcan_native` | Send and receive frames through the STM32G474 built-in FDCAN controller. | S | S | - |
| `22_rfid_nfc` | Read card identifiers through MFRC522 and PN532. | R0, RA, RV, S, E | R0, S, E | - |
| `23_io_pmic` | Control an RGB LED, I/O expander, and DAC, and read ADP5360 power status. | R0, RA, RV, S, E | R0, S, E | - |
| `24_epd_display` | Draw a test pattern and refresh a 200 × 200 e-paper display. | R0, RA, RV, S, E | R0, S, E | - |
| `25_ota` | Update firmware over OTA: discover devices, authenticate staging, confirm a trial image, roll back, and recover through BOOTSEL. | R0, RA, E | R0, E | - |
| `26_ble_stream` | Exchange data and commands through JH BLE Stream v1 after mutual authentication. | R0, RA, S | R0, RA, S | `commands` and `commands-freertos` on R0, RA, S; default check on R0 |
| `27_lora_point_to_point` | Exchange SX1262 ping/pong packets and fragmented 500-byte commands/responses over `hal_lora_link`. | R0, S | R0, S | `probe`, `responder`, `link` and `link-responder` on R0, S; separately selected hardware variants `sf7` and `responder-sf7` |
| `28_serial_commands` | Receive framed Serial Session commands and dispatch them to shared handlers. | R0, RA, RV, S, E | R0, S, E | - |
| `29_bluetooth_gamepad` | Read gamepad input, discover Classic devices, and receive raw HID reports. | R0, RA, S | R0 | `classic-scan`, `hid-host`, and `ble` on R0, RA, S; default check on R0 |
| `30_bluetooth_speaker` | Receive A2DP audio and play it over PWM, with optional AVRCP volume control and a build that includes BLE. | R0, RA | R0, RA | `avrcp` and `ble-a2dp` on R0, RA; both in default checks |

RP network examples default to `picow` for RP2040 and `pico2w` for
RP2350 ARM. RP2350 RISC-V configurations that require CYW43 are not
supported. STM32G474 network and Bluetooth projects use the NUCLEO-G474RE
profile with an external PIM730/RM2 module.

The LoRa project defaults to `pico-core1262-hf` and
`nucleo-g474re-core1262-hf`. Explicitly select `rp2040-lora-lf` for the
integrated Waveshare LF board. LF and HF devices use different bands;
do not combine them as one radio pair. The `probe` variant checks chip
capabilities, calibration, current RSSI, and CAD without transmitting.
The base and `responder` variants use SF9/10 dBm; `sf7` and
`responder-sf7` form a test pair at SF7/6 dBm.

The `link` and `link-responder` variants exchange a 500-byte binary `echo`
command and response, using three fragments in each direction. They test
addressing, request identifiers, message reassembly, duplicate suppression,
and retransmission. The route also accepts `BLE_STREAM` as a source,
but this example does not start BLE transport. SX1261, SX1276, and SX1278
remain experimental software integrations, without board profiles or
claims of verified hardware operation for this example.

## Supported build targets

| Target | Default board | Toolchain | Firmware artifacts |
|---|---|---|---|
| `rp2040` | `pico` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | official Pico SDK + pinned Hazard3 toolchain | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |
| `esp32s3` | `waveshare-esp32-s3-zero` | pinned ESP-IDF | ELF, BIN, MAP, bootloader, partition table, `jh_esp_idf_artifacts.json` |

For `esp32s3`, `jh-vscode` runs `scripts/build_esp_idf.py` with the
manifest's sources and definitions instead of the CMake dispatcher. An
example declares `esp32s3` only when every feature it requests is on the
target's `supportedFeatures` list; that list holds the ESP-IDF backends and
the portable bus, sensor, display, codec and command drivers, while flash
storage, audio, power, internal RTC, LoRa, BLE Stream and Bluetooth Classic
stay outside it. `tests/fixtures/esp32s3_phase3`
still checks the complete Phase 2/3 feature closure, and hardware reports are
in `tests/hardware/esp32s3_phase1` and `tests/hardware/esp32s3_phase2`.
Wiring in the example sources targets RP and STM32 boards; check pin numbers
against the ESP32-S3 board profile before running them.

## Requirements

Install CMake 3.20 or newer, Python 3, and the appropriate compiler.
RP2040, RP2350 ARM, and STM32G474 use `arm-none-eabi-gcc`.
Prepare the managed Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, LittleFS,
and RISC-V components with `./runmefirst.sh` or
`./third_party/update_components.sh`.

## Adding an example

First check whether an existing project can demonstrate the new feature.
Keeping related functionality together reduces repeated configuration
and avoids rebuilding the entire HAL in many small projects on every
target.

Create a separate project when the target, build tools, execution model,
board profile, resource conflicts, or hardware requirements make a
combined application impractical. Explain the constraint in this catalog
and declare exact `targets` and `gateTargets` in
`config/tooling/examples.json`. Add a variant only when the behavior
cannot be selected while the program runs. After a change, check the
configuration counts for both the full build matrix and default checks.

## Build commands

Build all supported configurations for a target:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target esp32s3 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Build one project with the same tool used by VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/01_core_runtime --target rp2040 --board pico
```

Select projects from the registry:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 \
  --example 01_core_runtime --example 10_storage
```

List configurations and refresh generated files after a registry change:

```bash
scripts/examples_dispatcher.py list
python3 scripts/sync_generated.py --write
```

Final build outputs are stored in `.build/examples/<example>/`. CMake
working directories use `.build/examples/<example>/cmake/<target>/<board>/`.
Use `examples/CMakeLists.txt` to call the same script through CMake:

```bash
cmake -S examples -B .build/examples-cmake/rp2040 \
  -DJH_EXAMPLE_TARGET=rp2040
cmake --build .build/examples-cmake/rp2040
```

## Application structure

```text
NN_example_name/
  app.c
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    tasks.json
    settings.json
```

Applications expose these functions:

```c
void app_start(void);
void app_task0(void);
void app_task1(void); /* optional with HAL_ENABLE_APP_TASK1 */
```

The selected application runtime provides `main()`. Bare-metal RP runs
`app_task0()` on core 0 and starts core 1 for `app_task1()` only when
requested. With FreeRTOS, application functions run as tasks assigned to
the corresponding cores. STM32G474 runs both functions cooperatively
without an operating system or as separate FreeRTOS tasks. Host examples
use a cooperative loop.

A minimal LED-blink application is:

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

Enable library modules in `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_I2C
#define HAL_ENABLE_BH1750
```

Define `HAL_ENABLE_*` macros without a value or with an explicit `1`.
The project tools reject `0`; omit a macro to disable a module. Keep this
header macro-only because it is read before target and board settings
are finalized. Definitions must be unconditional, except for a
same-symbol `#ifndef` guard. The source-selection tool reads these
declarations as text rather than as fully preprocessed output.

The build sets `HAL_PROVIDE_APP_ENTRY`. Pin assignments come from the
selected generated board profile. When no predefined composite profile
matches the application wiring, describe it with an explicit hardware
descriptor.

## VS Code

Task names and shortcuts are documented in
[JaszczurHAL in VS Code](../vscode/README.md). For project configuration,
target and board selection, source discovery, and output directories,
see [Firmware Project Workflow](../doc/en/FwProjectWorkflow.md).

Project and task files are generated from the registry and repository
tools. To retain wording changes after regeneration, also update the
corresponding generator source; manually edited generated files may be
overwritten on the next refresh.

The `01_core_runtime` variant `capture` has a separate entry point because it reserves a capture input and timer/DMA resources; the base diagnostic application needs none of them.
