# JaszczurHAL examples

The `examples/` directory contains 30 projects demonstrating JaszczurHAL,
from sensors and displays to networking, Bluetooth, and firmware updates.
Each project has an English `README.md` and a Polish `README.pl.md`.

Open a project directory in VS Code to use its `Build`, `Upload`,
`Serial Monitor`, `Clean`, `Config Dump`, `OTA`, and board-selection tasks.

Every example is an ordinary firmware project. Its `hal_project_config.h`
declares the HAL features, the targets (`JH_PROJECT_TARGETS`), and the
variants (`JH_PROJECT_VARIANTS`). Its hand-written
`.vscode/jaszczurhal.project.json` holds the tooling metadata, such as the
default board for each target. `scripts/examples_dispatcher.py` generates the
other VS Code files from these two and builds every configuration through
`vscode/entry/jh-vscode`. The rules are described in
[Targets and variants](../doc/en/FwProjectWorkflow.md#targets-and-variants).

## Project catalog

The table uses `R0` for `rp2040`, `RA` for `rp2350-arm`, `RV` for
`rp2350-riscv`, `S` for `stm32g474`, and `E` for `esp32s3`. A variant builds
on every target of its project unless the table names the targets. Gate 9 of
`runalltests.sh` builds every configuration for `rp2040`, `stm32g474`, and
`esp32s3`. **An available build configuration is not proof that the example
works on every board.** See each project's README for wiring and hardware test
coverage.

| Project | Purpose | Supported targets | Variants |
|---|---|---|---|
| `01_core_runtime` | Blink an LED, inspect platform diagnostics, run software timers, and calculate PID output. | R0, RA, RV, S, E | `CAPTURE` |
| `02_crypto` | Calculate an MD5 digest and encrypt/decrypt with ChaCha20-Poly1305. | R0, RA, RV, S, E | - |
| `03_modem_A7670E` | Start an A7670/A7672 modem and publish MQTT messages over a cellular network. | R0, RA, RV, E | - |
| `04_sensor_hub` | Read DS18B20 temperature, DHT temperature/humidity, and BH1750 illuminance. | R0, RA, RV, S, E | - |
| `05_serial_gps` | Read GPS data over UART; test software-serial loopback in a separate variant. | R0, RA, RV, S, E | `SWSERIAL` on R0, RA, RV |
| `06_thermocouple` | Read thermocouple temperatures through MCP9600 and MAX6675. | R0, RA, RV, S, E | - |
| `07_display_media` | Display graphics on ILI9341, decode PNG/JPEG, and convert Base64/RGB565 data. | R0, RA, RV, S, E | - |
| `08_mqtt` | Publish and receive MQTT messages over a CYW43 network connection. | R0, RA, S, E | - |
| `09_wireguard` | Prepare WireGuard configuration; starting the example alone does not confirm a working tunnel. | R0, RA, S, E | - |
| `10_storage` | Store settings and a boot counter in KV, write LittleFS files, and log to SD/FatFs. | R0, RA, RV, S | - |
| `11_i2c_slave` | Expose status, counter, and time values through I2C slave registers. | R0, RA, RV, S, E | - |
| `12_i2c_scan` | Discover I2C addresses with time limits; the wiring in the source is configured for STM32G474. | R0, RA, RV, S, E | - |
| `13_adc` | Read voltage through the internal ADC and an ADS1115 converter; sample internal inputs continuously with a hardware-paced DMA scan in a separate variant. | R0, RA, RV, S, E | `SCAN` |
| `14_can_mcp2515` | Send and receive classic CAN frames through MCP2515. | R0, RA, RV, S, E | - |
| `15_display_oled_lcd` | Display text on an SSD1306 OLED and an HD44780 character LCD. | R0, RA, RV, S, E | - |
| `16_rtc_backends` | Read RTCs, schedule wake-up, and enter low-power modes; display a DS3231 clock on ILI9341 in a separate variant. | R0, RA, RV, S | `DISPLAY_CLOCK` on S |
| `17_audio_output` | Adjust PGA2311 gain and generate PWM audio with DMA. | R0, RA, RV, S, E | - |
| `18_freertos_suite` | Run FreeRTOS tasks; add WiFi, cJSON, BSD sockets, an HTTP server, an HTTP/HTTPS client, files, WebSocket, and a console. Telegram support is compiled in but sends no notifications in this example. | R0, RA, RV, S, E | `NETWORK` on R0, RA, S, E |
| `19_touch` | Read touch input from TSC2007 and STMPE610. | R0, RA, RV, S, E | - |
| `20_irsmall_decoder` | Receive and decode infrared signals with IRsmall. | R0, RA, RV, S, E | - |
| `21_stm32g474_fdcan_native` | Send and receive CAN FD frames on every board channel through the STM32G474 built-in FDCAN controllers; loopback without wiring by default. | S | - |
| `22_rfid_nfc` | Read card identifiers through MFRC522 and PN532. | R0, RA, RV, S, E | - |
| `23_io_pmic` | Control an RGB LED, I/O expander, and DAC, and read ADP5360 power status. | R0, RA, RV, S, E | - |
| `24_epd_display` | Draw a test pattern and refresh a 200 × 200 e-paper display. | R0, RA, RV, S, E | - |
| `25_ota` | Update firmware over OTA: discover devices, authenticate staging, confirm a trial image, roll back, and recover through BOOTSEL. | R0, RA, E | - |
| `26_ble_stream` | Exchange data and commands through JH BLE Stream v1 after mutual authentication. | R0, RA, S | `COMMANDS`, `COMMANDS_FREERTOS` |
| `27_lora_point_to_point` | Exchange SX1262 ping/pong packets and fragmented 500-byte commands/responses over `hal_lora_link`. | R0, S | `PROBE`, `RESPONDER`, `SF7`, `RESPONDER_SF7`, `LINK`, `LINK_RESPONDER` |
| `28_serial_commands` | Receive framed Serial Session commands and dispatch them to shared handlers. | R0, RA, RV, S, E | - |
| `29_bluetooth_gamepad` | Read gamepad input, discover Classic devices, and receive raw HID reports. | R0, RA, S | `CLASSIC_SCAN`, `HID_HOST`, `BLE` |
| `30_bluetooth_speaker` | Receive A2DP audio and play it over PWM, with optional AVRCP volume control and a build that includes BLE. | R0, RA | `AVRCP`, `BLE_A2DP` |

RP network examples default to `picow` for RP2040 and `pico2w` for
RP2350 ARM. RP2350 RISC-V configurations that require CYW43 are not
supported. STM32G474 network and Bluetooth projects use the NUCLEO-G474RE
profile with an external PIM730/RM2 module.

The LoRa project builds for the plain `pico` and `nucleo-g474re` boards with an
external Waveshare Core1262-HF wired as described in its README. Explicitly
select `rp2040-lora-lf` for the integrated Waveshare LF board. LF and HF devices use different bands;
do not combine them as one radio pair. The `PROBE` variant checks chip
capabilities, calibration, current RSSI, and CAD without transmitting.
The base and `RESPONDER` variants use SF9/10 dBm; `SF7` and
`RESPONDER_SF7` form a test pair at SF7/6 dBm.

The `LINK` and `LINK_RESPONDER` variants exchange a 500-byte binary `echo`
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
project header and the selected variant instead of the CMake dispatcher. An
example lists `HAL_TARGET_ESP32_S3` in `JH_PROJECT_TARGETS` only when every
feature it requests is on the target's `supportedFeatures` list; that list holds the ESP-IDF backends and
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
combined application impractical, and explain the constraint in this
catalog. A new example is a directory under `examples/` with a hand-written
`.vscode/jaszczurhal.project.json` and a `hal_project_config.h` that declares
its targets in `JH_PROJECT_TARGETS`. Run
`scripts/examples_dispatcher.py generate` to write its other VS Code files.

Add a variant to `JH_PROJECT_VARIANTS` only when the behavior cannot be
selected while the program runs, and limit it to some targets with `#error`
in the header. `scripts/examples_dispatcher.py list` shows the resulting
configurations; after a change, update the full-matrix and gate 9
configuration counts pinned in `tests/test_vscode_native_workflow.py`.

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

Select projects:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 \
  --example 01_core_runtime --example 10_storage
```

Build one variant:

```bash
vscode/entry/jh-vscode build \
  --project examples/16_rtc_backends \
  --target stm32g474 --board nucleo-g474re --variant DISPLAY_CLOCK
```

List configurations, and refresh the generated VS Code files after changing
a manifest or a header:

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py generate
scripts/examples_dispatcher.py check
```

`python3 scripts/sync_generated.py --write` runs `generate` together with the
other repository generators.

Final build outputs are stored in `.build/examples/<example>/`, and a
variant's in `.build/examples/<example>/variants/<id>/`. CMake working
directories use `.build/examples/<example>/cmake/<target>/<board>/`.

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
are finalized. Definitions may depend on the target selector or on a
variant's definitions; see
[Targets and variants](../doc/en/FwProjectWorkflow.md#targets-and-variants).

The build sets `HAL_PROVIDE_APP_ENTRY`. Pin assignments come from the
selected generated board profile. When no predefined composite profile
matches the application wiring, describe it with an explicit hardware
descriptor.

## VS Code

Task names and shortcuts are documented in
[JaszczurHAL in VS Code](../vscode/README.md). For project configuration,
target and board selection, source discovery, and output directories,
see [Firmware Project Workflow](../doc/en/FwProjectWorkflow.md).

The manifest is written by hand. `settings.json`, `tasks.json`,
`launch.json`, `keybindings.reference.json`, and `extensions.json` are
generated from the manifest and the header by
`scripts/examples_dispatcher.py generate`. To keep a wording change in them,
update the generator source; a hand edit is overwritten on the next refresh.

The `01_core_runtime` variant `CAPTURE` has a separate entry point because it reserves a capture input and timer/DMA resources; the base diagnostic application needs none of them.
