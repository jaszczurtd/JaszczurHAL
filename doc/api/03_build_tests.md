# Build dependencies and unit tests

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## Dependencies (hardware build)

| HAL module | External dependency |
|---|---|
| `hal_gpio`, `hal_pwm`, `hal_adc`, `hal_system` | Arduino-pico core (`Arduino.h`) on RP2040; STM32G474 register backend. `hal_system` also uses FreeRTOS task APIs in supported `HAL_ENABLE_FREERTOS` builds |
| `hal_serial` | RP2040: TinyUSB CDC / pico SDK headers provided by the arduino-pico toolchain, with a native HAL transport instead of Arduino `Serial.print()`. STM32G474: debug UART / stdio backend. Mock: stdio capture helpers. |
| `hal_sync` | RP2040: pico SDK `pico/mutex.h` in normal builds, FreeRTOS `semphr.h` / `task.h` in `HAL_ENABLE_FREERTOS + __FREERTOS` builds. STM32G474: atomic spinlock in normal builds, FreeRTOS `semphr.h` / `task.h` in `HAL_ENABLE_FREERTOS` builds |
| `hal_timer` | RP2040: pico SDK alarm/time APIs (`pico/time.h`); STM32G474: TIM6 + NVIC register backend |
| `hal_soft_timer` | internal `SmartTimers` utility |
| `hal_pid_controller` | internal `pidController` utility |
| `hal_can` | generic CAN facade plus backend-selected CAN drivers: MCP2515 (`impl/shared/drivers/mcp2515/*`), MCP251XFD (`impl/shared/drivers/mcp251xfd/*`) and STM32G474 native FDCAN (`impl/stm32g474/hal_can_stm32g474_fdcan.*`) |
| `hal_display` | Shared Arduino-free display stack (`impl/shared/drivers/display/hal_display.cpp`, `jh_gfx.*`, `ili9341_driver.*`, `st77xx_driver.*`, `ssd1306_driver.*`) reused by RP2040 and STM32G474; target backends provide SPI/I2C/GPIO transport |
| `hal_hd44780` | shared HD44780-compatible character LCD driver (`impl/shared/drivers/hd44780/hd44780.*`) over HAL GPIO/system timing |
| `hal_dma_pwm_audio` | timer-paced PWM-audio DMA helper used by DACless on RP2040, STM32G474 and mock |
| `hal_dacless` | shared DACless PWM-audio engine (`impl/shared/drivers/dacless/dacless.*`) over HAL DMA/PWM-freq, ADC, timing and synchronization |
| `hal_tsc2007` | shared TSC2007 resistive touch controller driver (`impl/shared/drivers/tsc2007/tsc2007.cpp`) over HAL I2C/system timing |
| `hal_stmpe610` | shared STMPE610 resistive touch controller driver (`impl/shared/drivers/stmpe610/stmpe610.cpp`) over HAL I2C or HAL SPI/GPIO |
| `hal_irsmall_decoder` | shared IR receiver decoder (`impl/shared/frameworks/irsmall_decoder/irsmall_decoder.cpp`) over HAL GPIO interrupts and system timing |
| `hal_spi` | RP2040 native Pico SDK `hardware/spi.h`; STM32G474 register backend |
| `hal_i2c` | RP2040 native Pico SDK `hardware/i2c.h`; STM32G474 register backend |
| `hal_swserial` | shared HAL GPIO/timing/sync software UART |
| `hal_gps` | portable in-tree NMEA engine + `hal_uart` / `hal_swserial` transport |
| `hal_rgb_led` | shared NeoPixel core (`impl/shared/drivers/neopixel/jh_neopixel.*`) + target transport glue |
| `hal_thermocouple` (MCP9600/MCP9601) | shared Arduino-free driver (`impl/shared/drivers/mcp9600/mcp9600_driver.*`) |
| `hal_thermocouple` (MAX6675) | shared Arduino-free driver (`impl/shared/drivers/max6675/max6675_driver.*`) |
| `hal_onewire` | shared Arduino-free bit-bang driver (`impl/shared/drivers/onewire/onewire_driver.*`) over HAL GPIO/time |
| `hal_ds18b20` | shared Arduino-free DS18B20 backend (`impl/shared/drivers/ds18b20/hal_ds18b20.cpp`) over shared OneWire |
| `hal_external_adc` | shared Arduino-free ADS1X15/ADS1115 driver (`impl/shared/drivers/ads1x15/ads1x15_driver.*`) |
| `hal_pga2311` | shared Arduino-free PGA2311 stereo volume driver (`impl/shared/drivers/pga2311/pga2311_driver.*`) over HAL SPI/GPIO |
| `hal_wifi` | Arduino-pico WiFi stack (`WiFi.h`) |
| `hal_littlefs` | Arduino-pico `LittleFS` on RP2040; upstream littlefs + STM32 internal flash partition on STM32G474 |
| `hal_udp` | Arduino-pico `WiFiUDP` |
| `hal_wireguard` | bundled `arduino-wireguard-pico-w` + Arduino-pico WiFi/lwIP stack |
| `hal_mqtt` | bundled `PubSubClient` + Arduino-pico `WiFiClient` |
| `hal_ota` | Arduino-pico `ArduinoOTA` |
| `hal_time` | Arduino-pico / lwIP SNTP (`configTime`) |
| `hal_kv` | internal `hal_eeprom` + `hal_sync` |
| `hal_sdlogger` | shared FatFs file layer in `impl/shared/frameworks/filesystem/` |
| `tools` | HAL APIs |
| `multicoreWatchdog` | internal `SmartTimers` + `hal_sync` mutex |

