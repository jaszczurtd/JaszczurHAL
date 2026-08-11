# Build dependencies and unit tests

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## Dependencies (hardware build)

| HAL module | External dependency |
|---|---|
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system` | Pico SDK `hardware_*` / `pico_*` APIs on the RP family; STM32G474 register backend. `hal_system` also uses FreeRTOS task APIs in supported `HAL_ENABLE_FREERTOS` builds |
| `hal_usb` | HAL-owned TinyUSB device on RP: CDC descriptors, IRQ/timer pump in bare builds, core-0 worker task in FreeRTOS builds, and BOOTSEL reset. STM32G474 is currently unsupported. Mock provides deterministic CDC buffers and a reset observer. |
| `hal_serial` | One target-independent serial/debug core plus link-time ports: RP `hal_usb` CDC, STM32G474 debug USART2/host stdout, and mock stdout capture/injectable RX. |
| `hal_sync` | RP: Pico SDK `pico/mutex.h` in bare builds and FreeRTOS `semphr.h` / `task.h` in `HAL_ENABLE_FREERTOS` builds. STM32G474: atomic spinlock in bare builds and FreeRTOS mutex/task APIs in `HAL_ENABLE_FREERTOS` builds |
| `hal_timer` | RP2040: pico SDK alarm/time APIs (`pico/time.h`); STM32G474: TIM6 + NVIC register backend |
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
| `hal_spi` | RP2040 native Pico SDK `hardware/spi.h`; STM32G474 register backend |
| `hal_lora_radio` | Mutually exclusive family providers: the pinned official Semtech SX126x driver with the HAL adapter for validated SX1262 and experimental software-only SX1261, or the HAL-owned register provider for experimental software-only SX1276/SX1278; both compile for RP and STM32G474 and have deterministic mock coverage |
| `hal_i2c` | RP2040 native Pico SDK `hardware/i2c.h`; STM32G474 register backend |
| `hal_swserial` | native Pico SDK PIO/DMA backend on RP2040; shared HAL GPIO/timing/sync backend on other targets |
| `hal_gps` | one portable facade selecting `hal_uart` / `hal_swserial` at compile time, plus the shared in-tree NMEA engine |
| `hal_rgb_led` | shared NeoPixel core (`hal/gpio/neopixel/jh_neopixel.*`) + target transport glue |
| `hal_thermocouple` (MCP9600/MCP9601) | shared driver (`hal/temperature/mcp9600/mcp9600_driver.*`) |
| `hal_thermocouple` (MAX6675) | shared driver (`hal/temperature/max6675/max6675_driver.*`) |
| `hal_onewire` | shared bit-bang driver (`hal/onewire/onewire_driver.*`) over HAL GPIO/time |
| `hal_ds18b20` | shared DS18B20 backend (`hal/temperature/ds18b20/hal_ds18b20.cpp`) over shared OneWire |
| `hal_external_adc` | shared ADS1X15/ADS1115 driver (`hal/analog/ads1x15/ads1x15_driver.*`) |
| `hal_pga2311` | shared PGA2311 stereo volume driver (`hal/audio/pga2311/pga2311_driver.*`) over HAL SPI/GPIO |
| `hal_wifi` | pinned CYW43 driver and lwIP; RP uses PIO gSPI, while STM32G474 uses the configured gSPI bus |
| `hal_littlefs` | pinned `third_party/littlefs` core plus coordinated internal flash on RP and STM32G474 |
| `hal_udp` | shared lwIP raw UDP engine over the selected CYW43 network backend |
| `hal_tls` | bundled BearSSL over native `hal_tcp`; the optional BSD transport adapter is built only when `HAL_ENABLE_BSD_SOCKETS` is also enabled |
| BSD sockets adapter | shared `hal/network/adapters/bsd/hal_bsd_sockets.cpp` over HAL UDP/TCP; remains independently selectable without TLS |
| `hal_wireguard` | shared WireGuard/lwIP engine + capability-advertised host-lwIP backend |
| `hal_mqtt` | bundled `PubSubClient` over HAL TCP, with optional BearSSL MQTTS transport |
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

### Quick start scripts

Two convenience scripts in the repository root simplify local development:

**`runmefirst.sh`** - One-time toolchain setup
```bash
./runmefirst.sh
```
Configures your local environment for the first time:
- Installs git hooks (pre-commit and commit-msg from `.githooks/`)
- Synchronizes all pinned components through `third_party/update_components.sh`
- Offers persistent, LAN-scoped TCP/8266 firewall setup for OTA callbacks
- Sets up build directories and initial CMake configuration
- Run this once when cloning the repository or after environment changes

**`runalltests.sh`** - Full validation gate
```bash
./runalltests.sh
```
Runs the complete quality-gate suite (8 gates, in order):
1. Tool-presence check
2. Host/mock unit tests (`.build/gate/host/` + ctest, incl. FreeRTOS POSIX)
3. Memory safety (Valgrind memcheck on `MEMCHECK_REQUIRED_TESTS`)
4. Static analysis: cppcheck
5. Static analysis: clang-tidy (host + STM32 compile databases below
   `.build/gate/`)
6. PMD CPD duplicate detection across owned C/C++ implementation sources
7. Target builds (STM32G474 plus Pico SDK RP2040/RP2350 ARM/RP2350 RISC-V
   entry/core probes, RP feature profiles, and six representative
   `01_core_runtime`/`18_freertos_suite` ELF/BIN/UF2 builds)
8. Examples build (52 dispatcher-backed `gateTargets` configurations: 27 for
   RP2040 and 25 for STM32G474, plus the dedicated target/runtime fixtures)

Exits non-zero on the first failure; logs capture any warnings/errors.

All repository-owned compilation output is kept below the single ignored
`.build/` root. CMake script-mode compiler probes use `.build/tests/`; they do
not emit `.o` files into the repository root.

The clang-tidy gate creates profile-specific analysis databases with one
compile command per source file. This keeps facade tests that compile the same
shared driver under several feature sets from triggering duplicate analyzer
runs while normal target builds still compile every configured variant.

The CPD gate uses the authenticated PMD 7.26.0 distribution managed under
`third_party/pmd`. It scans implementation files rather than headers and
excludes generated and vendored sources. Every duplicate group from 100 tokens
blocks in production, tests, and examples; no baseline or allowlist can hide an
existing group. The report computes the union of duplicated token ranges and
prints coverage globally and for mock, RP2040, STM32G474, shared, and remaining
portable code. XML reports and deterministic file lists are written below
`.build/gate/cpd/`. CPD `PASS` means zero groups at the configured threshold.

This is the **recommended pre-commit validation** and **CI/CD test gate**. Run before pushing changes to catch cross-platform issues early.

### Native Windows CI gate

`.github/workflows/ci.yml` runs two native `windows-2025` gates in addition to
the complete Linux quality gate:

- `windows-tooling` prepares the authenticated managed environment, repeats
  `runmefirst.ps1 -VerifyOnly`, runs the shared runtime/platform/bootstrap and
  generator tests, verifies the RP and STM32 FreeRTOS CMake dependency source
  selection, then compiles and runs the portable host contracts with MSVC
  `/W4 /permissive- /WX`;
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

### Native RP hardware fixtures

The repeatable physical-device probes use the same VS Code dispatcher as
applications and keep their artifacts below `.build/hardware/`:

| Fixture | Coverage |
|---|---|
| `tests/hardware/rp_usb_cdc_echo` | Native TinyUSB CDC enumeration, backpressure, reconnect and throughput |
| `tests/hardware/rp_usb_multicore` | Concurrent CDC producers on both RP cores, record integrity, completeness and affinity in bare-metal/FreeRTOS |
| `tests/hardware/rp_freertos_smp` | Scheduler, both cores, mutex/delay, heap and USB under FreeRTOS SMP |
| `tests/hardware/rp_flash_transaction` | Flash coordinator sequencing, rejection paths, erase/program and recovery |
| `tests/hardware/rp_storage` | EEPROM commit/persistence, LittleFS format/remount and cross-reset mounting |
| `tests/hardware/rp_sdlogger` | Physical SPI SD mount, deterministic append, flush/close, reset/remount, content and EEPROM log-counter persistence |
| `tests/hardware/rp_ota` | Discovery, authentication, transfer, trial/confirm, rollback and USB/network recovery |
| `tests/hardware/lora_sx1262` | Two-device SX1262 initialization, bidirectional packets, RSSI/SNR, sleep/wake and destroy/create reinitialization on integrated LF or external HF pairs |

Each fixture contains its exact build, upload and verifier commands in its
local `README.md`. The storage probe supports `rp2040`, `rp2350-arm` and
`rp2350-riscv`.

---

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

`tools.cpp` is covered by `test_tools` using HAL mocks.
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

### Test suites

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
| `test_hal_rtc` | RTC init/get/set datetime, full Gregorian validation, 1970/2000/2099 epoch boundaries and overflow, integrity flag, interrupt mask, read-clear event flags, CLKOUT/timer/alarm configuration, legacy invalid-input guards and `_ex` status mapping |
| `test_jh_rtc_i2c_provider` | Shared PCF8563/DS3231 provider selection, metadata, datetime/event translation, and backend capability status mapping over mock HAL I2C |
| `test_rtc_architecture` | Single RTC facade ownership, deleted target-local copies, provider boundaries, shared validation/locking, HAL-only chip drivers, and source-manifest wiring |
| `test_jh_calendar` | Gregorian leap-year/month-length/day-of-week validation, impossible dates, Unix epoch zero, leap-day round-trip, RTC upper boundary and 64-bit overflow statuses |
| `test_calendar_architecture` | Shared calendar source ownership, HAL-only legacy time wrappers, and rejection of target/driver-local calendar algorithms or `hal_time_from_components()` copies |
| `test_hal_eeprom` | byte/int write-read, `commit` flag |
| `test_hal_serial` | Serial wire/message boundaries, binary RX inject + `available`/`read`, task/ISR debug prefixes, accepted/rejected timestamps, rate-limit configuration/lifecycle/source isolation, streamed formatting beyond `HAL_DEBUG_BUF_SIZE`, ISR-deferred ring/drop summaries, mute and flush semantics |
| `test_hal_serial_session` | Framed HELLO/AUTH lifecycle, deterministic and consecutive random challenges, entropy fail-closed behavior, challenge cleanup, command compatibility, unknown-handler dispatch, seq echo, malformed-frame drops and null-arg safety |
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
| `test_sx126x_adapter` | Official driver command orchestration, SX1261/SX1262 PA and OCP selection, SPI transaction cleanup, BUSY deadlines, RF switch levels, electrical setup, band calibration, TX timeout and RX CRC IRQ mapping |
| `test_hal_lora_sx127x` | SX1276/SX1278 model-specific descriptor validation and the common facade lifecycle, capabilities, calibration boundary, TX, RX, CAD and power states |
| `test_sx127x_adapter` | SX127x register transport, version probe, modem/frequency/PA configuration, IRQ/status mapping, FIFO metadata, RSSI, CAD, timeout, cancellation, bus errors and TCXO sleep/wake behavior |
| `test_hal_pga2311` | PGA2311 status/config validation, pool exhaustion, injected SPI failures and retry, frame writes, dB/code conversion, soft/hardware mute behavior |
| `test_irsmall_decoder_driver` | IRsmallDecoder NEC/NECx/SIRC/Samsung frame decode, RC5 transition-table decode including extended command bit, repeat/held reporting, timeout reset and interrupt disable/enable paths |
| `test_hal_i2c` | bus0/bus1 transfer and status paths, direct read helpers, locking, init/deinit, bus clear, bounded scan results, count-only/overflow behavior and per-address callback coverage |
| `test_hal_rgb_led` | status-first init/init_ex, invalid config, allocation/transport failure, retry, brightness clamp, off and pre-init guard |
| `test_hal_display` | status-first display API, capabilities/raw-write contract, text sizing/formatting, presets, drawing, SSD1306 init, streaming/async DMA state, validation and injected backend-I/O failures |
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
| `test_hal_littlefs` | mount/unmount flow, size stats, path exists/remove helpers, format success/failure behavior, direct status operations, missing-path and unmounted-state semantics, input validation |
| `test_hal_sdlogger` | EEPROM-backed file numbering, buffered log flush/close, crash-report formatting, SD/open failure paths |
| `test_hal_udp` | begin/parse/read flow, handle-based multi-socket bind/RX/TX separation, chunked datagram reads, remote endpoint capture/reset-on-stop, beginPacket explicit/remote sender paths, write/endPacket behavior, input validation |
| `test_hal_tcp` | TCP client connect/send/recv/shutdown/close, listener bind/listen/accept, backlog/pool limits, readiness probes and accepted-socket independence |
| `test_hal_http_server` | HTTP route dispatch, query/body/header parsing, exact/prefix routes, response headers/body, HEAD handling, handler failures and invalid configuration |
| `test_hal_http_files` | Callback-backed HTTP file serving, MIME mapping, ETag/`If-None-Match`, raw PUT, multipart upload and path traversal rejection |
| `test_hal_websocket` | HTTP Upgrade handshake, `Sec-WebSocket-Accept`, masked text frames, broadcast, ping/pong, close callbacks and invalid handshakes |
| `test_hal_net_console` | Password-required TCP console start/auth flow, serial/debug mirroring to authenticated clients, multi-client broadcast, bidirectional command input, per-client replies and disconnect callbacks |
| `test_hal_net_commands` | JSON/text command registration and dispatch, HTTP route integration, WebSocket message integration, structured errors and API validation |
| `test_bsd_sockets` | BSD/POSIX adapter fd mapping, sockaddr translation, errno/EAI paths, TCP/UDP flow, nonblocking mode, `select()`, `getaddrinfo()` and `setsockopt()` |
| `test_bsd_socket_headers_c` | portable C declaration/constant/structure contract for BSD socket headers; runs under GNU-like hosts and MSVC |
| `test_hal_tls` / `test_bearssl_provider` | public TLS lifecycle, native HAL TCP transport, bounded BearSSL progression and optional TLS-over-BSD callbacks |
| TLS/BSD compile probes | prove that TLS builds without BSD, BSD builds without TLS, and each flag propagates only its required network modules |
| `test_bsd_sockets_c_compile` | C compile/link smoke test for socket headers, `netdb.h`, TCP/UDP client/server shapes, `fcntl()`, `select()`, `getaddrinfo()` and `setsockopt()` |
| `test_hal_wireguard` | IPv4 parser validation, byte-array and text WireGuard begin/begin_advanced/kick paths, peer-up endpoint reporting (`hal_wireguard_peer_up` + `hal_wireguard_peer_up_quick`), handshake kick trigger, input validation |
| `test_hal_mqtt` | server/connect flow, publish/subscribe/unsubscribe capture, callback dispatch from `hal_mqtt_loop`, invalid input guards |
| `test_hal_network_status` | Cross-module WiFi/DNS, TCP/UDP, MQTT and WireGuard status API validation, output initialization, pool exhaustion, state and failure mapping |
| `test_hal_ota` | OTA config setters, begin/is_started flow, boot status/confirmation, callback dispatch from injected start/progress/error/end events, callback replace/unregister flow, re-begin queue-clear behavior, invalid input guards |
| `test_ota_image` | Versioned OTA manifest and redundant boot-state encoding, CRC/HMAC validation, corruption handling, sequence wraparound and newest-record selection |
| `test_ota_swap_engine` | Resumable program/staging sector swap across every simulated pre/post-mutation failure boundary, reverse swap rollback and corrupt phase rejection |
| `test_rp_ota_artifacts` | Native RP OTA packaging helper, including RP2040-E14 sector padding, real-page preservation, UF2 renumbering and overlap rejection |
| `test_hal_time` | timezone, NTP sync, Unix/local time formatting, component conversion and uint32 overflow, CET/CEST boundaries and rollover, half-open ranges, and minute extraction |
| `test_hal_kv` | u32/blob CRUD, delete, unchanged-skip, GC, concurrent updates, direct EEPROM-status propagation, uninitialised/range/capacity errors and output initialization |
| `test_hal_crypto` | Base64/MD5/one-shot and incremental SHA-256/HMAC-SHA256/ChaCha20/ChaCha20-Poly1305 helper behavior, input validation, and ChaCha20 counter-wrap rejection regression checks |
| `test_wireguard_crypto_shared` | shared WireGuard crypto primitives (`crypto_equal/zero`, BLAKE2s, X25519, ChaCha20, ChaCha20-Poly1305 including RFC8439 IETF detached AEAD vectors) |
| `test_hal_soft_timer` | C wrapper coverage: create/begin/tick/abort/restart, table setup/tick helpers, delay/idle callback path, invalid input validation (`NULL` table / `count==0`) |
| `test_SmartTimers` | `tick`, callback firing, `abort`, `restart` (core behavior used by `hal_soft_timer_*`) |
| `test_pidController` | P output, output clamping, integral reset, stability detection (core behavior used by `hal_pid_controller_*`) |
| `test_multicoreWatchdog` | dual-core liveness gating, external reset path, pre-setup no-op safety |
| `test_tools` | utility coverage from `tools.cpp` using HAL mocks, including `debugInit`, `setDebugPrefixWithColon`, numeric/string helpers, HAL-delegating legacy time wrappers, and buffer-safe formatting helpers |
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
| `test_ds3231_driver` | shared DS3231 datetime, alarm, status, temperature and register behavior |
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

*Back to [JaszczurHAL API Reference](../JaszczurHAL_API.md)*

*Next: [Multicore safety, drivers, migration](04_multicore_drivers_migration.md)*
