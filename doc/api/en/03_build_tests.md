<a id="build-dependencies-tests-and-hardware-fixtures"></a>

# Building, automated tests, and hardware validation

*Also available in [Polish](../pl/03_build_tests.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

This chapter covers build dependencies, tests that run on a workstation, and procedures for checking the library on physical devices. A successful build, a mock-based test, and a hardware test establish different things; none should be treated as a substitute for the others.

<a id="dependencies-hardware-build"></a>

## Dependencies for device builds

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

<a id="dependencies-mock--pc-build"></a>

## Dependencies for workstation tests

All `impl/.mock/` files depend only on standard host headers such as
`<cstdio>`, `<cstring>`, `<mutex>`, `<queue>`, and `<stdarg.h>`. No embedded
SDK is required.

---

<a id="test-system-map-and-sources-of-truth"></a>

## Test organization and configuration

| Test layer | Configuration source | Execution | Extension point |
|---|---|---|---|
| Host/mock unit tests | `tests/CMakeLists.txt`, `tests/test_*.cpp`, root `CMakeLists.txt` | CMake plus CTest | Add a Unity suite and register it with `add_hal_test(...)`, or declare a dedicated executable for extra sources. |
| Target backends on the host | `tests/fakes/<sdk>/` headers and fakes, a dedicated executable in `tests/CMakeLists.txt` | CMake plus CTest | Compile the real backend against the fake SDK and check its behaviour through the fake: recorded driver calls, injected failures, and simulated interrupts. |
| FreeRTOS POSIX host tests | `tests/freertos_posix/`, `JH_ENABLE_FREERTOS_POSIX_TESTS` | CTest through the host build or full gate | Add a target with `add_hal_freertos_posix_test(...)`. |
| Repository quality gate | `runalltests.sh`, `.github/workflows/ci.yml`, and the tooling data described in `00_scripts.md` | `./runalltests.sh` | Extend the owning gate and its focused regression tests; keep generated artifacts below `.build/`. |
| Firmware compile fixtures | `tests/fixtures/<fixture>/.vscode/jaszczurhal.project.json` | `jh-vscode` or the target production runner | Extend the manifest target/board/variant matrix and its artifact-layout test. |
| Physical hardware fixtures | `tests/hardware/<fixture>/` source, manifest, and verifier | Build/upload through `jh-vscode` or the target production runner, then run the verifier described in the fixture README | Add the firmware, explicit hardware matrix, host oracle, a README with the procedure and acceptance criteria in both languages, and a row in the table below. |

If the description disagrees with test behavior, check the configuration and executable files listed above. Each hardware fixture README holds its procedure, wiring, and requirements; [Tests on physical devices](#hardware-fixtures) lists them all.

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

<a id="repository-quality-gates"></a>

## Repository validation

<a id="quick-start-scripts"></a>

### Set up the workstation and run validation

The repository root contains two main scripts:

**`runmefirst.sh`** - workstation and toolchain setup
```bash
./runmefirst.sh
```
Configures your local environment for the first time:
- Installs git hooks (pre-commit and commit-msg from `.githooks/`)
- Synchronizes all pinned components through `third_party/update_components.sh`
- Installs persistent RP2040/RP2350 USB and `/dev/ttyACM*` access rules for
  sudo-less upload and automatic 1200-bps BOOTSEL reset
- Offers persistent, LAN-scoped firewall setup for the OTA TCP/8266 callback
  and UDP/8266 discovery replies
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
3. Clang ASan/UBSan tests, native tests under ThreadSanitizer, and bounded
   libFuzzer smoke checks through the same runner used by CI
4. Memory safety (Valgrind memcheck on all native C/C++ test executables)
5. Static analysis: cppcheck
6. Static analysis: clang-tidy (host + STM32 compile databases below
   `.build/gate/`)
7. PMD CPD duplicate detection across owned C/C++ implementations and Python
   scripts
8. Target builds (STM32G474 plus Pico SDK RP2040/RP2350 ARM/RP2350 RISC-V
   entry/core probes, RP feature profiles, six representative
   `01_core_runtime`/`18_freertos_suite` ELF/BIN/UF2 builds, one clean
   compile-only `tests/fixtures/esp32s3_phase3` build with the pinned ESP-IDF
   and validated multi-image manifest, and the ESP32-S3 all-features
   `libJaszczurHAL.a` covering the whole target allowlist)
9. Examples build (the dispatcher-derived `gateTargets` matrix for RP2040,
   STM32G474 and ESP32-S3 plus dedicated target/runtime fixtures)

Exits non-zero on the first failure; logs capture warnings/errors from both
standard output and standard error.
The Valgrind gate selects every directly registered native C/C++ test executable
through the CTest `memcheck` label. `MEMCHECK_REQUIRED_TESTS` in
`runalltests.sh` is a critical subset checked before execution, not the complete
selection. Python, CMake, and shell-driver tests remain outside memcheck. Fair
Valgrind thread scheduling keeps the native FreeRTOS POSIX scheduler tests in
the selection. Live CTest/Valgrind progress is streamed to the terminal and
`.build/gate/logs/jh_memcheck.log`.

All build and test outputs go in the ignored `.build/` directory. CMake compiler probes run in script mode use `.build/tests/` and do not leave `.o` files in the repository root.

The clang-tidy stage creates a separate compilation database for each profile, with one entry per source file. This avoids repeated analysis of a shared driver that API tests compile with several feature sets. Normal target builds still compile every configured variant.

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

Run the full validation before committing and pushing changes. CI/CD uses the same checks.

<a id="native-windows-ci-gate"></a>

### Native Windows CI validation

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

<a id="hardware-fixtures"></a>

## Tests on physical devices

Device tests use the same VS Code tooling as user applications. Their outputs go in `.build/hardware/`.

The default `runalltests.sh`, CTest configuration, and CI do not read or build
this firmware. Run the commands in this section explicitly when the required
devices are available. Optional host-side checks of fixture-specific files can
be registered with `-DJH_ENABLE_HARDWARE_FIXTURE_CHECKS=ON`.

| Fixture | Coverage |
|---|---|
| [`tests/hardware/bluetooth_stage1`](../../../tests/hardware/bluetooth_stage1/README.md) | Internal pre-API CYW43/BTstack controller, advertising, static GATT and WiFi-only memory baseline on Pico W and STM32G474/PIM730. |
| [`tests/hardware/bluetooth_gamepad`](../../../tests/hardware/bluetooth_gamepad/README.md) | Private Classic HID Host gamepad parser probe for Pico 2 W, using the sanitized 8BitDo Zero 2 Android D-input descriptor/report data from `tests/fixtures/bluetooth_gamepad`. |
| [`tests/hardware/bluetooth_classic_hid_device`](../../../tests/hardware/bluetooth_classic_hid_device/README.md) | Private Pico W Classic HID mouse used to validate the public generic HID Host on a second Pico radio. |
| [`tests/hardware/bluetooth_classic_hci_trace`](../../../tests/hardware/bluetooth_classic_hci_trace/README.md) | Private privacy-preserving raw HCI inquiry trace and CYW43 transport/clock diagnostics for Pico W and Pico 2 W. |
| [`tests/hardware/bluetooth_observer`](../../../tests/hardware/bluetooth_observer/README.md) | Public passive Observer scan, bounded report queue and Teltonika/iBeacon/Eddystone BLE parsing on Pico W, Pico 2 W and STM32G474/PIM730. |
| [`tests/hardware/bluetooth_stream`](../../../tests/hardware/bluetooth_stream/README.md) | Public BLE lifecycle and authenticated Stream gate across target/board/runtime tuples, including reconnect, watchdog, sustained traffic, saturation and negative security cases. |
| [`tests/hardware/rp_usb_cdc_echo`](../../../tests/hardware/rp_usb_cdc_echo/README.md) | Native TinyUSB CDC enumeration, backpressure, reconnect and throughput |
| [`tests/hardware/rp_gpio_irq_context`](../../../tests/hardware/rp_gpio_irq_context/README.md) | Context-aware GPIO interrupts: per-pin routing, both handler kinds side by side, replacement and detach |
| [`tests/hardware/rp_usb_multicore`](../../../tests/hardware/rp_usb_multicore/README.md) | Concurrent CDC producers on both RP cores, record integrity, completeness and affinity in bare-metal/FreeRTOS |
| [`tests/hardware/rp_freertos_smp`](../../../tests/hardware/rp_freertos_smp/README.md) | Scheduler, both cores, mutex/delay, heap and USB under FreeRTOS SMP |
| [`tests/hardware/rp_flash_transaction`](../../../tests/hardware/rp_flash_transaction/README.md) | Flash coordinator sequencing, rejection paths, erase/program and recovery |
| [`tests/hardware/rp_kv_power_loss`](../../../tests/hardware/rp_kv_power_loss/README.md) | Dual-bank KV recovery after interruption following erase, body write, verification and publication |
| [`tests/hardware/rp_storage`](../../../tests/hardware/rp_storage/README.md) | EEPROM commit/persistence, LittleFS format/remount and cross-reset mounting |
| [`tests/hardware/rp_sdlogger`](../../../tests/hardware/rp_sdlogger/README.md) | Physical SPI SD mount, deterministic append, flush/close, reset/remount, content and EEPROM log-counter persistence |
| [`tests/hardware/rp_ota`](../../../tests/hardware/rp_ota/README.md) | Discovery, authentication, transfer, trial/confirm, rollback, persistent-storage coexistence and USB/network recovery |
| [`tests/hardware/lora_sx1262`](../../../tests/hardware/lora_sx1262/README.md) | Two-device SX1262 initialization, bidirectional raw packets, reliable-link lifecycle, and fragmented command-router request/response transactions on integrated LF or external HF pairs |
| [`tests/hardware/esp32s3_phase1`](../../../tests/hardware/esp32s3_phase1/README.md) | Phase 1 ESP32-S3 target/board identity, generated link signature, chip/core count, physical flash, initialized Quad PSRAM, and a repeated FreeRTOS `app_task0()` heartbeat over native USB Serial/JTAG. |
| [`tests/hardware/esp32s3_phase2`](../../../tests/hardware/esp32s3_phase2/README.md) | ESP32-S3 Phase 2 runtime probe for both application tasks, system/sync, GPIO/IRQ including context-aware handlers, ADC, USB Serial/JTAG TX/RX, hardware UART, I2C master scan, SPI master transfer path, dedicated-pool timer callbacks, and enabled FreeRTOS stack-guard configuration. |

Each fixture README lists the wiring, the commands, and the pass criteria. The last three checks below use examples instead of a dedicated fixture. A successful build confirms that firmware was built, not that it works correctly on a device. Hardware acceptance requires running the procedure, passing the host verifier or the specified visual inspection, and meeting the documented PASS criteria.

<a id="bluetooth-classic-manager-hardware-probe"></a>

### Bluetooth Classic manager hardware test

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

The check does not cover an audio data profile.

<a id="ble-and-classic-gamepad-coexistence-gate"></a>

### BLE and Classic gamepad coexistence validation

The public `ble` variant of example 29 runs a passive BLE Observer beside the
Classic HID/gamepad profile on the shared CYW43 host:

```sh
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant ble
vscode/entry/jh-vscode upload \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant ble \
  --port /dev/ttyACM0
```

Follow the pairing procedure in the
[example README](../../../examples/29_bluetooth_gamepad/README.md). Acceptance
requires valid gamepad input while BLE reports continue, successful
`BLE_STOP`/`BLE_START`, and HID disconnect/reconnect without stopping BLE.
`INFO` must retain both radio-runtime users and must not report HCI transport,
fixed-pool allocation, or queue errors.

<a id="a2dp-sink-and-avrcp-target-hardware-gate"></a>

### A2DP Sink and AVRCP Target hardware validation

Example 30 is the public A2DP/AVRCP hardware exercise for Pico W and Pico 2 W:

```sh
vscode/entry/jh-vscode build \
  --project examples/30_bluetooth_speaker \
  --target rp2040 --board picow --variant avrcp
vscode/entry/jh-vscode upload \
  --project examples/30_bluetooth_speaker \
  --target rp2040 --board picow --variant avrcp \
  --port /dev/ttyACM0
vscode/entry/jh-vscode build \
  --project examples/30_bluetooth_speaker \
  --target rp2350-arm --board pico2w --variant avrcp
vscode/entry/jh-vscode upload \
  --project examples/30_bluetooth_speaker \
  --target rp2350-arm --board pico2w --variant avrcp \
  --port /dev/ttyACM0
```

Follow the wiring, pairing, and serial-command procedure in the
[example README](../../../examples/30_bluetooth_speaker/README.md). Acceptance
requires clean SBC playback, pause/resume/stop, AVRCP absolute volume, bonded
reconnect after restart, a separate watchdog-reset reconnect, and no queue,
pool, DMA, or timing failure. The physical output stage remains a product-level
check. The `ble-a2dp` variant is part of the compile gate; active audio+BLE
coexistence is not yet a hardware requirement.

<a id="firmware-compilelink-fixtures"></a>

## Firmware compilation and linking checks

<a id="esp32-s3-compilelink-fixture"></a>

### ESP32-S3 compilation and linking

| Fixture | Coverage |
|---|---|
| `tests/fixtures/esp32s3_phase3` | Compile-only ESP-IDF project selecting every ESP32-S3 backend delivered through Phase 3. It checks feature/source/dependency resolution, compilation, linking, `two-ota-large` partition generation, and artifact publication. |

CI and local validation stage 8 build this project. A successful build does not validate WiFi, sockets, TLS, services, OTA, or WireGuard at runtime, nor does it replace tests of the new Phase 2 peripherals. These areas need separate hardware, lifecycle, and negative security tests.

---

<a id="host-test-architecture"></a>

## How workstation tests are built

<a id="how-it-works"></a>

### Test library and dependencies

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

Tests use Unity 2.5.4 in `third_party/Unity/src`. The version-controlled JaszczurHAL integration consists of:

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

This expects `tests/test_my_module.cpp` and creates a test executable linked with `hal_mock`.

Register a test that needs additional implementation files as a separate CMake target:

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

<a id="test-suite-guide"></a>

### Listing the test suites

The complete test registry is in `tests/CMakeLists.txt`. To list the tests registered in the current checkout, run:

```bash
ctest --test-dir .build/host -N
```

Host suites live in `tests/test_<name>.c` or `.cpp`, script and repository checks in `tests/test_<name>.py`, and CMake checks in `tests/test_<name>.cmake`; the name says what each one covers.

### Adding a new test suite

1. Create `tests/test_<name>.cpp` with `#include "utils/unity.h"`, Unity
   `setUp`, `tearDown`, `UNITY_BEGIN`, `RUN_TEST`, and `UNITY_END` calls.
2. Add `add_hal_test(test_<name>)` to `tests/CMakeLists.txt`.
    For suites that compile extra sources (for example `test_tools` and
    `test_multicoreWatchdog`), create a dedicated `add_executable(...)` entry.
3. Rebuild:
   `cmake --build .build/host && ctest --test-dir .build/host`.

<a id="mock-time-control"></a>

### Controlling time in mock-based tests

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