## Dependencies (mock / PC build)

All `impl/.mock/` files depend only on: `<cstdio>`, `<cstring>`, `<mutex>`, `<queue>`, `<stdarg.h>`.
No Arduino SDK, no pico SDK required.

---

## Unit tests

### Requirements

- CMake ≥ 3.16
- GCC / Clang with C++17

### Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Quick start scripts

Two convenience scripts in the repository root simplify local development:

**`runmefirst.sh`** - One-time toolchain setup
```bash
./runmefirst.sh
```
Configures your local environment for the first time:
- Installs git hooks (pre-commit and commit-msg from `.githooks/`)
- Verifies/downloads FreeRTOS-Kernel if needed
- Sets up build directories and initial CMake configuration
- Run this once when cloning the repository or after environment changes

**`runalltests.sh`** - Full validation gate
```bash
./runalltests.sh
```
Runs the complete quality-gate suite (7 gates, in order):
1. Tool-presence check
2. Host/mock unit tests (`build_test/` + ctest, incl. FreeRTOS POSIX)
3. Memory safety (Valgrind memcheck on `MEMCHECK_REQUIRED_TESTS`)
4. Static analysis: cppcheck
5. Static analysis: clang-tidy (host + STM32 compile databases; `build_stm32_host/`)
6. Target static-library builds (STM32G474 + RP2040 flag matrix)
7. Examples build (RP2040 + STM32G474, via `examples/CMakeLists.txt`)

Exits non-zero on the first failure; logs capture any warnings/errors.

This is the **recommended pre-commit validation** and **CI/CD test gate**. Run before pushing changes to catch cross-platform issues early.

---

### How it works

The CMake build at the project root compiles a static library `hal_mock` from:

- all `src/hal/impl/.mock/*.cpp` stubs,
- the backend-neutral HAL sources in `UTIL_SOURCES` (see `CMakeLists.txt`):
  `hal_config.cpp`, `hal_can_util.cpp`, `compat/debug_format/hal_debug_format.cpp`, `hal_crypto.cpp`,
  `hal_kv.cpp`, `hal_modem_at.cpp`, `hal_simcom_a76xx.cpp`, `hal_timer_ext.cpp`,
  `hal_soft_timer.cpp`, `hal_digipot.cpp`, `hal_pga2311.cpp`, plus the shared
  drivers (mcp2515/mcp251xfd/digipot/pga2311) and shared frameworks
  (wireguard crypto, smart_timers, cJSON, lodepng, jpeg) and `pidController.cpp`,
- `src/utils/unity.c` (Unity framework).

The exact list is the `UTIL_SOURCES` set in `CMakeLists.txt` - treat that as the
source of truth.

Each test executable in `tests/` links against `hal_mock` only - no Arduino
headers, no pico SDK, no hardware.

The bundled Unity test framework lives in:

- `src/utils/unity.c`
- `src/utils/unity.h`
- `src/utils/unity_internals.h`
- `src/utils/unity_config.h`

