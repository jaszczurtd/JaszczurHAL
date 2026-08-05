# JaszczurHAL Examples

Each numbered directory is a dispatcher-backed firmware project with its own
`.vscode/jaszczurhal.project.json`. Open an example directory directly in VS
Code to use the same Build, Upload, Serial Monitor, Clean, Config Dump, OTA, and
board-selection tasks as a standalone firmware project.

The example registry lives in `scripts/examples_dispatcher.py`. Generated
manifests describe supported targets, board defaults, variants, sources, and
feature definitions. The quality gate builds those manifests through
`vscode/entry/jh-vscode` and `cmake/jh_firmware_project`.

## Supported build targets

| Target | Default board | Toolchain | Firmware artifacts |
|---|---|---|---|
| `rp2040` | `pico` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-arm` | `pico2` | official Pico SDK + GNU Arm | ELF, BIN, HEX, UF2, MAP |
| `rp2350-riscv` | `pico2` | official Pico SDK + pinned Hazard3 toolchain | ELF, BIN, HEX, UF2, MAP |
| `stm32g474` | `nucleo-g474re` | GNU Arm | ELF, BIN, HEX, MAP |

WiFi examples select `picow` for RP2040 and `pico2w` for RP2350 ARM.
RP2350 RISC-V examples that require CYW43 remain excluded because that network
combination is unsupported. On STM32G474, network examples select the standard
NUCLEO-G474RE board plus a target-specific external PIM730 profile using
PB14/WL_ON, PB12/chip-select, PB15/DAT and PB13/clock.

## Requirements

- CMake 3.20 or newer for dispatcher-backed firmware builds;
- Python 3;
- `arm-none-eabi-gcc` for RP2040, RP2350 ARM, and STM32G474;
- managed Pico SDK, picotool, FreeRTOS, lwIP, BearSSL, littlefs, and RISC-V
  components prepared by `./runmefirst.sh` or
  `./third_party/update_components.sh`.

## Build commands

Build every declared example for one target:

```bash
scripts/examples_dispatcher.py build --target rp2040 --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-arm --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target rp2350-riscv --jobs "$(nproc)"
scripts/examples_dispatcher.py build --target stm32g474 --jobs "$(nproc)"
```

Build one project through the same CLI used by VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/01_blink --target rp2040 --board pico
```

Limit the registry runner to one or more examples:

```bash
scripts/examples_dispatcher.py build \
  --target rp2040 --example 01_blink --example 16_littlefs
```

Inspect the current generated matrix:

```bash
scripts/examples_dispatcher.py list
```

Regenerate manifests and VS Code support files after changing the registry:

```bash
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
#include <hal/hal_app.h>
#include <hal/hal_board.h>
#include <hal/hal_gpio.h>
#include <hal/hal_system.h>

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

The build flow supplies `HAL_PROVIDE_APP_ENTRY`. Board-specific pin facts come
from the selected generated profile; project wiring remains in the project
configuration.

## Example groups

The registry currently covers:

- core runtime, GPIO, ADC, PWM, timers, synchronization, serial, and FreeRTOS;
- I2C, SPI, UART, software serial, CAN, and native STM32 FDCAN;
- EEPROM/KV, LittleFS, FatFs, SDLogger, and flash transaction behavior;
- sensors, RTC, GPS, PMIC, simple I/O chips, RFID/NFC, and output devices;
- TFT, OLED, EPD, PNG, JPEG, and graphics helpers;
- WiFi, UDP/TCP, BSD sockets, HTTP/HTTPS, TLS, MQTT, WireGuard, WebSocket,
  network console, command dispatch, OTA, and experimental BLE Peripheral.

Notable specialized examples:

- `29_freertos_smoke` validates task affinity, worker tasks, mutexes, delay,
  idle, and USB service;
- `38_stm32g474_fdcan_native` targets NUCLEO-G474RE FDCAN1;
- `39_sdlogger` exercises SD/FatFs plus EEPROM-backed counters;
- `42_bsd_sockets_tcp_udp` provides TCP/UDP client/server variants;
- `56_http_https_client` demonstrates verified BearSSL HTTPS;
- `57_ota` demonstrates discovery, authenticated staging, trial confirmation,
  rollback, and BOOTSEL recovery.
- `58_ble_peripheral` demonstrates experimental BLE advertising and connection
  events on Pico W and STM32G474 with PIM730/RM2.

Use the registry `list` command for the exact per-example target and variant
matrix. This avoids duplicating build metadata in prose.

## VS Code

The generated task labels and keyboard-shortcut reference are documented in
[JaszczurHAL VS Code Entry](../vscode/README.md). Project configuration,
target/board resolution, source discovery, and artifact paths are documented in
[Firmware Project Workflow](../doc/FwProjectWorkflow.md).
