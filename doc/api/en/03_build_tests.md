# Build dependencies, tests, and hardware fixtures

*Also available in [Polish](../pl/03_build_tests.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

## Dependencies (hardware build)

| HAL module | External dependency |
|---|---|
| ESP32-S3 component model | Pinned ESP-IDF with one generated source/dependency graph: baseline core/simple-PWM sources, feature-selected Phase 2 peripherals, and native Phase 3 connectivity/services. |
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system` | Pico SDK `hardware_*` / `pico_*` APIs on the RP family; STM32G474 register backend; ESP-IDF GPIO, LEDC PWM, ADC and system services for ESP32-S3. `hal_system` also uses FreeRTOS task APIs in supported `HAL_ENABLE_FREERTOS` builds. |
| `hal_usb` | HAL-owned TinyUSB device on RP: CDC descriptors, IRQ/timer pump in bare builds, core-0 worker task in FreeRTOS builds, and BOOTSEL reset. STM32G474 is currently unsupported. Mock provides deterministic CDC buffers and a reset observer. |
| `hal_serial` | One target-independent serial/debug core plus link-time ports: RP `hal_usb` CDC, ESP32-S3 startup-owned USB Serial/JTAG VFS, STM32G474 debug USART2/host stdout, and mock stdout capture/injectable RX. |
| `hal_sync` | RP: Pico SDK `pico/mutex.h` in bare builds and FreeRTOS semaphores in RTOS builds. STM32G474: atomic spinlock in bare builds and FreeRTOS mutexes in RTOS builds. ESP32-S3: ESP-IDF FreeRTOS mutexes and `portMUX_TYPE` critical sections. |
| `hal_timer` | RP2040: pico SDK alarm/time APIs (`pico/time.h`); STM32G474: TIM6 + NVIC register backend; ESP32-S3: ESP-IDF GPTimer default and dedicated pools. |
| `hal_soft_timer` | internal `SmartTimers` utility |
| `hal_pid_controller` | internal `pidController` utility |
| `hal_can` | generic CAN facade plus backend-selected CAN drivers: MCP2515 (`hal/can/mcp2515/*`), MCP251XFD (`hal/can/mcp251xfd/*`) and STM32G474 native FDCAN (`impl/stm32g474/hal_can_stm32g474_fdcan.*`) |
| `hal_display` | Shared display stack (`hal/display/drivers/hal_display.cpp`, `jh_gfx.*`, `ili9341_driver.*`, `st77xx_driver.*`, `ssd1306_driver.*`) reused by RP2040 and STM32G474; target backends provide SPI/I2C/GPIO transport |
| `hal_hd44780` | shared HD44780-compatible character LCD driver (`hal/display/hd44780/hd44780.*`) over HAL GPIO/system timing |
| `hal_dma_pwm_audio` | timer-paced PWM-audio DMA helper used by DACless on RP2040, STM32G474 and mock |
| `hal_dacless` | shared DACless PWM-audio engine (`hal/audio/dacless/dacless.*`) over HAL DMA/PWM-freq, ADC, timing and synchronization |
| `hal_tsc2007` | shared TSC2007 resistive touch controller driver (`hal/input/tsc2007/tsc2007.cpp`) over HAL I2C/system timing |
| `hal_stmpe610` | shared STMPE610 resistive touch controller driver (`hal/input/stmpe610/stmpe610.cpp`) over HAL I2C or HAL SPI/GPIO |
| `hal_irsmall_decoder` | shared IR receiver decoder (`hal/input/irsmall_decoder/irsmall_decoder.cpp`) over HAL GPIO interrupts and system timing |
| `hal_spi` | RP2040 native Pico SDK `hardware/spi.h`; STM32G474 register backend; ESP32-S3 ESP-IDF SPI master on SPI2/SPI3. |
| `hal_lora_radio` | Mutually exclusive family providers: the pinned official Semtech SX126x driver with the HAL adapter for validated SX1262 and experimental software-only SX1261, or the HAL-owned register provider for experimental software-only SX1276/SX1278; both compile for RP and STM32G474 and have deterministic mock coverage |
| `hal_lora_link` | HAL-owned protocol over one configured `hal_lora_radio`; CRC-32 is internal, ChaCha20-Poly1305 uses the optional `hal_crypto` module, and no additional third-party dependency is introduced |
| `hal_i2c` | RP2040 native Pico SDK `hardware/i2c.h`; STM32G474 register backend; ESP32-S3 ESP-IDF I2C master on I2C0/I2C1. |
| `hal_swserial` | native Pico SDK PIO/DMA backend on RP2040; shared HAL GPIO/timing/sync backend on other targets |
| `hal_gps` | one portable facade selecting `hal_uart` / `hal_swserial` at compile time, plus the shared in-tree NMEA engine |
| `hal_rgb_led` | shared NeoPixel core (`hal/gpio/neopixel/jh_neopixel.*`) + target transport implementation, including ESP32-S3 RMT |
| `hal_thermocouple` (MCP9600/MCP9601) | shared driver (`hal/temperature/mcp9600/mcp9600_driver.*`) |
| `hal_thermocouple` (MAX6675) | shared driver (`hal/temperature/max6675/max6675_driver.*`) |
| `hal_onewire` | shared bit-bang driver (`hal/onewire/onewire_driver.*`) over HAL GPIO/time |
| `hal_ds18b20` | shared DS18B20 backend (`hal/temperature/ds18b20/hal_ds18b20.cpp`) over shared OneWire |
| `hal_external_adc` | shared ADS1X15/ADS1115 driver (`hal/analog/ads1x15/ads1x15_driver.*`) |
| `hal_pga2311` | shared PGA2311 stereo volume driver (`hal/audio/pga2311/pga2311_driver.*`) over HAL SPI/GPIO |
| `hal_wifi` | pinned CYW43 driver/lwIP on RP and STM32G474, or native ESP-IDF WiFi/`esp_netif`/lwIP on ESP32-S3 |
| `hal_littlefs` | one target-independent facade and shared provider over the pinned `third_party/littlefs` core; RP and STM32G474 provide geometry and coordinated raw internal-flash operations, while a dedicated host integration test uses a RAM flash model |
| `hal_udp` | shared lwIP raw UDP engine over the selected CYW43 network backend |
| `hal_tls` | bundled BearSSL over native `hal_tcp`; the optional BSD transport adapter is built only when `HAL_ENABLE_BSD_SOCKETS` is also enabled |
| BSD sockets adapter | shared `hal/network/adapters/bsd/hal_bsd_sockets.cpp` over HAL UDP/TCP; remains independently selectable without TLS |
| `hal_wireguard` | shared WireGuard/lwIP engine + capability-advertised host-lwIP backend |
| `hal_mqtt` | bundled `PubSubClient` over HAL TCP, with optional BearSSL MQTTS transport |
| `hal_notify` | backend-dispatched facade plus Telegram over the shared HTTP/HTTPS client |
| `hal_ota` | RP staging/applier with authenticated VS Code transport over HAL UDP/TCP |
| `hal_time` | Shared Gregorian/CET/CEST and interval helpers, plus HAL UDP/NTP client and target timekeeping integration |
| `hal_kv` | internal `hal_eeprom` + `hal_sync` |
| `hal_sdlogger` | pinned FatFs R0.16 core plus the shared file layer in `hal/storage/filesystem/` |
| `tools` | HAL APIs |
| `multicoreWatchdog` | internal `SmartTimers` + `hal_sync` mutex |

## Dependencies (mock / PC build)

All `impl/.mock/` files depend only on standard host headers such as
`<cstdio>`, `<cstring>`, `<mutex>`, `<queue>`, and `<stdarg.h>`. No embedded
SDK is required.

---

## Test system map and sources of truth

| Test layer | Configuration source | Execution | Extension point |
|---|---|---|---|
| Host/mock unit tests | `tests/CMakeLists.txt`, `tests/test_*.cpp`, root `CMakeLists.txt` | CMake plus CTest | Add a Unity suite and register it with `add_hal_test(...)`, or declare a dedicated executable for extra sources. |
| FreeRTOS POSIX host tests | `tests/freertos_posix/`, `JH_ENABLE_FREERTOS_POSIX_TESTS` | CTest through the host build or full gate | Add a target with `add_hal_freertos_posix_test(...)`. |
| Repository quality gate | `runalltests.sh`, `.github/workflows/ci.yml`, and the tooling data described in `00_scripts.md` | `./runalltests.sh` | Extend the owning gate and its focused regression tests; keep generated artifacts below `.build/`. |
| Firmware compile fixtures | `tests/fixtures/<fixture>/.vscode/jaszczurhal.project.json` | `jh-vscode` or the target production runner | Extend the manifest target/board/variant matrix and its artifact-layout test. |
| Physical hardware fixtures | `tests/hardware/<fixture>/` source, manifest, and verifier | Build/upload through `jh-vscode` or the target production runner, then run the verifier documented below | Add the firmware, explicit hardware matrix, host oracle, acceptance criteria, and a subsection in this document. |

The executable files above are the source of truth when prose and behavior
disagree. Each hardware fixture keeps only a short README link for local
discovery. Complete operator instructions, wiring, requirements, and recorded
acceptance results are centralized in the
[Hardware fixtures](#hardware-fixtures) section below.

---

## Unit tests

### Requirements

- CMake ≥ 3.16
- GCC / Clang with C++17

### Build and run

```bash
cmake -B .build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
```

## Repository quality gates

### Quick start scripts

Two convenience scripts in the repository root simplify local development:

**`runmefirst.sh`** - One-time toolchain setup
```bash
./runmefirst.sh
```
Configures your local environment for the first time:
- Installs git hooks (pre-commit and commit-msg from `.githooks/`)
- Synchronizes all pinned components through `third_party/update_components.sh`
- Installs persistent RP2040/RP2350 USB and `/dev/ttyACM*` access rules for
  sudo-less upload and automatic 1200-bps BOOTSEL reset
- Offers persistent, LAN-scoped TCP/8266 firewall setup for OTA callbacks
- Sets up build directories and initial CMake configuration
- Run this once when cloning the repository or after environment changes

The pre-commit hook normalizes and formats staged files, then verifies all
tracked generated artifacts. If any output is missing or stale, it blocks the
commit and asks you to run `python3 scripts/sync_generated.py --write`, review
the changes, and stage them before retrying.

**`runalltests.sh`** - Full validation gate
```bash
./runalltests.sh
./runalltests.sh --check-generated
```
The default mode refreshes deterministic tracked output before the gates and
lists files changed by that synchronization in the final summary.
`--check-generated` verifies the same output without repairing drift; CI uses
this stricter mode through `scripts/sync_generated.py --check`.

Runs the complete quality-gate suite (9 gates, in order):
1. Tool-presence check
2. Host/mock unit tests (`.build/gate/host/` + ctest, incl. FreeRTOS POSIX)
3. Clang ASan/UBSan tests and bounded libFuzzer smoke checks through the same
   runner used by CI
4. Memory safety (Valgrind memcheck on all native C/C++ test executables)
5. Static analysis: cppcheck
6. Static analysis: clang-tidy (host + STM32 compile databases below
   `.build/gate/`)
7. PMD CPD duplicate detection across owned C/C++ implementations and Python
   scripts
8. Target builds (STM32G474 plus Pico SDK RP2040/RP2350 ARM/RP2350 RISC-V
   entry/core probes, RP feature profiles, six representative
   `01_core_runtime`/`18_freertos_suite` ELF/BIN/UF2 builds, and one clean
   compile-only `tests/fixtures/esp32s3_phase3` build with the pinned ESP-IDF
   and validated multi-image manifest)
9. Examples build (the dispatcher-derived `gateTargets` matrix plus dedicated
   target/runtime fixtures)

Exits non-zero on the first failure; logs capture warnings/errors from both
standard output and standard error.
The Valgrind gate selects every directly registered native C/C++ test executable
through the CTest `memcheck` label. `MEMCHECK_REQUIRED_TESTS` in
`runalltests.sh` is a critical subset checked before execution, not the complete
selection. Python, CMake, and shell-driver tests remain outside memcheck. Fair
Valgrind thread scheduling keeps the native FreeRTOS POSIX scheduler tests in
the selection. Live CTest/Valgrind progress is streamed to the terminal and
`.build/gate/logs/jh_memcheck.log`.

All repository-owned compilation output is kept below the single ignored
`.build/` root. CMake script-mode compiler probes use `.build/tests/`; they do
not emit `.o` files into the repository root.

The clang-tidy gate creates profile-specific analysis databases with one
compile command per source file. This keeps facade tests that compile the same
shared driver under several feature sets from triggering duplicate analyzer
runs while normal target builds still compile every configured variant.

The CPD gate uses the authenticated PMD 7.26.0 distribution managed under
`third_party/pmd`. It scans C/C++ implementation files rather than headers and
Python files below `scripts/`, while excluding generated and vendored sources.
Every C/C++ duplicate group from 100 tokens blocks in production, tests, and
examples; every Python-script group from 50 tokens also blocks. No baseline or
allowlist can hide an existing group. The report computes the union of
duplicated token ranges and prints coverage globally and for mock, RP2040,
STM32G474, shared, remaining portable code, and Python scripts. XML reports and
deterministic file lists are written below `.build/gate/cpd/`. CPD `PASS` means
zero groups at the configured language-specific thresholds.

This is the **recommended pre-commit validation** and **CI/CD test gate**. Run before pushing changes to catch cross-platform issues early.

### Native Windows CI gate

`.github/workflows/ci.yml` runs two native `windows-2025` gates in addition to
the complete Linux quality gate:

- `windows-tooling` prepares the authenticated managed environment, repeats
  `runmefirst.ps1 -VerifyOnly`, runs the shared runtime/platform/bootstrap and
  generator tests, verifies the RP and STM32 FreeRTOS CMake dependency source
  selection, performs a clean production ESP32-S3/ESP-IDF build and uploads its
  multi-image artifacts, then compiles and runs the portable host checks
  with MSVC `/W4 /permissive- /WX`;
- `Windows firmware (<target>)` builds a generated consumer from a path
  containing spaces through Ninja for `rp2040`, `rp2350-arm`,
  `rp2350-riscv`, and `stm32g474`, checks the target artifacts and patched
  compile database, and uploads the representative build artifacts.

The Windows CTest inventory keeps the POSIX BSD adapter, Bash/POSIX BearSSL
integration, and FreeRTOS GCC/POSIX runtime visible as disabled tests. Their
active coverage, together with Valgrind, cppcheck, clang-tidy, and PMD CPD,
remains in the Linux gate. Fiesta, DoomConsole, and Ford DPF Tracker own separate native
Windows firmware workflows, which provide consumer-specific integration
coverage in addition to JaszczurHAL's generated-consumer fixture.

## Hardware fixtures

The repeatable physical-device probes use the same VS Code dispatcher as
applications and keep their artifacts below `.build/hardware/`:

| Fixture | Coverage |
|---|---|
| `tests/hardware/bluetooth_stage1` | Internal pre-API CYW43/BTstack controller, advertising, static GATT and WiFi-only memory baseline on Pico W and STM32G474/PIM730. |
| `tests/hardware/bluetooth_gamepad` | Sanitized 8BitDo Zero 2 Android D-input descriptor/report capture and the private Classic HID Host gamepad parser probe for Pico 2 W. |
| `tests/hardware/bluetooth_classic_hid_device` | Private Pico W Classic HID mouse used to validate the public generic HID Host on a second Pico radio. |
| `tests/hardware/bluetooth_classic_hci_trace` | Private privacy-preserving raw HCI inquiry trace and CYW43 transport/clock diagnostics for Pico W and Pico 2 W. |
| `tests/hardware/bluetooth_observer` | Public passive Observer scan, bounded report queue and Teltonika/iBeacon/Eddystone BLE parsing on Pico W, Pico 2 W and STM32G474/PIM730. |
| `tests/hardware/bluetooth_stream` | Public BLE lifecycle and authenticated Stream gate across target/board/runtime tuples, including reconnect, watchdog, sustained traffic, saturation and negative security cases. |
| `tests/hardware/rp_usb_cdc_echo` | Native TinyUSB CDC enumeration, backpressure, reconnect and throughput |
| `tests/hardware/rp_usb_multicore` | Concurrent CDC producers on both RP cores, record integrity, completeness and affinity in bare-metal/FreeRTOS |
| `tests/hardware/rp_freertos_smp` | Scheduler, both cores, mutex/delay, heap and USB under FreeRTOS SMP |
| `tests/hardware/rp_flash_transaction` | Flash coordinator sequencing, rejection paths, erase/program and recovery |
| `tests/hardware/rp_kv_power_loss` | Dual-bank KV recovery after interruption following erase, body write, verification and publication |
| `tests/hardware/rp_storage` | EEPROM commit/persistence, LittleFS format/remount and cross-reset mounting |
| `tests/hardware/rp_sdlogger` | Physical SPI SD mount, deterministic append, flush/close, reset/remount, content and EEPROM log-counter persistence |
| `tests/hardware/rp_ota` | Discovery, authentication, transfer, trial/confirm, rollback, persistent-storage coexistence and USB/network recovery |
| `tests/hardware/lora_sx1262` | Two-device SX1262 initialization, bidirectional raw packets, reliable-link lifecycle, and fragmented command-router request/response transactions on integrated LF or external HF pairs |
| `tests/hardware/esp32s3_phase1` | Phase 1 ESP32-S3 target/board identity, generated link signature, chip/core count, physical flash, initialized Quad PSRAM, and a repeated FreeRTOS `app_task0()` heartbeat over native USB Serial/JTAG. |
| `tests/hardware/esp32s3_phase2` | ESP32-S3 Phase 2 runtime probe for both application tasks, system/sync, GPIO/IRQ, ADC, USB Serial/JTAG TX/RX, hardware UART, I2C master scan, SPI master transfer path, dedicated-pool timer callbacks, and enabled FreeRTOS stack-guard configuration. |

The subsections below are the complete operator reference for each physical
fixture. A successful firmware build is only a software result unless the
fixture explicitly states otherwise; physical acceptance requires its host or
visual oracle and the recorded PASS criteria.

### RP USB CDC hardware probe

`tests/hardware/rp_usb_cdc_echo` validates the native RP TinyUSB owner on a
physical Pico or Pico 2, including the RP2350 ARM and RISC-V targets. The
firmware echoes arbitrary CDC bytes and toggles the board LED after each fully
echoed USB receive block.

Build and perform the first BOOTSEL upload:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload-uf2 \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
```

For Pico W and Pico 2 W use `--board picow` and `--board pico2w`, respectively.
When another board is already in BOOTSEL, target-neutral `upload` snapshots the
existing drives before the 1200-bps touch and writes only to the newly appeared
drive.

Validate data integrity, delayed host reads, throughput, and close/reopen:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_usb_cdc_echo/verify_cdc_echo.py \
  --port /dev/serial/by-id/<device>
```

After the first flash, the target-neutral `upload` action must enter BOOTSEL
through the 1200-bps DTR touch and return with the same CDC identity:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Use an explicit stable by-id port when multiple compatible boards are
connected. The workflow intentionally refuses to guess between two verified
ports.

#### Linux runtime suspend and resume

Close every process holding the CDC port. Set `USB_DEVICE_SYSFS` to the USB
device node, not its interface node (for example,
`/sys/bus/usb/devices/3-4.1.4`):

```sh
printf '0\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/autosuspend_delay_ms" >/dev/null
printf 'auto\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 3
cat "$USB_DEVICE_SYSFS/power/runtime_status"

printf 'on\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 1
cat "$USB_DEVICE_SYSFS/power/runtime_status"
```

The expected states are `suspended` and then `active`. Run
`verify_cdc_echo.py` again after resume, then restore the original
`autosuspend_delay_ms` and `control` values.

### RP multicore USB hardware probe

`tests/hardware/rp_usb_multicore` starts one CDC producer on each RP core. Both
producers write 4096 independently numbered and checksummed records through
`hal_usb` while the host verifies record boundaries, integrity, completeness,
producer affinity, and the final HAL status. A malformed line detects
byte-level interleaving between concurrent writes.

Build and upload the bare-metal RP2040 variant:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_usb_multicore/verify_usb_multicore.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

For Pico 2, select `rp2350-arm` or `rp2350-riscv`, use the `pico2` build board,
and pass `--board pico2` to the verifier. Add `--variant freertos` to build and
upload commands and use `--runtime freertos` for the FreeRTOS SMP run.

The verifier's default `--records 4096` must match
`JH_USB_MULTICORE_RECORDS` in the firmware build.

### RP FreeRTOS SMP hardware probe

`tests/hardware/rp_freertos_smp` validates the pinned native FreeRTOS kernel on
a physical Pico or Pico 2. It verifies scheduler startup, application task
affinity on both cores, cross-core HAL mutex operation, FreeRTOS heap reporting,
and native USB CDC traffic with delayed host reads.

Build and upload through the regular native VS Code workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Run the host verifier:

```sh
python3 tests/hardware/rp_freertos_smp/verify_freertos_smp.py \
  --port /dev/serial/by-id/<device>
```

Use `rp2350-arm` or `rp2350-riscv` with board `pico2` for Pico 2. When the
device has no running CDC firmware yet, use `upload-uf2` while it is in
BOOTSEL.

### RP flash transaction hardware probe

`tests/hardware/rp_flash_transaction` validates the native RP flash
coordinator on a physical Pico or Pico 2. It runs RAM-resident operations from
both cores, verifies rejection of active DMA and XIP callbacks, checks
recursive-entry handling, mutates the last flash sector, and verifies cleanup
plus recovery after an operation stops between erase and program.

The probe intentionally owns the last sector of the board flash. Do not run it
on firmware that stores unrelated data there.

Build and upload the bare-metal variant through the regular workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

For the FreeRTOS SMP variant, add the following temporary cache entry to the
manifest and run the same build/upload commands:

```json
"JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS=1"
```

Remove the cache entry before rebuilding the bare-metal variant.

Run the verifier:

```sh
python3 tests/hardware/rp_flash_transaction/verify_flash_transaction.py \
  --port /dev/serial/by-id/<device>
```

### RP KV power-loss hardware probe

`tests/hardware/rp_kv_power_loss` enables a build-only fault-injection hook in
the native flash provider. It interrupts inactive-bank replacement after
invalidation, body programming, body verification, or publication. After each
case the fixture reloads the EEPROM mirror from physical flash, just as a new
boot would, and asks shared `hal_kv` to select a bank. The first three cases
must recover the previous value; the late error after complete publication
must recover the new value. A deferred two-key commit is checked as well.

The probe erases and owns the complete native EEPROM/KV reservation. Do not run
it on a board whose persistent tail must be preserved. The fault-injection
define is fixture-only and must not be used by application firmware.

Build and upload with the regular workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_kv_power_loss \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_kv_power_loss \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Run the verifier:

```sh
python3 tests/hardware/rp_kv_power_loss/verify_kv_power_loss.py \
  --port /dev/serial/by-id/<device> --target rp2040
```

Use `--target rp2350-arm --board pico2` for Pico 2 and pass the same target to
the verifier. Physical RP2040 and RP2350 ARM runs passed on 2026-09-02.

### RP native storage hardware probe

`tests/hardware/rp_storage` validates native EEPROM and LittleFS on physical
RP2040/RP2350 hardware. It commits an EEPROM boot counter, formats and remounts
the LittleFS partition, resets through the watchdog, then verifies EEPROM
persistence and a LittleFS mount without another format.

Build and upload through the regular native workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_storage \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_storage \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Run the verifier:

```sh
python3 tests/hardware/rp_storage/verify_storage.py \
  --port /dev/serial/by-id/<device>
```

Use `rp2350-arm` or `rp2350-riscv` with board `pico2` for Pico 2.

### RP SDLogger hardware probe

`tests/hardware/rp_sdlogger` validates the shared SDLogger with a physical SPI
SD card. It mounts the card, opens the EEPROM-numbered log, appends
deterministic content, flushes and closes the file, resets through the
watchdog, remounts the card, checks the exact appended file tail, and verifies
that the EEPROM log counter persisted.

Connect a 3.3 V SPI SD module to a Pico or Pico 2:

| SD signal | RP GPIO | Pico physical pin |
|---|---:|---:|
| MISO | GP16 | 21 |
| CS | GP17 | 22 |
| SCK | GP18 | 24 |
| MOSI | GP19 | 25 |
| 3V3 | 3V3(OUT) | 36 |
| GND | GND | 23 |

Build, upload, and verify the bare-metal RP2040 variant:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_sdlogger \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_sdlogger \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_sdlogger/verify_sdlogger.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

For Pico 2, select `rp2350-arm` or `rp2350-riscv`, use the `pico2` build board,
and pass `--board pico2` to the verifier. Add `--variant freertos` to build and
upload commands and use `--runtime freertos` for the FreeRTOS run.

The verifier is repeatable without formatting the card. If an old log file
with the same name exists, it validates the newly appended deterministic tail.

### Native RP OTA hardware probe

`tests/hardware/rp_ota` validates OTA discovery and authentication,
acknowledged chunk-by-chunk transfer, trial boot, explicit confirmation, a
second unconfirmed trial, automatic rollback, and network/USB recovery after
each reboot. The same fixture increments a boot counter in two-bank `hal_kv`
and mounts a separate LittleFS partition, proving that both persistent users
remain intact while OTA program, staging and control regions change. It
supports Pico W/RP2040 and Pico 2 W/RP2350 ARM in bare-metal and FreeRTOS
builds, plus an ordinary Pico/RP2040 connected to a PIM730/RM2 wireless
breakout.

Copy the local secret template and replace all values. The resulting header is
ignored by Git:

```sh
cp tests/hardware/rp_ota/ota_test_secrets.example.h \
  tests/hardware/rp_ota/ota_test_secrets.h
```

The verifier reads the OTA password from this ignored header. An environment
variable can override it when needed:

```sh
export JH_OTA_TEST_PASSWORD='the-value-from-ota_test_secrets.h'
```

Build, upload, and verify the bare-metal RP2040 variant:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota \
  --target rp2040 --board picow
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota \
  --target rp2040 --board picow \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board picow --runtime baremetal
```

Use `--target rp2350-arm --board pico2w` for Pico 2 W. Add
`--variant freertos` to both build and upload, then pass `--runtime freertos`
to the verifier for the FreeRTOS variant. The fixture gives its application
task an 8 KiB stack in FreeRTOS builds because CYW43 initialization and OTA
handling exceed the general-purpose 2 KiB default.

For Pico+PIM730, connect the breakout to an ordinary Pico as follows:

| PIM730 | Pico signal | Physical pin |
|---|---|---|
| `WL_ON` | GP2 | 4 |
| `CS` | GP3 | 5 |
| `DAT` | GP4 | 6 |
| `CLK` | GP5 | 7 |
| `GND` | GND | 8 |
| `3V3` | 3V3(OUT) | 36 |

`DAT` is the combined bidirectional data/host-wake signal. Leave `BL_ON`,
`GPIO0..2`, and `N/C` disconnected. Build and verify this profile explicitly:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota \
  --target rp2040 --board pico-rm2
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota \
  --target rp2040 --board pico-rm2 \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico-rm2 --runtime baremetal \
  --artifact-dir .build/hardware/rp_ota/cmake/rp2040/pico-rm2
```

Add `--status-only` to validate the board identity, network readiness, and
automatic gSPI clock telemetry without generating or transferring OTA images.
This diagnostic mode does not require the OTA password or build artifacts.

Use `--variant freertos`, `--runtime freertos`, and the artifact directory
`.build/hardware/rp_ota/cmake/variants/freertos/rp2040/pico-rm2` for the
FreeRTOS run.

When several target/runtime builds exist at once, point the verifier at the
matching CMake output instead of the last published artifact directory:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --target rp2350-arm --board pico2w --runtime freertos \
  --artifact-dir \
    .build/hardware/rp_ota/cmake/variants/freertos/rp2350-arm/pico2w
```

Pico W and Pico 2 W may remain connected together. Their OTA hostnames are
`jh-ota-rp2040` and `jh-ota-rp2350-arm`, so discovery never guesses between
them. Pico W and Pico+PIM730 share the RP2040 hostname; when both are powered,
pass the selected device's IP to `--broadcast`. The CDC status response
includes and the verifier checks the board profile, current IPv4 address,
target, runtime and OTA boot state, plus the live `clk_sys`, requested and
effective gSPI rates, 16.8 divider, selected PIO timing program, KV boot count
and LittleFS mount/format state. The
verifier creates signed A/B containers below `.build/hardware/rp_ota` and
leaves the device in stable image A after proving rollback from image B.

`hal_ota_begin()` also publishes the configured hostname through mDNS. While
the probe is connected, verify the responder independently with, for example,
`getent hosts jh-ota-rp2040.local` (or the RP2350 hostname). The WiFi hostname
is sent in DHCP option 12 and changing it with an active lease triggers a DHCP
renewal.

The OTA upload callback is a TCP connection from the board to host TCP/8266.
Run `./runmefirst.sh` and approve its persistent, LAN-scoped OTA rule before the
hardware test. Verify the rule without changing it with:

```sh
python3 scripts/configure_ota_firewall.py --check
```

On native Windows, first verify that the selected trusted LAN has a `Private`
connection profile, then inspect the exact rule plan without changing the
host:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

Apply the same scope from an already elevated PowerShell after removing
`--dry-run`. The helper requests confirmation, creates an idempotent Windows
Defender Firewall rule limited to the `Private` profile, interface, source
subnet, and TCP/8266, and never changes the network profile itself. Windows
hardware verification uses the managed Python executable and accepts a COM
port such as `--port COM3`.

If the test network differs from the network selected during initial setup,
re-run the helper with explicit `--interface` and `--network`. If
limited-broadcast discovery is blocked but the device IP is known, use a
unicast discovery destination:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  <other-options> --broadcast <device-ip>
```

When a factory UF2 replaces the active program with a different target/runtime
build, or a board reused by another flash fixture reports an invalid initial
OTA state, enter BOOTSEL and erase only the four OTA control sectors before
loading the new UF2. The old metadata contains a digest of the previous active
program and must not be paired with the replacement image. The absolute ranges
are `0x101fc000..0x10200000` for Pico W or Pico+PIM730 and
`0x103fc000..0x10400000` for Pico 2 W:

```sh
.build/tools/picotool/picotool erase -r <range-start> <range-end> \
  --bus <usb-bus> --address <usb-address>
```

This automated probe does not simulate loss of power during an in-progress
flash swap. Power-cut validation requires a controlled power switch and is a
separate destructive/recovery run.

### Bluetooth Stage 1 hardware probe

`tests/hardware/bluetooth_stage1` is an internal probe for the pre-API
CYW43/BTstack integration. Its build matrix covers STM32G474 Nucleo + PIM730,
Raspberry Pi Pico W, and RP2350 ARM Pico 2 W. It deliberately enables no public
Bluetooth feature macro and must not be used as an application API example.

The historical Stage 1 hardware runs below cover Nucleo+PIM730 and Pico W.
Pico 2 W hardware acceptance uses the public
[Bluetooth Observer](#bluetooth-observer-hardware-probe) and
[BLE Stream](#jh-ble-stream-v1-hardware-gate) fixtures instead. RP2350 RISC-V
is unsupported because its CYW43 Bluetooth transport is not enabled.

The build owns BTstack sources directly and does not link `pico_cyw43_arch`,
`pico_btstack_cyw43`, or Pico SDK Bluetooth storage integration. It brings up the
shared JH CYW43 radio owner through its BLE reference, downloads the Bluetooth
firmware through the same CYW43 instance, starts connectable advertising as
`JH BLE Stage 1`, and exposes a bounded static read/write GATT characteristic.

Successful compilation is only the software gate. Hardware results must record
the `JHBT1` output, connection/write behaviour, ELF/map memory use, and the
exact board/wiring under test. The STM32 run additionally verifies that the
PIM730 `BT_ON` trace still follows `WL_ON` in the assembled setup.

The `bluetooth` variant is the probe; `wifi-only` is the otherwise equivalent
memory baseline. Both variants must be measured from their ELF/map files with
the same target, board, compiler, and build type.

The Stage 1 software builds measured on 2026-08-04 are:

| Target and variant | FLASH load | SRAM static | Reserved heap/stack |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 332.3 KiB | 50.0 KiB | 3.0 KiB |
| STM32G474 + PIM730, `wifi-only` | 276.9 KiB | 43.2 KiB | 3.0 KiB |
| RP2040 Pico W, `bluetooth` | 403.2 KiB | 60.4 KiB | 6.0 KiB |
| RP2040 Pico W, `wifi-only` | 326.0 KiB | 53.6 KiB | 6.0 KiB |

These measurements do not require a reduced ATT MTU or smaller Stage 1 queues.

After the Stage 2 shared-owner migration, the matched images measured:

| Target and variant | FLASH load | SRAM static | Reserved heap/stack |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 326.0 KiB | 48.4 KiB | 3.0 KiB |
| STM32G474 + PIM730, `wifi-only` | 278.1 KiB | 43.2 KiB | 3.0 KiB |
| RP2040 Pico W, `bluetooth` | 393.8 KiB | 57.3 KiB | 6.0 KiB |
| RP2040 Pico W, `wifi-only` | 327.8 KiB | 53.6 KiB | 6.0 KiB |

The WiFi-only images still exclude BTstack, Bluetooth firmware, and shared-bus
Bluetooth pools. The owner migration adds no static SRAM to either WiFi-only
baseline.

After the Stage 3 controller-interface, bounded-HCI, and JH-owned run-loop
migration, the matched images measured:

| Target and variant | FLASH load | SRAM static | Reserved heap/stack |
|---|---:|---:|---:|
| STM32G474 + PIM730, `bluetooth` | 327.1 KiB | 48.5 KiB | 3.0 KiB |
| STM32G474 + PIM730, `wifi-only` | 278.1 KiB | 43.3 KiB | 3.0 KiB |
| RP2040 Pico W, `bluetooth` | 390.1 KiB | 57.3 KiB | 6.0 KiB |
| RP2040 Pico W, `wifi-only` | 322.5 KiB | 53.6 KiB | 6.0 KiB |

The Stage 3 hardware gate repeated controller startup, advertising, BlueZ
connection, and GATT service resolution on both boards. STM32G474 + PIM730
recorded symmetric ACL traffic at `11/11` and two drain-budget hits confined
to initialization. Pico W recorded symmetric ACL traffic at `11/11` with no
drain-budget hits. Both transports remained `HAL_OK`, and both boards were
left running the `bluetooth` variant.

Hardware substage 1.a completed on both profiles on 2026-08-04. The
STM32G474 + PIM730 probe used the wiring below. The Pico W probe used its
on-board CYW43439 and enumerated as `JaszczurHAL RP` over USB. On both boards
the probe reached controller-ready and connectable advertising states, BlueZ
resolved the static GATT service, characteristic read and write passed, and the
peripheral accepted a disconnect followed by a fresh connection and GATT read.
The matched `wifi-only` images also reported `HAL_OK`.

Initial STM32 ATT
discovery exposed a missing Security Manager initialization; the probe now
initializes `sm_init()` before `att_server_init()`. Connection lifecycle is
observed through one HCI event registration so each physical link is counted
once.

The final image restored to each board is the `bluetooth` variant. The
Pico W connection run recorded no drain-budget hits. The STM32 probe recorded
two bounded drain hits during controller initialization and then remained
stable with `HAL_OK` transport status.

The Stage 2 smoke gate repeated controller startup, advertising, BlueZ
connection, and GATT service resolution with the shared owner on both boards.
Pico W also recorded symmetric ACL traffic with no drain-budget hits. Both
boards were left running the `bluetooth` variant.

#### Hardware substage 1.a wiring and procedure

Begin with the Nucleo disconnected from USB and all other power. Connect the
PIM730 directly with short leads:

| PIM730 | STM32G474 | Nucleo connector |
|---|---|---|
| `CS` | `PB12` | CN10 pin 16 |
| `DAT` | `PB15` | CN10 pin 26 |
| `WL_ON` | `PB14` | CN10 pin 28 |
| `CLK` | `PB13` | CN10 pin 30 |
| `GND` | GND | CN10 pin 20 |
| `3V3` | 3.3 V | CN7 pin 16 |

Do not use 5 V. Confirm visually that the PIM730 cuttable
`BT_ON`-to-`WL_ON` trace is intact; leave `BT_ON`/`BL_ON` otherwise
unconnected. Only after the wiring and trace state are confirmed should the
STM32 Bluetooth image be programmed through the Nucleo ST-Link. Record the
periodic `JHBT1` status before testing discovery, connection, characteristic
read/write, disconnect/reconnect, and the WiFi-only regression. The Pico W
on-board-radio run follows as the second hardware profile.

### Bluetooth Classic manager hardware probe

The public `classic-scan` variant of example 29 is the generic Classic
hardware probe. It uses only `HAL_ENABLE_BLUETOOTH_CLASSIC`, assigns volatile
indexes to observed devices, and deliberately omits Bluetooth addresses and
link-key material from its log. Build, upload, and open its serial console:

```sh
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-scan
vscode/entry/jh-vscode upload \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-scan \
  --port /dev/ttyACM0
```

After the initial inquiry and serialized SDP queries, use `PAIR n`, approve a
reported request with `AUTHORIZE`, publish the validated peer with `SAVE n`,
and inspect `INFO`. `FORGET n` must return the peer count to zero. `SCAN`
followed by `STOP` verifies explicit inquiry cancellation. The console uses
RAM-only storage; persistence across reset belongs to the separate bonding
gate.

On 2026-09-03, a Pico 2 W running the `rp2350-arm` image detected an XY-BT
Audio/Video device that was not a gamepad. Its copied Class of Device was
`0x340404`, and serialized SDP resolved PnP, Serial Port, and Audio Sink
(`0x16`). Just Works authorization succeeded. Saving, forgetting, and
pairing again changed the peer count `0 -> 1 -> 0 -> 1`. A restart with an
empty manager RAM followed by another pairing also succeeded. Testing exposed
and fixed a BTstack timing edge: a second SDP query could begin before the
client became ready or could complete synchronously. The backend now retries
the busy client without blocking and treats synchronous completion as success;
two consecutive device queries passed on the same hardware.

This result closes the non-gamepad Classic-manager part of the C8.5 hardware
gate. It does not claim an audio data profile or persistent bonding.

### Bluetooth Classic raw HCI inquiry diagnostics

`tests/hardware/bluetooth_classic_hci_trace` records BTstack HCI commands and
events before the public manager parses them. The same fixture builds for Pico
W RP2040 and Pico 2 W RP2350 ARM. It also reports HCI transport counters and
the measured CYW43 gSPI clock. Bluetooth addresses are masked, unknown command
payloads and ACL bodies are hidden, and Extended Inquiry Result is retained
only through its RSSI byte; its EIR data body is redacted.

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hci_trace \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hci_trace \
  --target rp2350-arm --board pico2w
```

Use `SCAN` for one ten-second scan or `SCAN30` for three consecutive inquiry
cycles, then issue `INFO` and `DUMP`. `STOP` exercises cancellation and `RESET`
discards buffered records. This fixture uses private diagnostic interfaces and
is not part of the public HAL API.

On 2026-09-03, comparative traces showed identical successful 21-command
controller initialization on both boards: HCI/LMP 5.2, manufacturer `0x0131`,
subversion `0x2310`, and matching supported-command, feature, and buffer-size
responses. Both sent the same GIAC Inquiry command and reported no ACL traffic,
dropped trace records, or HCI transport drain-budget hits. Pico W produced no
Inquiry Result over repeated 30-second runs, whereas Pico 2 W reported XY-BT
with Class of Device `0x340404` and RSSI between approximately -85 and -94 dBm.

Three discriminating checks isolated the observation:

- halving only the Pico W gSPI clock to 15.625 MHz did not change the result;
- Pico W immediately discovered a nearby Pico 2 W Classic HID fixture at
  approximately -84 to -85 dBm, and the reverse link was approximately -86 to
  -88 dBm, proving that both Classic radios and the inquiry path work;
- an independent Pico W image using the stock Pico SDK `pico_btstack_cyw43`
  transport also completed seven inquiry cycles without detecting XY-BT.

The evidence rules out the public result parser, scan deadline, custom HCI
transport, and gSPI rate. It does not show that RP2040 is unable to discover
XY-BT in general. The supported conclusion is that this particular Pico W,
antenna geometry, and RF environment placed the already weak XY-BT inquiry
response below its reception threshold. The backend now explicitly requests
Extended Inquiry Result mode so successful scans expose EIR names and RSSI for
future diagnosis.

### Bluetooth Classic non-gamepad HID Host hardware probe

`tests/hardware/bluetooth_classic_hid_device` is a private, test-only BTstack
HID Device fixture. A Pico W advertises a standards-based Classic HID mouse
with a Generic Desktop descriptor and emits alternating relative-motion input
reports. This is not a public HID-device API. Build the RP2040 fixture and the
public `hid-host` example for the RP2350 ARM host:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hid_device \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant hid-host
```

Flash each image only to its designated board. `INFO` on the fixture must show
`controller=1`. On the host, use `SCAN`, approve the pending Just Works request
with `AUTHORIZE`, then use `INFO`. Acceptance requires
`JHC85-HID-PASS`, `descriptor=1`, `input=1`, and `saved=1`; the fixture must
show `hid=1` and a non-zero report count. Neither console prints Bluetooth
addresses or link keys.

On 2026-09-03, the Pico 2 W host copied the 50-byte mouse descriptor from a
Pico W, received repeated three-byte input reports, and published the validated
peer after explicit host-side authorization. The fixture confirmed the HID
channel and report transmission. This closes the non-gamepad HID
descriptor/report part of C8.5. Both sides use volatile link-key storage, so
the result does not claim persistence across reset.

### Bluetooth Classic HID gamepad probe

`tests/hardware/bluetooth_gamepad` owns the private pre-API Classic HID Host
probe and the sanitized `zero2_android_dinput.json` capture. The capture keeps
the 137-byte report descriptor, PnP identity, SDP metadata, all twelve input
states, the undeclared trailing input byte, and a repeated raw report. It omits
Bluetooth addresses, link keys, host identity, and USB serial numbers.

The firmware initializes the shared HCI/L2CAP runtime, a volatile one-entry
link-key database, the SDP client, one HID Host connection, and one event
handler. Inquiry starts only after the `DISCOVER` serial command and closes
after 120 seconds or after one matching device is accepted. A candidate must
have the peripheral device class, the captured name, a Classic HID service,
and the captured PnP identity. The private selector must not be combined with
public BLE or the earlier Stage 1 probe.

The private parser consumes the HID report descriptor instead of Zero 2 byte
offsets. It accepts Generic Desktop Game Pad and Joystick application
collections and normalizes up to 32 buttons, nine desktop axes, and one hat
switch into fixed-memory snapshots. The C6 limits are 256 descriptor bytes,
32 bytes per input report, and 16 queued snapshots. Unknown usages are ignored;
malformed or oversized descriptors, truncated or oversized reports, unknown
report IDs, duplicate mapped usages, and queue overflow are reported through
bounded diagnostics. Repeated reports that do not change the state do not add
another snapshot.

`test_bluetooth_gamepad_parser` uses the same sanitized capture as the hardware
probe. It covers the captured reports, descriptor-driven layouts, idempotent
input, reconnect state clearing, malformed/truncated input, unknown report IDs
and usages, duplicate usages, queue overflow, and the absence of dynamic
allocation during parser operation.

Build the required Pico 2 W image:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid
```

Upload it and run the hardware verifier on the resulting CDC port:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant classic-hid \
  --port /dev/ttyACM0

python3 tests/hardware/bluetooth_gamepad/verify_zero2.py \
  --port /dev/ttyACM0
```

When prompted, start the Zero 2 in Android D-input mode with `B+Start`, then
hold `Select` until the pairing LED flashes. The verifier authorizes the
pairing method reported by the controller, checks the captured descriptor and
all controls, runs the disconnect/reconnect and power-cycle cases, and keeps a
continuous connected interval for 30 minutes. During the first reconnect it
asks for a held control so the disconnect path can release active input.

The verifier writes `zero2_pico2w_c6_result.json`; the earlier C5 result remains
the baseline from before parser integration. The C6 report contains
target/library versions, durations, transport counters, parser diagnostics,
and pool high-water marks. It must not contain Bluetooth addresses, link-key
material, host identity, a serial port name, or a USB serial number. The
ELF/map and a symbol listing must also show `ENABLE_CLASSIC` HID Host, SDP
client, HID parser, and the memory link-key database while excluding ATT,
GATT, SM, RFCOMM, SDP server, HID Device, and audio profiles.

### Bluetooth Observer hardware probe

`tests/hardware/bluetooth_observer` validates the passive BLE Observer API on
Raspberry Pi Pico W, Pico 2 W, and STM32G474 Nucleo with PIM730/RM2. It starts
passive legacy scanning, drains the bounded report queue, parses AD structures,
and records Teltonika company data, iBeacon, and Eddystone signatures without
initiating a BLE connection.

Build and upload each board separately:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2040 --board picow

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target rp2350-arm --board pico2w

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_observer \
  --target stm32g474 --board nucleo-g474re-pim730
```

Successful output uses the `JHBL4A` prefix. Record at least one Teltonika EYE
Beacon report on each board, the total and dropped report counters, and the
ELF/map memory summary. The test remains passive: scan responses, GATT client,
connections, pairing, and bonding are outside this probe.

RP2350 RISC-V is unsupported because its CYW43 Bluetooth transport is not
enabled.

### JH BLE Stream v1 hardware gate

`tests/hardware/bluetooth_stream` validates the public BLE lifecycle and
authenticated application stream on Raspberry Pi Pico W, Pico 2 W, RP2040
Pico with RM2/PIM730, and STM32G474 Nucleo with PIM730/RM2. The firmware
advertises as `JH Stream HW`, requires a fixed test-only 256-bit secret, and
echoes authenticated payloads. `verify.py` acts as a Linux Central through
BlueZ.

#### Build matrix

Build and upload all eight target, board, and runtime combinations separately:

| Target | Board | Runtime |
|---|---|---|
| `rp2040` | `picow` | bare-metal, FreeRTOS |
| `rp2040` | `pico-rm2` | bare-metal, FreeRTOS |
| `rp2350-arm` | `pico2w` | bare-metal, FreeRTOS |
| `stm32g474` | `nucleo-g474re-pim730` | bare-metal, FreeRTOS |

The same eight tuples are declared as `example.hardwareMatrix` in the fixture
manifest and are checked by the repository artifact-layout test.

Bare-metal builds:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2040 --board pico-rm2
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target rp2350-arm --board pico2w
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730
```

Append `--variant freertos` to each build and upload command for the FreeRTOS
image. The fixture initializes BLE from the first `app_task0()` call, after the
FreeRTOS scheduler has started. Its task-0 stack is 1024 words because the
authenticated handshake and its cryptographic temporaries exceed the generic
fixture default. Use the same explicit target, board, and variant for upload.
A successful build is a software result; it does not count as a hardware pass.

#### STM32G474 PIM730 plus ILI9341 load variants

The optional `display` and `display-freertos` variants keep the same BLE Stream
protocol and host verifier while continuously updating an ILI9341 connected to
the NUCLEO-G474RE Arduino SPI header. They exercise SPI1 in parallel with the
dedicated PIM730 gSPI transport on PB12-PB15. These variants are additional
coexistence/load evidence and do not replace either of the two base STM32 gate
images declared in `example.hardwareMatrix`.

Build the separate artifacts with:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730 \
  --variant display

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --target stm32g474 --board nucleo-g474re-pim730 \
  --variant display-freertos
```

The ILI9341 uses the wiring already validated by `examples/07_display_media`:

| ILI9341 | STM32G474 | Nucleo connector |
|---|---|---|
| `SCK` | `PA5` | CN5 pin 6 (`D13`) |
| `MISO` | `PA6` | CN5 pin 5 (`D12`) |
| `MOSI` | `PA7` | CN5 pin 4 (`D11`) |
| `CS` | `PB6` | CN5 pin 3 (`D10`) |
| `DC` | `PC7` | CN5 pin 2 (`D9`) |
| `RESET` | `PA9` | CN5 pin 1 (`D8`) |

The screen reports the controller address, BLE/Stream state, MTU, RX/TX,
drop/overflow/security counters, lifecycle restarts, status and uptime. Static
and unchanged fields are redrawn only when their value changes; RX/TX and
status/uptime continue to update once per second. This keeps persistent SPI1
load without repeatedly transferring identical full text rows. A display
initialization or update failure stops normal fixture progress and is reported
as a lifecycle failure. The LCD is write-only, so visual inspection remains
the physical oracle for panel output.

RP2350 RISC-V is absent intentionally: the CYW43 Bluetooth transport is not
enabled for that target.

#### Hardware verifier

Run the verifier after uploading each image, using the address printed by the
fixture. `--target`, `--board`, and `--runtime` are required and must describe
the uploaded image:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 \
  --board picow \
  --runtime baremetal
```

The default gate performs:

- public metadata, ATT MTU, initialization, advertising, connection, and
  authenticated stream smoke, including exact service ownership and GATT
  flags plus both write-request and write-command DATA paths;
- an encrypted request for full Stream and BLE deinitialization followed by
  initialization without an MCU reset, with stable-address and generation
  checks;
- 50 consecutive disconnect, reconnect, authentication, and echo cycles;
- an authenticated stream for at least 300 seconds at a target rate of at
  least 10 messages per second, with at least 90% of that target rate plus
  sequence, duplication, and integrity checks;
- RX queue saturation with 12 encrypted frames, verification of retained and
  dropped frames, explicit overflow accounting, and a post-saturation echo;
- wrong proof, forged tag, replay, forward counter gap, and authentication
  backoff checks.

The principal workload parameters are explicit and enforce acceptance
minimums:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 \
  --board picow \
  --runtime baremetal \
  --reconnects 50 \
  --stream-seconds 300 \
  --stream-rate 10 \
  --saturation-frames 12 \
  --saturation-hold 5
```

`--reconnects` cannot be lower than 50, `--stream-seconds` cannot be lower than
300, and `--stream-rate` cannot be lower than 10. A gate also fails if the
observed authenticated-message rate is below 90% of `--stream-rate`. Increase
the stream duration for an overnight soak. Use runtime `baremetal` for the
base image and `freertos` for the `freertos` manifest variant. The verifier
explicitly selects LE discovery, so a stale BlueZ alias does not affect
address-based selection. It requires the system Python packages for D-Bus and
GLib plus the `cryptography` package.

#### Recorded hardware results - 2026-08-25

| Target and runtime | Reconnects | Sustained authenticated stream | Result |
|---|---:|---:|---|
| RP2040 Pico W, bare-metal | 50/50 | 2773 messages / 300.1 s (9.24 Hz) | PASS |
| RP2040 Pico W, FreeRTOS | 50/50 | 3000 messages / 300.0 s (10.00 Hz) | PASS after requesting a 15 ms connection interval |
| RP2350 ARM Pico 2 W, bare-metal | 50/50 | 3000 messages / 300.0 s (10.00 Hz) | PASS |
| RP2350 ARM Pico 2 W, FreeRTOS | 50/50 | 3000 messages / 300.0 s (10.00 Hz) | PASS |
| RP2040 Pico + RM2/PIM730, both runtimes | - | - | physical gate pending |
| STM32G474 + RM2/PIM730, both runtimes | - | - | extended physical gate pending |
| STM32G474 + RM2/PIM730 + ILI9341, bare-metal | 50/50 | 2910 messages / 300.1 s (9.70 Hz) | HOST PASS, including IWDG reset |
| STM32G474 + RM2/PIM730 + ILI9341, FreeRTOS | 50/50 | 2930 messages / 300.0 s (9.77 Hz) | HOST PASS, including IWDG reset |

The STM32G474 display-load runs on 2026-08-25 used the `display` and
`display-freertos` variants with the PIM730 on PB12-PB15 and the ILI9341 on
SPI1. Both passed lifecycle restart, an unattended IWDG reset, 50 authenticated
reconnects, the five-minute stream, saturation with four retained and eight
dropped frames plus one reported overflow, and all negative security/recovery
cases with 62 unique handshakes. The IWDG step changed reset reason `2 -> 4`
and boot identifier
`7cb8c0cf4bc622a8 -> 5e78009fc6d2fb2b` in bare-metal and
`927f820a16299332 -> 5f2cd64b89ba00fc` in FreeRTOS while retaining address
`28:CD:C1:19:18:19`. Mean/max sustained-stream latency was 103.1/331.1 ms for
bare-metal and 102.4/313.1 ms for FreeRTOS.

Earlier pre-watchdog reruns reached exactly 3000 messages in 300.0 seconds in
both runtimes. An initial bare-metal run that redrew all nine LCD rows every
second reached only 2660 messages (8.87 Hz) and correctly failed the 9.00 Hz
acceptance threshold. Retaining unchanged rows removed that avoidable load
while RX/TX and uptime continued to refresh once per second.

After the per-boot oracle was added, a complete Pico 2 W bare-metal rerun again
passed 50/50 reconnects and 3000 messages in 300.0 s (10.00 Hz), plus
saturation and all security cases. Its watchdog step changed reset reason
`3 -> 4` and boot identifier
`cb3ef2a0b00439b4 -> bc75beed8bfd5cf1` while retaining address
`2C:CF:67:BB:40:2E`.

The recorded passing runs completed the public lifecycle restart, retained the
local address across the reset test, changed reset reason from `3` to watchdog
reason `4`, and authenticated a fresh session. The current oracle accepts any
pre-reset reason, but requires watchdog reason `4` afterwards and a nonzero
random per-boot identifier to change. Consequently, an old watchdog reason
cannot make a mere BLE restart look like an MCU reset. Each run also retained
four of 12 saturation frames while reporting eight drops and one overflow
acknowledgement, and passed forged-tag, replay, counter-gap, wrong-proof,
backoff, and fresh-session recovery checks. The watchdog command provides an
unattended MCU-reset interruption test; it does not remove VBUS physically.

The earlier Pico W FreeRTOS image used a 512-word task stack and reset during
the first authenticated handshake. Increasing the fixture stack to 1024 words
removed that reset.

But a subsequent run lost the BLE link during reconnects and another sustained only
8.06 Hz. The Stream backend had left the connection
interval entirely to the central, making the sequential authenticated
request/notification round trip too slow on RP2040 FreeRTOS. After the
peripheral began requesting a 15 ms interval with zero peripheral latency, the
complete rerun passed: watchdog recovery, 50/50 reconnects, 3000 messages in
300.0 seconds (10.00 Hz, 60.2/153.5 ms mean/max latency), saturation, and all
negative security/recovery cases with 62 unique handshakes.

The current host oracle is Linux/BlueZ. Native Windows execution is deferred,
as is downstream consumer/lights-timer integration; neither is a requirement
for the results recorded above.

#### Fixture command and identity rules

Identity, restart, saturation, and stats controls are fixture-only commands
carried inside mutually authenticated and encrypted Stream DATA payloads. Each
command is one complete DATA frame written with a BlueZ `WriteValue` request
to the existing RX characteristic
`b7ce0002-3c13-4fe2-801f-d71bdab1369b`. Its response is one encrypted DATA
notification from the existing TX characteristic
`b7ce0003-3c13-4fe2-801f-d71bdab1369b`. Commands are not split across GATT
writes. They add no characteristic and do not change the JH BLE Stream v1 wire
protocol.

The verifier additionally sends an ordinary authenticated echo through BlueZ
`type=command` to exercise the RX `write-without-response` path; fixture control
commands use `type=request` so their request completion is observable.

| Authenticated command payload | Fixture response or effect |
|---|---|
| `JHBL5/IDENTITY` | `J5I1\|<target>\|<board>\|<runtime>` |
| `JHBL5/RESTART` | `JHBL5/RESTARTING`, then full Stream and BLE restart |
| `JHBL5/BOOT` | `J5B1` followed by one reset-reason byte and a little-endian random 64-bit boot identifier |
| `JHBL5/POWER-LOSS` (RP and STM32G474) | `JHBL5/POWER-LOSS-ARMED`, then a watchdog reset without host or user intervention |
| `JHBL5/SATURATE` + little-endian hold time | `JHBL5/SATURATE-READY`, then bounded RX pause |
| `JHBL5/STATS` | compact `J5S1` binary recovery oracle |

The identity response is compiled directly from `HAL_TARGET_NAME`,
`HAL_BOARD_PROFILE_NAME`, and `HAL_ENABLE_FREERTOS`; runtime is exactly
`baremetal` or `freertos`. The verifier compares it with all three required CLI
values before accepting workload results. For example, the Pico W bare-metal
response is `J5I1|rp2040|picow|baremetal`.

A passing physical run ends with `JHBL5 HOST PASS`. The device log uses the
`JHBL5` prefix and records negotiated MTU, counters, authentication failures,
replay rejections, lifecycle failures, restarts, and bounded queue loss.

The final security phase intentionally enters the authentication backoff
window, sends rejected HELLO probes at least once per second throughout the
configured 30-second window, and proves a fresh authenticated recovery only
after its deadline before printing `JHBL5 HOST PASS`. A passing run therefore
leaves the fixture ready for another gate without a reload.

The embedded secret and its copy in `verify.py` are public test material. They
must never be reused by a product. A product needs a unique random per-device
secret delivered out of band and stored through its provisioning flow.

### SX1262 raw LoRa hardware gate

`tests/hardware/lora_sx1262` uses the buildable
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) firmware and
two radios in the same band. It validates initialization, bidirectional
over-the-air packets, sequence continuity, DIO1-driven asynchronous callbacks,
IRQ/cancel diagnostics, RSSI/SNR metadata, sleep/wake and radio destroy/create
reinitialization.

On profiles with a GPIO status LED, a solid LED indicates transmit activity
and a 120 ms pulse confirms a received packet.

Do not pair an LF device with an HF device. Confirm the labels on both radios
and antennas, connect the correct antenna before power-up, use 3.3 V I/O and
comply with local spectrum, power and duty-cycle rules.

#### LF pair: two RP2040-LoRa-LF boards

Build and upload the initiator to the first board, then build and upload the
responder to the second board:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf \
  --port /dev/serial/by-id/<lf-initiator>

vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder \
  --port /dev/serial/by-id/<lf-responder>
```

The firmware deliberately uses 434.0 MHz for this fixture; this is a test
configuration, not a universal regulatory preset.

#### HF pair: external Core1262-HF on RP2040 and STM32G474

Use the fixed wiring documented by the composite profiles. Build the
RP2040/Pico as initiator and the NUCLEO-G474RE as responder (or reverse both
roles):

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf --variant responder
```

Both Core1262-HF devices use the same module electrical profile and EU868
technical configuration. The generated board facts own their host pin maps;
the example contains no target-specific fixture wiring. The Nucleo profile
uses SPI2 on PB13/PB14/PB15 and retains LD2/`HAL_LED_BUILTIN` on PA5.

Before an OTA run, the no-transmit probe may be built and uploaded on either
host with `--variant probe`. A pass verifies provider capabilities, explicit
calibration, current RSSI, CAD and standby without enabling the RF transmit
path.

#### Verification

Run long enough to observe the automatic lifecycle probes at sequence 10 and
20:

```bash
python3 tests/hardware/lora_sx1262/verify_pair.py \
  --initiator-port /dev/serial/by-id/<initiator> \
  --responder-port /dev/serial/by-id/<responder> \
  --duration 75
```

A pass requires at least five matched ping/pong sequences, packet metadata,
the asynchronous event-loop marker on both radios, non-zero IRQ/callback/cancel
counters, `HAL_OK` sleep/wake and `HAL_OK` reinitialization. Then swap which
physical device receives the `responder` build and repeat.

Repeat the gate for the two deterministic test combinations. The base and
`responder` variants use SF9/10 dBm; `sf7` and `responder-sf7` use SF7/6 dBm.
Do not assume that SF12/14 dBm is permitted. Both ends of a run must use the
matching variant family. Record module/antenna labels, exact wiring, firmware
revision, distance, packet counts, loss, RSSI/SNR range and the verifier JSON
in the private hardware report.

### SX1262 command-router over LoRa hardware gate

The `link` and `link-responder` variants of
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) attach
`hal_lora_commands` to one reliable link. The initiator sends a correlated
500-byte binary `echo` request; the responder dispatches it through the shared
router and returns the exact payload. Both encoded directions require three
plaintext link fragments with the default bounds.

The handler policy permits both `LORA_LINK` and `BLE_STREAM`. This gate supplies
only the LoRa adapter. The BLE source entry demonstrates that the route and
wire API are reusable by a later BLE adapter; it does not claim that such an
adapter is implemented.

For two integrated LF boards, build and upload opposite roles. When both
boards are already in BOOTSEL and therefore have no serial port, select each
drive explicitly:

```bash
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant link \
  --bootsel-volume /dev/<initiator-partition>

vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant link-responder \
  --bootsel-volume /dev/<responder-partition>
```

After both CDC devices enumerate, use stable `/dev/serial/by-id/` paths and
capture at least three complete transactions:

```bash
python3 tests/hardware/lora_sx1262/verify_commands.py \
  --initiator-port /dev/serial/by-id/<command-initiator> \
  --responder-port /dev/serial/by-id/<command-responder> \
  --duration 75 --minimum-transactions 3
```

A pass requires the expected role marker from both devices and no `JHCMD1`
error or timeout. For at least three nonzero request identifiers, the
initiator request, responder handler and initiator response must agree on the
500-byte length, CRC-32 and three-fragment count. The handler peer and response
source must be `0x1001` and `0x1002`, respectively; both session identifiers
must match their role's nonzero `READY` value. The plaintext security flags
must be zero, RSSI must be in the negative LoRa range, SNR must be bounded, the
handler source must be `LORA_LINK`, handler calls must strictly increase, and
the final status and byte comparison must both succeed. Saved logs can be
checked with `--initiator-log` and `--responder-log` instead of live serial
ports.

Swap the two physical devices' roles and repeat. Record their module and
antenna labels, firmware revision, distance, matched request identifiers,
RSSI/SNR range and verifier JSON only in the private hardware report. The
434.0 MHz fixture settings are technical test values; connect the LF antennas
and follow the local spectrum, power and duty-cycle requirements.

### ESP32-S3 Phase 1 hardware probe

`tests/hardware/esp32s3_phase1` closes the target, board, build, flash, and
monitor plumbing for the Waveshare ESP32-S3-Zero SKU 25081. It intentionally
does not exercise the GPIO, serial, bus, networking, or storage HAL APIs
assigned to later phases.

The firmware reports the exact generated target and board identity, then checks
the detected chip model, core count, physical flash size, PSRAM initialization,
and physical PSRAM size against the board registry. `verify_phase1.py` obtains
its expectations from the same target and board descriptors and waits for the
repeated report on the native USB Serial/JTAG port.

Use the stable `/dev/serial/by-id/` path when available. Build and validate the
artifacts through the production ESP-IDF runner:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1 --clean
python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1
```

The same project is tracked as a `jh-vscode` ESP-IDF project. Set `PORT` to the
stable alias of the connected board, then build, refresh IntelliSense, upload,
and monitor through the public workflow:

```bash
PORT="/dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>"
vscode/entry/jh-vscode config-dump \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode build \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode refresh-intellisense \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode upload \
  --project tests/hardware/esp32s3_phase1 --port "$PORT"
vscode/entry/jh-vscode monitor \
  --project tests/hardware/esp32s3_phase1 --port "$PORT" \
  --lock-policy replace-own
```

The selected device must match the board profile's USB Serial/JTAG VID/PID
`303a:1001`. To test upload handoff, leave the monitor running and invoke the
same `upload` command from a second terminal. The upload must release only this
project's monitor, flash the complete three-image manifest, reset the board,
and allow the monitor to reconnect.

Stop the monitor before running the standalone verifier because both commands
take exclusive ownership of the serial port:

```bash
python3 tests/hardware/esp32s3_phase1/verify_phase1.py \
  --port "$PORT"
```

A successful run prints one JSON object with `"phase": "task0"`, a sequence
of at least one, and `"status": "PASS"`.

#### Verified Phase 1 baseline

The physical closure run completed with a clean 555-step ESP-IDF build. The
application image was 150544 bytes with 86% of its partition free. Three
complete uploads each flashed the bootloader, partition table, and application
image. The runtime report matched an ESP32-S3 with two cores, 4194304 bytes of
physical flash, and initialized 2097152-byte Quad PSRAM. The persistent ESP
monitor also released the port for upload, reconnected after reset, and resumed
the repeated `app_task0()` heartbeat.

This result validates the Phase 1 target/board/build/flash/monitor workflow for
the Waveshare ESP32-S3-Zero SKU 25081. It does not extend support to the GPIO,
serial, bus, networking, storage, or optional second-task surfaces assigned to
Phase 2.

### ESP32-S3 Phase 2 hardware probe

`tests/hardware/esp32s3_phase2` validates the Phase 2 peripheral HAL on the
`waveshare-esp32-s3-zero` profile. It needs only the board's native USB cable;
no external sensor, jumper, or SPI/I2C device is required.

The firmware checks:

- system time, architecture, UID, heap, die temperature, watchdog, retained-
  fault boundary, and the enabled FreeRTOS stack-guard behavior;
- FreeRTOS mutexes, critical sections, and `app_task0`/`app_task1` affinity on
  cores 0/1;
- GPIO input with pull-up, output/readback, and a same-owner reconfigured GPIO
  interrupt;
- 12-bit ADC readings driven apart by the GPIO's internal pull-down/pull-up;
- hardware UART1 TX/RX through one GPIO-matrix loopback pin;
- I2C master bus clear, initialization, and a complete address scan (zero
  discovered devices is valid for an unwired board);
- SPI2 master transactions, blocking DMA, and the synchronous async-DMA
  fallback without assuming received data from an absent slave;
- managed dedicated-pool GPTimer pause/resume, repeated ISR callbacks, and
  teardown;
- bidirectional debug traffic over the startup console's native USB
  Serial/JTAG VFS.

Build and materialize the relocatable artifact manifest:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --clean

python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware
```

Use the stable `/dev/serial/by-id/...` alias of the board on Linux (or its COM
port on Windows) for both flash and verification:

```bash
python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_phase2/verify_phase2.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

The verifier sends `PING` and accepts only a complete `status=PASS` report. A
missing callback, wrong core, stalled RX path, out-of-range ADC result, or
failed peripheral status therefore cannot be reported as a successful smoke
test.

The fixture intentionally does not call `hal_enter_bootloader()`: successful
download-mode entry resets the MCU and requires a separate reconnect/recovery
test. Its symbol is compile/link covered by the Phase 3 fixture, while the
reset transition belongs to the Phase 3.5 hardware campaign.

#### Recorded Phase 2 status

An earlier revision completed physical closure on the Waveshare
ESP32-S3-Zero: task0/task1 affinity was cores 0/1, two GPIO callbacks ran in
ISR context, ADC pull-down/pull-up readings were 37/4095, UART GPIO loopback and
SPI passed, an unwired I2C scan returned `HAL_OK` with zero devices, 20
default-pool GPTimer callbacks ran in ISR context, and bidirectional USB
Serial/JTAG plus system/synchronization checks passed.

That historical result covers the original fixture subset only. The current
fixture now requires a dedicated timer pool and the implemented stack guard,
but those checks have not been rerun on hardware. I2C target, PWM/PWM_FREQ,
RMT/RGB, PCNT, download-boot entry, destructive stack/fault injection, and
retained-fault recovery also remain in the Phase 3.5 hardware campaign.

## Firmware compile/link fixtures

### ESP32-S3 compile/link fixture

| Fixture | Coverage |
|---|---|
| `tests/fixtures/esp32s3_phase3` | Compile-only ESP-IDF project selecting every ESP32-S3 backend delivered through Phase 3. It checks feature/source/dependency resolution, compilation, linking, `two-ota-large` partition generation, and artifact publication. |

CI and local Gate 8 build this fixture. A passing build does not establish
runtime WiFi/socket/TLS/service/OTA/WireGuard behavior or the newly completed
Phase 2 peripheral behavior; those require a separate hardware, lifecycle, and
negative-security verification campaign.

---

## Host test architecture

### How it works

The CMake build at the project root compiles a static library `hal_mock` from:

- all `src/hal/impl/.mock/*.cpp` stubs,
- the backend-neutral HAL sources in `UTIL_SOURCES` (see `CMakeLists.txt`),
  including remaining shared MQTT/WireGuard status adapters in
  `hal_network_status.cpp`, HAL facades, compatibility layers, portable
  device drivers and bundled frameworks,
- `src/utils/unity.c` (Unity integration wrapper).

The exact list is the `UTIL_SOURCES` set in `CMakeLists.txt` - treat that as the
source of truth.

Each test executable in `tests/` links against `hal_mock` only, with no
headers, no pico SDK, no hardware.

The managed Unity 2.5.4 framework lives in `third_party/Unity/src`. The tracked
JaszczurHAL integration consists of:

- `src/utils/unity.c`
- `src/utils/unity.h`
- `src/utils/unity_internals.h`
- `src/utils/unity_config.h`

The host CMake build compiles the `src/utils/unity.c` wrapper into `hal_mock`
and enables `HAL_ENABLE_UNITY` plus `UNITY_INCLUDE_CONFIG_H`. Test sources
include `"utils/unity.h"` and use the repository-local `unity_config.h`. Run
`scripts/ensure_unity.sh` or the central component updater to reconstruct the
pinned checkout. Outside the test/support build, Unity is inactive unless
`HAL_ENABLE_UNITY` is explicitly enabled.

The thematic utility modules and their compatibility wrappers are covered by
`test_tools` using HAL mocks.
`multicoreWatchdog.cpp` is covered by `test_multicoreWatchdog` using a local
logger-close stub plus HAL mocks.
`utils/draw7Segment.cpp` has no platform dependencies
(pure `const char*` + `hal_display`).

### Unity examples

Minimal test file:

```cpp
#include "utils/unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_adds_numbers(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_adds_numbers);
    return UNITY_END();
}
```

Test using HAL mocks:

```cpp
#include "utils/unity.h"
#include "hal/system/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_delay_ms_updates_mock_time(void) {
    hal_delay_ms(10);

    TEST_ASSERT_EQUAL_UINT32(10, hal_millis());
    TEST_ASSERT_EQUAL_UINT32(10000, hal_micros());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_delay_ms_updates_mock_time);
    return UNITY_END();
}
```

Simple CMake registration:

```cmake
add_hal_test(test_my_module)
```

This expects `tests/test_my_module.cpp` and links it with `hal_mock`.

When a test needs additional implementation files, create a dedicated target:

```cmake
add_executable(test_my_driver
    test_my_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/sensors/my_driver/my_driver.cpp
)
target_link_libraries(test_my_driver PRIVATE hal_mock)
add_test(NAME test_my_driver COMMAND test_my_driver)
```

Run only the new suite:

```bash
cmake --build .build/host --target test_my_module
ctest --test-dir .build/host -R test_my_module --output-on-failure
```

### Test suite guide

`tests/CMakeLists.txt` is the authoritative test inventory. Inspect the suite
registered by the current checkout with:

```bash
ctest --test-dir .build/host -N
```

The table below is a coverage guide for representative and grouped suites; it
is intentionally not a second exhaustive test registry.

| Suite | What it covers |
|---|---|
| `test_hal_gpio` | pin modes, read/write, level injection, interrupt attach/detach |
| `test_hal_adc` | resolution config, inject + read |
| `test_hal_pwm` | resolution config, write |
| `test_hal_timer` | low-level alarm add/cancel paths, `_ex` diagnostics, managed timer start/stop/pause/resume/period/remaining behavior |
| `test_stm32_hal_timer` | real STM32G474 timer backend under host simulation: one-shot alarms, callback reschedule, cancel, pool limits/destruction, long-delay chunking, managed stop/pause/resume |
| `test_hal_ds18b20` | non-blocking request/poll/take_latest flow, busy-state behavior, CRC/presence handling |
| `test_hal_dht` | DHT GPIO transaction timing, checksum handling, cached sample getters and critical-section restoration |
| `test_hal_onewire` | reset/read/write/select/search wrappers, CRC8/CRC16 helpers and mock bus locking |
| `test_hal_rtc` | RTC init/get/set datetime, internal/external clock-source reporting, non-I2C provider dispatch, full Gregorian validation, 1970/2000/2099 epoch boundaries and overflow, integrity flag, interrupt mask, read-clear event flags, relative wake-up one-shot/state behavior, CLKOUT/timer/alarm configuration, legacy invalid-input guards and `_ex` status mapping |
| `test_jh_rtc_i2c_provider` | Shared PCF8563/DS3231 provider selection, metadata, datetime/event translation, safe DS3231 CLKOUT control, I2C error propagation, and backend capability status mapping over mock HAL I2C |
| `test_stm32_rtc_codec` | STM32G474 RTC TR/DR and Alarm A BCD register encoding, calendar/range rejection, day-versus-weekday constraints, LSE/LSI 1 Hz prescalers, and wake-up counter rounding/bounds |
| `test_rtc_architecture` | Single RTC facade ownership, I2C/internal provider boundaries, shared validation/locking, HAL-only chip drivers, STM32G474 WUT and RP AON alarm dispatch, and source-manifest wiring |
| `test_hal_power` / `test_hal_power_header_c` | Power capabilities, request validation, callback ordering, RTC wake, monotonic elapsed time, reset-style mock behavior, cleanup, and C header compatibility |
| `test_power_architecture` | Separate RTC/power ownership, target backend presence, STM32 STOP/Standby mappings, RP AON integration, generated feature dependency, and example wiring |
| `test_jh_calendar` | Gregorian leap-year/month-length/day-of-week validation, impossible dates, Unix epoch zero, leap-day round-trip, RTC upper boundary and 64-bit overflow statuses |
| `test_calendar_architecture` | Shared calendar source ownership, HAL-only legacy time wrappers, and rejection of target/driver-local calendar algorithms or `hal_time_from_components()` copies |
| `test_hal_eeprom` | byte/int write-read, `commit` flag |
| `test_hal_serial` | Serial wire/message boundaries, binary RX inject + `available`/`read`, task/ISR debug prefixes, accepted/rejected timestamps, rate-limit configuration/lifecycle/source isolation, streamed formatting beyond `HAL_DEBUG_BUF_SIZE`, ISR-deferred ring/drop summaries, mute and flush semantics |
| `test_hal_serial_session` | Framed HELLO/AUTH lifecycle, deterministic and consecutive random challenges, entropy fail-closed behavior, challenge cleanup, command compatibility, unknown-handler dispatch, seq echo, malformed-frame drops and null-arg safety |
| `test_hal_serial_commands`, `test_hal_serial_commands_header_c`, `test_hal_serial_commands_header_cpp` | Active-session gating and selected pre-HELLO routing, SC name/argument parsing, sequence/session/auth metadata, verbatim text responses, legacy formatting, prefix fallback, payload bounds, callback ownership, reentrant lifecycle safety and standalone C/C++ headers |
| `test_hal_sc_auth` | Stable per-device key/response vectors, invalid-input output clearing and shared constant-time MAC comparison |
| `test_jh_security_primitives` | Secure zeroization, constant-time equality/mismatch behavior, deterministic mock entropy vector and failure output clearing |
| `test_security_architecture` | Compiled Serial Session/auth ownership, one shared entropy/zeroize/constant-time implementation, BLE adoption and source-manifest wiring |
| `test_serial_architecture` | One shared serial/debug core, three complete link-time transport ports, target-core duplication rejection and source-manifest wiring |
| `test_hal_swserial` | software UART status success/failure paths, pool exhaustion, RX inject, TX capture, frame format and pin reassignment |
| `test_rp2040_swserial_backend` | RP2040 source-selection guard: Pico SDK PIO programs required; wrapper serial implementations, GPIO RX callbacks, microsecond bit delays and HAL critical sections forbidden |
| `test_hal_uart` | hardware UART RX inject, TX capture, pin reassignment |
| `test_hal_spi` | SPI init/reinit, reset, per-bus locks, transfers, status validation and DMA failure mapping |
| `test_hal_lora_radio_lifecycle` | Opaque-handle allocation limits, stale handles, lifecycle cleanup and provider error propagation |
| `test_hal_lora_radio` | SX1262 profiles and presets, SX1261 model limits, blocking TX, bounded/continuous polling RX, overflow/CRC/timeout diagnostics, power state, time-on-air and two connected mock radios |
| `test_hal_lora_link` | Link defaults/lifecycle, stale handles, addressed plaintext and AEAD fragmentation, ACK loss/retransmission, bounded timeout including the maximum retry count, duplicate delivery suppression, whole-message integrity, out-of-order/incomplete reassembly, corrupt packets during ACK wait, later-fragment start recovery and concurrent send serialization over connected mock radios |
| `test_lora_link_frame` | Strict versioned-frame shapes, plaintext capacity/round-trip, authenticated encryption, header/ciphertext tamper rejection, ACK encoding, truncation and output bounds |
| `test_hal_lora_link_header_c`, `test_hal_lora_link_header_cpp` | Standalone public link-header compatibility in C11 and C++17 |
| `test_lora_link_plain_compile` | Strict warning-as-error compilation of the link and frame codec without the optional crypto feature |
| `test_sx126x_adapter` | Official driver command orchestration, SX1261/SX1262 PA and OCP selection, SPI transaction cleanup, BUSY deadlines, RF switch levels, electrical setup, band calibration, TX timeout and RX CRC IRQ mapping |
| `test_hal_lora_sx127x` | SX1276/SX1278 model-specific descriptor validation and the common facade lifecycle, capabilities, calibration boundary, TX, RX, CAD and power states |
| `test_sx127x_adapter` | SX127x register transport, version probe, modem/frequency/PA configuration, IRQ/status mapping, FIFO metadata, RSSI, CAD, timeout, cancellation, bus errors and TCXO sleep/wake behavior |
| `test_hal_pga2311` | PGA2311 status/config validation, pool exhaustion, injected SPI failures and retry, frame writes, dB/code conversion, soft/hardware mute behavior |
| `test_irsmall_decoder_driver` | IRsmallDecoder NEC/NECx/SIRC/Samsung frame decode, RC5 transition-table decode including extended command bit, repeat/held reporting, timeout reset and interrupt disable/enable paths |
| `test_hal_i2c` | bus0/bus1 transfer and status paths, direct read helpers, locking, init/deinit, bus clear, bounded scan results, count-only/overflow behavior and per-address callback coverage |
| `test_hal_rgb_led` | status-first init/init_ex, invalid config, allocation/transport failure, retry, brightness clamp, off and pre-init guard |
| `test_hal_display` | status-first display API, capabilities/raw-write rules, text sizing/formatting, presets, drawing, SSD1306 init, streaming/async DMA state, validation and injected backend-I/O failures |
| `test_hal_can` | send/receive, ring buffer, null-data guard, payload clamp, backend selection, classic-vs-FD frame validation, filter API, `hal_can_process_all`, `hal_can_create_with_retry`, `hal_can_encode_temp_i8` |
| `test_hal_thermocouple` | MCP9600 + MAX6675 inject, unsupported-op NAN returns, ADC resolution, enable/disable, alert/status |
| `test_max6675_driver` | Shared MAX6675 raw decode, open-circuit fault, GPIO pin setup and bit-bang read sequence |
| `test_mcp9600_driver` | Shared MCP9600/MCP9601 device ID handling, register transactions, fixed-point decoding, ADC sign extension, config bit preservation, alert/status and legacy ambient-resolution mapping |
| `test_bh1750_driver` | Shared BH1750 init command, first-measurement delay, I2C bus routing and two-byte lux decode |
| `test_adp5360_driver` | Shared ADP5360 device-ID validation, charger/fuel-gauge/regulator register flows, status conversion, I2C failures and instance-mutex coverage |
| `test_simple_io_drivers` | Shared MCP23017/PCA9654E/PCF8574/74HC595/MCP3221/MCP4725 init sequences, per-pin/full-port write and read paths, invert/pull-up/IRQ configuration and instance-mutex coverage |
| `test_hd44780_driver` | Shared HD44780 GPIO init, 4-bit/8-bit command framing, cursor row offsets, CGRAM writes, print/write path and instance-mutex coverage |
| `test_hal_dma_pwm_audio` | Mock DMA PWM-audio lifecycle, callback dispatch, pause/resume and interpolation coverage |
| `test_dacless_driver` | Shared DACless config normalization, DMA and polling sample/block callback refill flow, ADC buffer, mute/unmute, interpolation helpers and mutex coverage |
| `test_tsc2007_driver` | Shared TSC2007 command-byte layout, 12-bit reply decode, touch-read sequence, stability rejection, bus routing and instance-mutex coverage |
| `test_stmpe610_driver` | Shared STMPE610 setup sequence, chip-ID probing, I2C/SPI/register transactions, FIFO decode, soft-SPI bit-bang path and instance-mutex coverage |
| `test_ads1x15_driver` | Shared ADS1X15 register config, ADS1115/ADS1015 conversion reads, gain/mode/data-rate mapping, comparator threshold writes and I2C clock forwarding |
| `test_hal_external_adc` | ADS1115 range setup, per-channel raw/scaled reads, out-of-range safety |
| `test_hal_gps` | shared public NMEA encode/getter path, location/speed/date/time and extended-fix injection, valid/updated/age flags, reset and diagnostics |
| `test_gps_architecture` | Single GPS transport facade ownership, deleted target copies, shared getter/engine ownership, mock-injection boundary and source-manifest wiring |
| `test_hal_system` | delay/millis/micros behavior, wrap-safe non-blocking `hal_millis_interval_*` helpers (elapsed + callback variants), watchdog flags, heap/chip-temp helpers, type-independent `hal_constrain`/`hal_map` (incl. equal-range guard), `COUNTOF`, `hal_u32_to_bytes_be`, `NONULL` |
| `test_hal_bits` | bit helper macros (`is_set`, `set_bit`, `clr_bit`, `bitSet`, `bitClear`, `bitRead`, `set_bit_v`, `clr_bit_v`) |
| `test_hal_wifi` | mode/hostname/RSSI/ping, IP/DNS/MAC inject, input validation |
| `test_hal_net` | shared endpoint/status shape, network limits, IPv4 literal/localhost/mock-DNS resolver behavior |
| `test_hal_littlefs` | Shared-facade mount/unmount flow, idempotent mount, mounted-state clearing after unmount failure, concurrent lifecycle/stat serialization, size stats and output initialization, path exists/remove helpers, exact provider-status propagation, destructive-format success and best-effort remount outcomes, progress callback configuration and input validation |
| `test_jh_littlefs_lfs_provider` | Real pinned littlefs lifecycle over a flash-like RAM backend, format/mount/file/stat/remove/size behavior, checked block range/alignment/overflow guards, flash programming rules and injected raw-I/O failure/recovery |
| `test_littlefs_architecture` | Single ownership of public LittleFS behavior and library lifecycle, target-only geometry/block operations, mock separation from facade state, shared STM32G474 flash serialization and source-inventory wiring |
| `test_hal_sdlogger` | EEPROM-backed file numbering, buffered log flush/close, crash-report formatting, SD/open failure paths |
| `test_hal_udp` | begin/parse/read flow, handle-based multi-socket bind/RX/TX separation, chunked datagram reads, remote endpoint capture/reset-on-stop, beginPacket explicit/remote sender paths, write/endPacket behavior, input validation |
| `test_hal_tcp` | TCP client connect/send/recv/shutdown/close, listener bind/listen/accept, backlog/pool limits, readiness probes and accepted-socket independence |
| `test_hal_http_server` | HTTP route dispatch, query/body/header parsing, exact/prefix routes, response headers/body, HEAD handling, handler failures and invalid configuration |
| `test_hal_http_files` | Callback-backed HTTP file serving, MIME mapping, ETag/`If-None-Match`, raw PUT, multipart upload and path traversal rejection |
| `test_hal_websocket` | HTTP Upgrade handshake, `Sec-WebSocket-Accept`, masked text frames, broadcast, ping/pong, close callbacks and invalid handshakes |
| `test_hal_net_console` | Password-required TCP console start/auth flow, serial/debug mirroring to authenticated clients, multi-client broadcast, bidirectional command input, per-client replies and disconnect callbacks |
| `test_hal_net_commands` | JSON/text command registration and dispatch, HTTP route integration, WebSocket message integration, structured errors and API validation |
| `test_hal_notify` | Notification facade validation, fake-backend dispatch, generation-checked handle lifetime, Telegram request JSON, public-host HTTP rejection and rate-limit mapping |
| `test_bsd_sockets` | BSD/POSIX adapter fd mapping, sockaddr translation, errno/EAI paths, TCP/UDP flow, nonblocking mode, `select()`, `getaddrinfo()` and `setsockopt()` |
| `test_bsd_socket_headers_c` | portable C declarations, constants, and structures for BSD socket headers; runs under GNU-like hosts and MSVC |
| `test_hal_tls` / `test_bearssl_provider` | public TLS lifecycle, native HAL TCP transport, bounded BearSSL progression and optional TLS-over-BSD callbacks |
| TLS/BSD compile probes | prove that TLS builds without BSD, BSD builds without TLS, and each flag propagates only its required network modules |
| `test_bsd_sockets_c_compile` | C compile/link smoke test for socket headers, `netdb.h`, TCP/UDP client/server shapes, `fcntl()`, `select()`, `getaddrinfo()` and `setsockopt()` |
| `test_hal_wireguard` | IPv4 parser validation, byte-array and text WireGuard begin/begin_advanced/kick paths, peer-up endpoint reporting (`hal_wireguard_peer_up` + `hal_wireguard_peer_up_quick`), handshake kick trigger, input validation |
| `test_hal_mqtt` | server/connect flow, publish/subscribe/unsubscribe capture, callback dispatch from `hal_mqtt_loop`, invalid input guards |
| `test_hal_network_status` | Cross-module WiFi/DNS, TCP/UDP, MQTT and WireGuard status API validation, output initialization, pool exhaustion, state and failure mapping |
| `test_hal_ota` | OTA config setters, begin/is_started flow, boot status/confirmation, callback dispatch from injected start/progress/error/end events, callback replace/unregister flow, re-begin queue-clear behavior, invalid input guards |
| `test_ota_protocol` | Strict invitation/AUTH2 grammar, numeric and hexadecimal normalization, exact UDP endpoint identity, transcript field binding, constant-shape tag comparison and the shared host/device HMAC-SHA256 vector |
| `test_ota_image` | Versioned OTA manifest and redundant boot-state encoding, CRC/HMAC validation, corruption handling, sequence wraparound and newest-record selection |
| `test_ota_swap_engine` | Resumable program/staging sector swap across every simulated pre/post-mutation failure boundary, reverse swap rollback and corrupt phase rejection |
| `test_rp_ota_artifacts` | Native RP OTA packaging helper, including RP2040-E14 sector padding, real-page preservation, UF2 renumbering and overlap rejection |
| `test_hal_time` | shared setter/status, 64-bit monotonic progression across 32-bit wrap, RTC restore and NTP persistence, NTP success/failure state, timezone/local formatting, component conversion, CET/CEST, ranges, and minute extraction |
| `test_hal_kv` | u32/blob CRUD, delete, unchanged-skip, GC, concurrent updates, direct EEPROM-status propagation, uninitialised/range/capacity errors and output initialization |
| `test_hal_crypto` | Base64/MD5/one-shot and incremental SHA-256/HMAC-SHA256/ChaCha20/ChaCha20-Poly1305 helper behavior, input validation, and ChaCha20 counter-wrap rejection regression checks |
| `test_wireguard_crypto_shared` | shared WireGuard crypto primitives (`crypto_equal/zero`, BLAKE2s, X25519, ChaCha20, ChaCha20-Poly1305 including RFC8439 IETF detached AEAD vectors) |
| `test_hal_soft_timer` | C wrapper coverage: create/begin/tick/abort/restart, table setup/tick helpers, delay/idle callback path, invalid input validation (`NULL` table / `count==0`) |
| `test_SmartTimers` | `tick`, callback firing, `abort`, `restart` (core behavior used by `hal_soft_timer_*`) |
| `test_pidController` | P output, output clamping, integral reset, stability detection (core behavior used by `hal_pid_controller_*`) |
| `test_multicoreWatchdog` | dual-core liveness gating, external reset path, pre-setup no-op safety |
| `test_tools` | thematic utility and compatibility coverage using HAL mocks, including debug aliases, status-returning numeric/string/ADC/NTC/pixel helpers, endian conversion, wrap-safe periodic random state, legacy time wrappers, and bounded formatting |
| `test_hal_critical_section` | critical-section nesting and interrupt-state restoration behavior |
| `test_hal_dac` | DAC init compatibility plus status-first raw/millivolt writes, channel/range/uninitialised validation and unsupported-target reporting |
| `test_hal_digipot` | MCP401x/MAX5395 facade init/set behavior, range validation and status mapping |
| `test_hal_pcnt` | pulse-counter init/read/reset success, invalid arguments, uninitialised channels and compatibility wrappers |
| `test_hal_i2c_slave` | I2C-slave register map, callbacks, RX/TX transactions and invalid-input handling |
| `test_hal_serial_session_vocabulary` | serial-session command/status vocabulary constants and conversion helpers |
| `test_hal_status` | shared `hal_status_t` values, string conversion, predicates and bool/status adapters |
| `test_hal_modem_at` | generic AT engine command/response parsing, URCs, timeouts and callback dispatch |
| `test_hal_simcom_a76xx` | SIMCom A76xx power/SIM/PDP/GNSS/LBS/MQTT command flows and URC handling |
| `test_pcf8563_driver` | shared PCF8563 register encoding, datetime, alarm, timer, CLKOUT and integrity behavior |
| `test_ds3231_driver` | shared DS3231 datetime, full-calendar writes, alarm, status, oscillator-safe CLKOUT, temperature, I2C failures and register behavior |
| `test_ili9341_driver` | shared ILI9341 command/init sequence, address windows and pixel writes |
| `test_st77xx_driver` | shared ST7735/ST7789/ST7796S/GC9A01 initialization, offsets, windows and pixel writes |
| `test_ssd1306_driver` | shared SSD1306-family initialization, framebuffer updates, controller addressing offsets, suspend/resume and I2C/SPI command/data transfers |
| `test_rgb_oled_driver` | shared SSD1331/SSD135x initialization, contrast/remap command flow, address windows and RGB565 pixel writes |
| `test_st7567_driver` | shared ST7567 initialization, page-buffer sizing, page-aligned writes and invalid-area validation |
| `test_hal_display_rgb_oled_facade` | real shared-facade dispatch for SSD1331/SSD135x capabilities, RGB565 raw writes, GFX and rotation limits over mock SPI |
| `test_hal_display_st7567_facade` | real shared-facade dispatch for ST7567 MONO01/MONO10 capabilities, format switching and page-aligned raw writes over mock SPI |
| `test_jh_gfx_geometry` | shared GFX clipping, geometry primitives, bitmap and text-layout behavior |
| `test_mcp2515_driver` | shared MCP2515 register/SPI transactions, bit timing, TX/RX, filters and errors |
| `test_mfrc522_driver` | shared MFRC522 register transports, initialization and RFID protocol helpers |
| `test_pn532_driver` | shared PN532 SPI/I2C/UART framing, ACK/response parsing and NFC commands |
| `test_ff16_memdisk` | managed FatFs R0.16 integration over an in-memory disk, mount and file I/O behavior |
| `test_stm32_pwm_clock` | STM32G474 PWM timer-clock, prescaler and period calculation coverage |
| `test_hal_onewire_driver` | shared bit-bang OneWire timing, reset/presence, bit/byte I/O and search behavior |
| `test_hal_config_storage_flags` | compile/runtime coverage for storage feature-flag propagation and configuration |
| `test_jpeg` | managed TJpgDec decode, dimensions, RGB565 conversion and malformed input |
| `test_lodepng` | managed LodePNG encode/decode, memory ownership, conversion and error handling |
| `test_gps_nmea_parser` | NMEA framing/checksum, fix/date/time/speed parsing and invalid-input recovery |
| `test_stm32_hal_system` | STM32G474 system clock, reset/fault state and backend system-service simulation |
| `test_stm32_hal_i2c_slave` | STM32G474 I2C-slave register backend, events, callbacks and error handling |
| `test_freertos_posix_runtime` | Host FreeRTOS POSIX scheduler, task dispatch, mutex/delay and lazy create-once behavior, including concurrent serial/debug message boundaries |

### Adding a new test suite

1. Create `tests/test_<name>.cpp` with `#include "utils/unity.h"`, Unity
   `setUp`, `tearDown`, `UNITY_BEGIN`, `RUN_TEST`, and `UNITY_END` calls.
2. Add `add_hal_test(test_<name>)` to `tests/CMakeLists.txt`.
    For suites that compile extra sources (for example `test_tools` and
    `test_multicoreWatchdog`), create a dedicated `add_executable(...)` entry.
3. Rebuild:
   `cmake --build .build/host && ctest --test-dir .build/host`.

### Mock time control

SmartTimers and PIDController depend on `hal_millis()`.
The mock clock starts at 0 and is driven by:

```cpp
hal_mock_set_millis(uint32_t ms);     // set absolute time
hal_mock_advance_millis(uint32_t ms); // advance relative to now
hal_mock_timer_advance_us(uint64_t us); // fires pending hal_timer alarms
```

**Important:** `SmartTimers` uses `_lastTime == 0` as an "uninitialized" sentinel.
Start mock time at a non-zero value (e.g. `hal_mock_set_millis(1000)`) before
calling `SmartTimers::begin()` to avoid the guard triggering in tests.

---

*Back to [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)*

*Next: [Multicore safety, drivers, migration](04_multicore_drivers_migration.md)*