The host CMake build compiles `src/utils/unity.c` into `hal_mock` and enables
`HAL_ENABLE_UNITY` plus `UNITY_INCLUDE_CONFIG_H`. Test sources include
`"utils/unity.h"` and use the repository-local `unity_config.h`; no external
Unity package is fetched or required. Outside the test/support build, Unity is
inactive unless `HAL_ENABLE_UNITY` is explicitly enabled.

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
#include "hal/hal_system.h"
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/shared/drivers/my_driver/my_driver.cpp
)
target_link_libraries(test_my_driver PRIVATE hal_mock)
add_test(NAME test_my_driver COMMAND test_my_driver)
```

Run only the new suite:

```bash
cmake --build build --target test_my_module
ctest --test-dir build -R test_my_module --output-on-failure
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
| `test_hal_rtc` | RTC init/get/set datetime, integrity flag, interrupt mask, read-clear event flags, CLKOUT/timer/alarm configuration and invalid-input guards |
| `test_hal_eeprom` | byte/int write-read, `commit` flag |
| `test_hal_serial` | `println` capture, `deb`/`derr` capture, streamed debug formatter coverage beyond `HAL_DEBUG_BUF_SIZE`, ISR-deferred log ring behavior, mute semantics, RX inject + `available`/`read` |
| `test_hal_serial_session` | Framed HELLO handshake (encode/decode + CRC), unknown-payload reply (`SC_UNKNOWN_CMD`) and custom unknown-handler dispatch, request<->response seq echo, non-framed input is silently dropped, multi-frame RX handling, null-arg safety |
| `test_hal_swserial` | software UART RX inject, TX capture, pin reassignment |
| `test_hal_uart` | hardware UART RX inject, TX capture, pin reassignment |
| `test_hal_spi` | SPI init/reinit, reset, per-bus lock-depth coverage |
| `test_hal_pga2311` | PGA2311 config validation, SPI frame writes, dB/code conversion, soft/hardware mute behavior |
| `test_irsmall_decoder_driver` | IRsmallDecoder NEC/NECx/SIRC/Samsung frame decode, RC5 transition-table decode including extended command bit, repeat/held reporting, timeout reset and interrupt disable/enable paths |
| `test_hal_i2c` | bus0/bus1 begin/request/read flow, direct read-bytes helper, address capture, busy helper, lock-depth and init/deinit state coverage |
| `test_hal_rgb_led` | init/init_ex, brightness clamp, off path, pre-init set_color guard |
| `test_hal_display` | display helper API (text sizing/formatting, presets, draw image, SSD1306 init + `hal_display_init_ssd1306_i2c_ex`, text-line helpers) |
| `test_hal_can` | send/receive, ring buffer, null-data guard, payload clamp, backend selection, classic-vs-FD frame validation, filter API, `hal_can_process_all`, `hal_can_create_with_retry`, `hal_can_encode_temp_i8` |
| `test_hal_thermocouple` | MCP9600 + MAX6675 inject, unsupported-op NAN returns, ADC resolution, enable/disable, alert/status |
| `test_max6675_driver` | Shared MAX6675 raw decode, open-circuit fault, GPIO pin setup and bit-bang read sequence |
| `test_mcp9600_driver` | Shared MCP9600/MCP9601 device ID handling, register transactions, fixed-point decoding, ADC sign extension, config bit preservation, alert/status and legacy ambient-resolution mapping |
| `test_bh1750_driver` | Shared BH1750 init command, first-measurement delay, I2C bus routing and two-byte lux decode |
| `test_simple_io_drivers` | Shared MCP23017/PCA9654E/PCF8574/74HC595/MCP3221/MCP4725 init sequences, per-pin/full-port write and read paths, invert/pull-up/IRQ configuration and instance-mutex coverage |
| `test_hd44780_driver` | Shared HD44780 GPIO init, 4-bit/8-bit command framing, cursor row offsets, CGRAM writes, print/write path and instance-mutex coverage |
| `test_hal_dma_pwm_audio` | Mock DMA PWM-audio lifecycle, callback dispatch, pause/resume and interpolation coverage |
| `test_dacless_driver` | Shared DACless config normalization, DMA and polling sample/block callback refill flow, ADC buffer, mute/unmute, interpolation helpers and mutex coverage |
| `test_tsc2007_driver` | Shared TSC2007 command-byte layout, 12-bit reply decode, touch-read sequence, stability rejection, bus routing and instance-mutex coverage |
| `test_stmpe610_driver` | Shared STMPE610 setup sequence, chip-ID probing, I2C/SPI/register transactions, FIFO decode, soft-SPI bit-bang path and instance-mutex coverage |
| `test_ads1x15_driver` | Shared ADS1X15 register config, ADS1115/ADS1015 conversion reads, gain/mode/data-rate mapping, comparator threshold writes and I2C clock forwarding |
| `test_hal_external_adc` | ADS1115 range setup, per-channel raw/scaled reads, out-of-range safety |
| `test_hal_gps` | location/speed/date/time inject, valid/updated/age flags, init reset, diagnostics getters |
| `test_hal_system` | delay/millis/micros behavior, watchdog flags, heap/chip-temp helpers, type-independent `hal_constrain`/`hal_map` (incl. equal-range guard), `COUNTOF`, `hal_u32_to_bytes_be`, `NONULL` |
| `test_hal_bits` | bit helper macros (`is_set`, `set_bit`, `clr_bit`, `bitSet`, `bitClear`, `bitRead`, `set_bit_v`, `clr_bit_v`) |
| `test_hal_wifi` | mode/hostname/RSSI/ping, IP/DNS/MAC inject, input validation |
| `test_hal_net` | shared endpoint/status shape, network limits, IPv4 literal/localhost/mock-DNS resolver behavior |
| `test_hal_littlefs` | mount/unmount flow, size stats, path exists/remove helpers, format success/failure behavior, missing-path remove semantics, input validation |
| `test_hal_sdlogger` | EEPROM-backed file numbering, buffered log flush/close, crash-report formatting, SD/open failure paths |
| `test_hal_udp` | begin/parse/read flow, handle-based multi-socket bind/RX/TX separation, chunked datagram reads, remote endpoint capture/reset-on-stop, beginPacket explicit/remote sender paths, write/endPacket behavior, input validation |
| `test_hal_tcp` | TCP client connect/send/recv/shutdown/close, listener bind/listen/accept, backlog/pool limits, readiness probes and accepted-socket independence |
| `test_hal_http_server` | HTTP route dispatch, query/body/header parsing, exact/prefix routes, response headers/body, HEAD handling, handler failures and invalid configuration |
| `test_hal_http_files` | Callback-backed HTTP file serving, MIME mapping, ETag/`If-None-Match`, raw PUT, multipart upload and path traversal rejection |
| `test_hal_websocket` | HTTP Upgrade handshake, `Sec-WebSocket-Accept`, masked text frames, broadcast, ping/pong, close callbacks and invalid handshakes |
| `test_hal_net_console` | Password-required TCP console start/auth flow, serial/debug mirroring to authenticated clients, multi-client broadcast, bidirectional command input, per-client replies and disconnect callbacks |
| `test_hal_net_commands` | JSON/text command registration and dispatch, HTTP route integration, WebSocket message integration, structured errors and API validation |
| `test_bsd_sockets` | BSD/POSIX adapter fd mapping, sockaddr translation, errno/EAI paths, TCP/UDP flow, nonblocking mode, `select()`, `getaddrinfo()` and `setsockopt()` |
| `test_bsd_sockets_c_compile` | C compile/link smoke test for socket headers, `netdb.h`, TCP/UDP client/server shapes, `fcntl()`, `select()`, `getaddrinfo()` and `setsockopt()` |
| `test_hal_wireguard` | IPv4 parser validation, byte-array and text WireGuard begin/begin_advanced/kick paths, peer-up endpoint reporting (`hal_wireguard_peer_up` + `hal_wireguard_peer_up_quick`), handshake kick trigger, input validation |
| `test_hal_mqtt` | server/connect flow, publish/subscribe/unsubscribe capture, callback dispatch from `hal_mqtt_loop`, invalid input guards |
| `test_hal_ota` | OTA config setters, begin/is_started flow, callback dispatch from injected start/progress/error/end events, callback replace/unregister flow, re-begin queue-clear behavior, invalid input guards |
| `test_hal_time` | timezone, NTP sync, Unix time, local time formatting |
| `test_hal_kv` | u32/blob CRUD, delete, unchanged-skip, GC, concurrent updates |
| `test_hal_crypto` | Base64/MD5/SHA-256/HMAC-SHA256/ChaCha20/ChaCha20-Poly1305 helper behavior, input validation, and ChaCha20 counter-wrap rejection regression checks |
| `test_wireguard_crypto_shared` | shared WireGuard crypto primitives (`crypto_equal/zero`, BLAKE2s, X25519, ChaCha20, ChaCha20-Poly1305 including RFC8439 IETF detached AEAD vectors) |
| `test_hal_soft_timer` | C wrapper coverage: create/begin/tick/abort/restart, table setup/tick helpers, delay/idle callback path, invalid input validation (`NULL` table / `count==0`) |
| `test_SmartTimers` | `tick`, callback firing, `abort`, `restart` (core behavior used by `hal_soft_timer_*`) |
| `test_pidController` | P output, output clamping, integral reset, stability detection (core behavior used by `hal_pid_controller_*`) |
| `test_multicoreWatchdog` | dual-core liveness gating, external reset path, pre-setup no-op safety |
| `test_tools` | utility coverage from `tools.cpp` using HAL mocks, including `debugInit`, `setDebugPrefixWithColon`, numeric/time/string helpers, and buffer-safe formatting helpers |

### Adding a new test suite

1. Create `tests/test_<name>.cpp` with `#include "utils/unity.h"`, Unity
   `setUp`, `tearDown`, `UNITY_BEGIN`, `RUN_TEST`, and `UNITY_END` calls.
2. Add `add_hal_test(test_<name>)` to `tests/CMakeLists.txt`.
    For suites that compile extra sources (for example `test_tools` and
    `test_multicoreWatchdog`), create a dedicated `add_executable(...)` entry.
3. Rebuild: `cmake --build build && ctest --test-dir build`.

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
