# ESP32 Porting Progress

Last updated: 2026-06-15 (initial feasibility analysis; no code changes yet)

## Goal

Establish whether JaszczurHAL can be extended with an ESP32 backend, describe
the required scope of work, identify blockers, and provide a phased
implementation plan.

---

## Feasibility verdict

**Technically feasible, substantial effort.** ESP32 is not a minor flag change.
The library must grow a fourth compile-time target (`HAL_TARGET_ESP32`) with its
own backend folder analogous to `src/hal/impl/stm32g474/` or
`src/hal/impl/arduino/`. The recommended carrier is **Arduino-ESP32** (the
official Espressif Arduino core), which gives the same `Wire`, `SPI`,
`Serial`, `WiFiUDP`, `ArduinoOTA` surface used by the RP2040 backend, making
approximately half the existing Arduino backend files reusable with only
header-guard changes.

---

## Architecture overview (current state relevant to ESP32)

### Target selection (`src/hal/hal_target.h`)

The library today accepts exactly one of three canonical targets at compile time:

```c
#define HAL_TARGET_RP2040      // Raspberry Pi RP2040 / arduino-pico
#define HAL_TARGET_STM32G474   // STM32G474 bare-metal
#define HAL_TARGET_MOCK        // host unit-test / simulation backend
```

Auto-detection keys off `ARDUINO_ARCH_RP2040`, `STM32G474xx`, and host
compiler detection. Selecting two targets or none is a `#error`.

Adding ESP32 requires:
- a new `#define HAL_TARGET_ESP32` token and `HAL_TARGET_IS_ESP32` boolean,
- a new auto-detect branch for `ARDUINO_ARCH_ESP32`,
- extending the "exactly one" enforcement sum,
- extending `HAL_TARGET_NAME` and `LED_BUILTIN` fallback.

### Backend folder layout

| Folder | Target | File count |
|---|---|---|
| `src/hal/impl/arduino/` | RP2040 | ~28 `.cpp` |
| `src/hal/impl/stm32g474/` | STM32G474 | ~36 files |
| `src/hal/impl/.mock/` | Host tests | ~34 files |

A new `src/hal/impl/esp32_arduino/` folder should be created; it starts empty
and grows module by module.

### Application entry (`src/hal_app_entry.cpp`)

The portable `app_start` / `app_task0` / `app_task1` contract already works on
RP2040 via `setup()` / `loop()` / `loop1()`. Arduino-ESP32 uses the same
`setup()` / `loop()` model, so the ESP32 entry path is trivially:

```cpp
#elif HAL_TARGET_IS_ESP32
void setup(void) { app_start(); }
void loop(void)  { app_task0(); }
#ifdef HAL_ENABLE_APP_TASK1
// ESP32 loop() always runs on core 1 (APP_CPU).
// A separate task pinned to core 0 can call app_task1().
static void task1_wrapper(void *) { for(;;) { app_task1(); } }
void setup1(void) {
    xTaskCreatePinnedToCore(task1_wrapper, "jh_app1",
        HAL_FREERTOS_TASK1_STACK, nullptr, 1, nullptr, 0);
}
#endif
```

FreeRTOS is always present on ESP32 (IDF ships it); there is no non-FreeRTOS
code path to maintain for this target.

### HAL_ENABLE_FREERTOS on ESP32

ESP32 IDF embeds FreeRTOS natively. The current `HAL_ENABLE_FREERTOS` guard in
`src/hal/hal_config.h` explicitly allows only RP2040 and STM32G474, so:
- The error message must be extended.
- RP2040-style FreeRTOS paths (`__FREERTOS`, `pico/mutex.h`) cannot be reused.
- For ESP32, `HAL_ENABLE_FREERTOS` can be trivially satisfied because
  `<FreeRTOS.h>` is always available from the IDF include tree; the guard
  becomes a no-op.

---

## What can be reused directly

### Portable shared drivers (work on ESP32 with no changes)

Everything under `src/hal/impl/shared/` that is guarded by
`HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK`
compiles unchanged once `HAL_TARGET_IS_ESP32` is added to those guards
(46 such guard instances identified in the current codebase):

| Shared module | API dependency | Portable? |
|---|---|---|
| `ads1x15/` | `hal_i2c`, `hal_system` | Yes |
| `bh1750/` | `hal_i2c`, `hal_system` | Yes |
| `digipot/mcp401x`, `digipot/max5395` | `hal_i2c`, `hal_system` | Yes |
| `display/` (ILI9341, ST77xx, SSD1306, GFX) | `hal_spi`, `hal_i2c`, `hal_gpio`, `hal_system` | Yes |
| `ds18b20/`, `onewire/` | `hal_gpio`, `hal_system` | Yes |
| `ds3231/` | `hal_i2c`, `hal_system` | Yes |
| `gps/` (NMEA parser) | `hal_uart`/`hal_swserial`, `hal_system` | Yes |
| `max6675/` | `hal_gpio` bit-bang | Yes |
| `mcp2515/` | `hal_spi`, `hal_gpio`, `hal_sync` | Yes |
| `mcp9600/` | `hal_i2c`, `hal_system` | Yes |
| `neopixel/` (GRB engine only) | transport hook | Partial (RP2040 transport uses PIO) |
| `pcf8563/` | `hal_i2c`, `hal_system` | Yes |
| `pga2311/` | `hal_spi`, `hal_gpio` | Yes |
| `wireguard/crypto/` | pure C, no HAL | Yes |

### HAL-level portable modules (work unchanged)

All modules in `src/hal/` whose implementation is solely in `src/hal/hal_*.cpp`
(no per-target `impl/` dispatch) are already portable:
`hal_config`, `hal_can_util`, `hal_crypto`, `hal_pga2311`, `hal_digipot`,
`hal_kv`, `hal_modem_at`, `hal_simcom_a76xx`, `hal_timer_ext`,
`hal_soft_timer`, `hal_pid_controller`.

### Arduino backend files that port with header-guard changes only

The following files under `src/hal/impl/arduino/` use only standard Arduino
APIs (`Wire`, `SPI`, `Serial`, `WiFi`, `WiFiUDP`, `ArduinoOTA`). Their only
RP2040 dependency is the `#if HAL_TARGET_IS_RP2040` outer guard. Once ESP32 is
added to that guard (or each file gains a sibling in `esp32_arduino/`), they
compile as-is under Arduino-ESP32:

| File | Used Arduino API |
|---|---|
| `hal_adc.cpp` | `analogRead` |
| `hal_can.cpp` | generic CAN facade + `hal_spi` / MCP2515 shared backend |
| `hal_dac.cpp` | (`dacWrite` on ESP32 - minor edit needed) |
| `hal_eeprom.cpp` | `EEPROM`, `hal_i2c` |
| `hal_gps.cpp` | `hal_uart` / `hal_swserial` |
| `hal_i2c.cpp` | `Wire` |
| `hal_littlefs.cpp` | `LittleFS` (available in Arduino-ESP32) |
| `hal_mqtt.cpp` | `PubSubClient` |
| `hal_ota.cpp` | `ArduinoOTA` |
| `hal_pwm.cpp` | `analogWrite` |
| `hal_rtc.cpp` | `hal_i2c` (shared DS3231/PCF8563 drivers) |
| `hal_serial.cpp` | `Serial` |
| `hal_swserial.cpp` | `SoftwareSerial` |
| `hal_thermocouple.cpp` | shared MCP9600/MAX6675 drivers |
| `hal_time.cpp` | `configTime`, NTP |
| `hal_uart.cpp` | `HardwareSerial` |
| `hal_udp.cpp` | `WiFiUDP` |
| `hal_wifi.cpp` | `WiFi` |
| `hal_wireguard.cpp` | WireGuard library (Pico W version - needs ESP32 equivalent) |

Estimated count: ~18 files need only guard extension or minor API adaptation.

---

## What requires new implementation for ESP32

### Hard RP2040 SDK dependencies — must be rewritten

The following files use Raspberry Pi Pico SDK headers or APIs that have no
equivalent on ESP32 and cannot be conditionally compiled:

| File | RP2040-specific API | ESP32 replacement |
|---|---|---|
| `hal_sync.cpp` | `pico/mutex.h` (`mutex_t`, `mutex_init`, `mutex_enter_blocking`), `hardware/sync.h` (`save_and_disable_interrupts`, `restore_interrupts`), `pico/platform.h` (`get_core_num`) | `portENTER_CRITICAL` / `portEXIT_CRITICAL` (IDF), `xSemaphoreCreateMutex` |
| `hal_system.cpp` | `pico/time.h` (`time_us_64`, `busy_wait_us`, `busy_wait_ms`) | `esp_timer_get_time()`, `ets_delay_us()`, `esp_rom_delay_us()` |
| `hal_timer.cpp` | `pico/time.h` (`alarm_pool_create`, `alarm_pool_add_alarm_in_us`, `hardware_alarm_claim_unused`) | `esp_timer_create` / `esp_timer_start_once` |
| `hal_gpio.cpp` | `hardware/irq.h` (`PICO_DEFAULT_IRQ_PRIORITY`), `irq_set_priority` | `gpio_install_isr_service`, `esp_intr_alloc`, `GPIO_IS_VALID_GPIO` |
| `hal_pwm_freq.cpp` | `hardware/clocks.h`, `hardware/pwm.h` (`pwm_gpio_to_slice_num`, `pwm_init`, `pwm_set_gpio_level`) | LEDC peripheral (`ledc_timer_config`, `ledc_channel_config`, `ledc_set_duty`) |
| `hal_rgb_led.cpp` | PIO machine (`pio_claim_free_sm_and_add_program_for_gpio_range`, `ws2812_program_init`, `pio_sm_put_blocking`) | RMT peripheral (`rmt_new_tx_channel`, `rmt_transmit`) or `Adafruit_NeoPixel` |
| `hal_pcnt.cpp` | GPIO interrupt ISR trampolines (RP2040 approach — functional but not using PCNT hardware) | ESP32 PCNT peripheral (`pcnt_unit_config`, `pcnt_channel_config`) or interrupt-based fallback |
| `hal_i2c_slave.cpp` | `pico/critical_section.h` | IDF I2C slave driver |
| `drivers/rp2040/rp2040_system.cpp` | `pico/bootrom.h`, `pico/stdlib.h`, `pico/unique_id.h`, `hardware/watchdog.h` | `esp_efuse_mac_get_default`, `esp_restart`, IDF watchdog |
| `drivers/rp2040/rp2040_fault.cpp` | `pico/stdlib.h`, `hardware/watchdog.h`, `hardware/structs/watchdog.h` | IDF `esp_task_wdt`, `esp_reset_reason` |

**Count: 11 files require new ESP32 implementations.** These would live as
`src/hal/impl/esp32_arduino/` plus `src/hal/impl/esp32_arduino/drivers/esp32/`.

### SPI backend

`hal_spi.cpp` uses `SPIClassRP2040` (arduino-pico-specific class). Arduino-ESP32
uses the standard `SPIClass`. The replacement is straightforward: replace
`SPIClassRP2040&` with `SPIClass&` and initialise from the platform `SPI` /
`SPI1` singletons. No logic changes needed.

### WireGuard

The current implementation in `hal_wireguard.cpp` uses
`frameworks/arduino-wireguard-pico-w/` which is Pico W-specific. An ESP32
WireGuard library (e.g. `ciniml/WireGuard-ESP32` or `circularuniverse/ESP32-WireGuard`)
would need to be evaluated and bundled analogously under `frameworks/`. The
shared `impl/shared/wireguard/crypto/` code is pure C and works on ESP32
without changes.

### DAC

RP2040 has no true DAC peripheral (`hal_dac.cpp` returns `hal_dac_is_supported() == false`).
ESP32 has a real 8-bit DAC on GPIO25/GPIO26. The ESP32 backend can provide a
minimal `dacWrite(pin, value)` implementation.

---

## Build system changes required

### `src/hal/hal_target.h`

```c
// Auto-detect
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
#  define HAL_TARGET_ESP32 1
// Normalise
#if defined(HAL_TARGET_ESP32)
#  define HAL_TARGET_IS_ESP32 1
#else
#  define HAL_TARGET_IS_ESP32 0
#endif
// Exactly-one sum: +1
// LED_BUILTIN fallback: GPIO2 (built-in LED on most ESP32 devboards)
```

### `src/hal/hal_config.h`

- Extend the `HAL_ENABLE_FREERTOS` error to permit `HAL_TARGET_IS_ESP32`
  (FreeRTOS is always present; the check becomes a no-op for this target).

### `src/hal_app_entry.cpp`

- Add the `#elif HAL_TARGET_IS_ESP32` branch described above.

### `library.properties`

```properties
architectures=rp2040,esp32
```

### Arduino static library build

- Add `scripts/build_esp32_lib.sh` and `esp32_lib/CMakeLists.txt` analogous to
  the RP2040 script and `rp2040_lib/CMakeLists.txt`.
- The ESP32 Arduino toolchain is `xtensa-esp32-elf-gcc` (or
  `xtensa-esp32s2-elf-gcc` / `riscv32-esp-elf-gcc` for S2/C3) bundled under
  `~/.arduino15/packages/esp32/`.
- `ARDUINO_CHIP` defaults to `esp32`, with variants for `esp32s2`, `esp32s3`,
  `esp32c3` (RISC-V).

### `examples/CMakeLists.txt`

- Add `esp32` to `JH_EXAMPLE_TARGET` option list.
- Add `_jh_add_esp32_example()` function (arduino-cli compile, similar to
  `_jh_add_rp2040_example()`).

### `examples/CMakePresets.json`

- Add `esp32` configure and build presets.

### Shared guards

All 46 occurrences of `HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474` in
`src/hal/impl/shared/` and `src/hal/` need `|| HAL_TARGET_IS_ESP32` added
(grep pattern: `HAL_TARGET_IS_RP2040 \|\| HAL_TARGET_IS_STM32G474`).

---

## Phased implementation plan

### Phase 1 — Scaffolding (no logic, just compiles)

1. Extend `hal_target.h` with `HAL_TARGET_ESP32`.
2. Extend `hal_config.h` FreeRTOS guard.
3. Extend `hal_app_entry.cpp` with ESP32 entry.
4. Create empty `src/hal/impl/esp32_arduino/` with stub `.cpp` files
   (every file returns sensible no-op / "not supported" values).
5. Add `esp32_lib/CMakeLists.txt` and `scripts/build_esp32_lib.sh`.
6. Update `library.properties` to include `esp32`.
7. Add guard extensions to all 46 shared driver locations.

**Deliverable:** ESP32 firmware compiles (empty stubs). All existing tests stay green.

### Phase 2 — Core HAL (MVP hardware bring-up)

Priority order based on dependency chain:

1. `hal_system` — `hal_millis`, `hal_micros`, `hal_delay_ms`, `hal_delay_us`,
   `hal_in_isr`, `hal_get_free_heap`, watchdog (IDF `esp_task_wdt`), reset
   reason (`esp_reset_reason`), device UID (`esp_efuse_mac_get_default`).
2. `hal_sync` — `hal_mutex_*` (FreeRTOS `xSemaphoreCreateMutex`),
   `hal_critical_section_*` (`portENTER_CRITICAL` / `portEXIT_CRITICAL`).
3. `hal_gpio` — `pinMode`, `digitalWrite`, `digitalRead`, `attachInterrupt`;
   IRQ priority via `esp_intr_alloc` priority parameter.
4. `hal_serial` — `Serial.print/println/read/available` (identical to RP2040).
5. `hal_uart` — `HardwareSerial` UART1/UART2 (identical to RP2040 file).
6. `hal_adc` — `analogRead` (identical to RP2040 file).
7. `hal_i2c` — `Wire.begin/beginTransmission/write/endTransmission/requestFrom/read`
   (identical to RP2040 file with guard change).
8. `hal_spi` — replace `SPIClassRP2040` with `SPIClass` (minimal change).
9. `hal_timer` — `esp_timer_create` / `esp_timer_start_once` /
   `esp_timer_stop` / `esp_timer_delete` in place of pico alarm pool.

**Deliverable:** GPIO blink, debug serial, I2C scan, SPI device examples run on ESP32.

### Phase 3 — Connectivity stack

1. `hal_wifi` — `WiFi.h` (Arduino-ESP32; API is identical to Pico W, guard
   change only).
2. `hal_udp` — `WiFiUDP` (identical; guard change only).
3. `hal_mqtt` — PubSubClient (identical; guard change only).
4. `hal_ota` — `ArduinoOTA` (identical; guard change only).
5. `hal_time` — `configTime` + `getLocalTime` (identical; guard change only).

**Deliverable:** WiFi, MQTT, OTA, NTP examples run on ESP32.

### Phase 4 — Remaining Arduino-portable modules

1. `hal_pwm` — `analogWrite` or LEDC (`ledcSetup`, `ledcAttachPin`, `ledcWrite`).
2. `hal_pwm_freq` — LEDC timer (`ledc_timer_config`, `ledc_channel_config`).
3. `hal_rgb_led` — RMT peripheral or `Adafruit_NeoPixel` for ESP32.
4. `hal_pcnt` — ESP32 PCNT hardware peripheral (dedicated hardware, better
   than RP2040's interrupt-based approach).
5. `hal_swserial` — `SoftwareSerial` (Arduino-ESP32 provides it; guard change).
6. `hal_eeprom` — Arduino-ESP32 `EEPROM.h` (emulated in flash, identical API).
7. `hal_littlefs` — `LittleFS.h` (available in Arduino-ESP32; guard change).
8. `hal_gps` — `hal_uart`/`hal_swserial` transport (identical; guard change).
9. `hal_dac` — `dacWrite(pin, value)` for GPIO25/GPIO26 (real 8-bit DAC).
10. `hal_rtc` — shared DS3231/PCF8563 drivers over ESP32 `hal_i2c` (identical).
11. `hal_thermocouple` — shared MCP9600/MAX6675 drivers (identical).
12. `hal_i2c_slave` — IDF I2C slave driver (full rewrite needed).

### Phase 5 — Shared driver guard widening

All 46 guard sites in `src/hal/impl/shared/`:

```
// before:
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)
// after:
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || \
     HAL_TARGET_IS_ESP32  || HAL_TARGET_IS_MOCK)
```

### Phase 6 — WireGuard, SDLOG, advanced modules

1. `hal_wireguard` — evaluate and bundle an ESP32 WireGuard Arduino library.
2. `hal_sdlogger` — SD library is identical under Arduino-ESP32.
3. `hal_can` — facade/dispatch should stay target-local; MCP2515 shared
   backend works once SPI init and `HAL_ENABLE_MCP2515` are wired.
4. `hal_display` — shared display driver works; SPI/GPIO init differs.

---

## Module effort summary

| Module | Effort | Notes |
|---|---|---|
| `hal_target.h` + `hal_config.h` | Low | ~30 lines of new macro/guard code |
| `hal_app_entry.cpp` | Low | ~15 lines |
| `hal_system` | Medium | IDF watchdog, reset reason, UID, timer APIs |
| `hal_sync` | Medium | FreeRTOS critical-section model differs from pico SDK |
| `hal_gpio` | Low-Medium | IRQ priority mechanism differs |
| `hal_serial` | Low | Guard change only |
| `hal_uart` | Low | Guard change only |
| `hal_adc` | Low | Guard change only |
| `hal_i2c` | Low | Guard change only |
| `hal_spi` | Low | `SPIClassRP2040` → `SPIClass` |
| `hal_timer` | Medium-High | Full rewrite using `esp_timer` API |
| `hal_pwm` | Low | `analogWrite` or LEDC |
| `hal_pwm_freq` | Medium | LEDC timer/channel config differs from pico PWM slice |
| `hal_rgb_led` | Medium | PIO → RMT or NeoPixel library |
| `hal_pcnt` | Medium | ESP32 has dedicated PCNT HW (better than RP2040 approach) |
| `hal_wifi` | Low | Guard change only |
| `hal_udp` | Low | Guard change only |
| `hal_mqtt` | Low | Guard change only |
| `hal_ota` | Low | Guard change only |
| `hal_time` | Low | Guard change only |
| `hal_swserial` | Low | Guard change only |
| `hal_eeprom` | Low | Guard change only |
| `hal_littlefs` | Low | Guard change only |
| `hal_gps` | Low | Guard change only |
| `hal_dac` | Low | Real DAC on ESP32; minor new implementation |
| `hal_rtc` | Low | Shared drivers; guard change only |
| `hal_thermocouple` | Low | Shared drivers; guard change only |
| `hal_i2c_slave` | High | Full IDF rewrite |
| `hal_wireguard` | High | New library evaluation + integration |
| `hal_sdlogger` | Low | SD library identical; guard change |
| Shared driver guards (46 sites) | Low | Mechanical text replacement |
| Build system (esp32_lib, scripts, presets) | Medium | Analogous to RP2040 build system |
| Examples (2-3 minimal ESP32 examples) | Low-Medium | Reuse existing app.c |

---

## Known risks and open questions

### 1. Dual-core model (ESP32 has two Xtensa cores)

RP2040 uses `core0`/`core1` terminology with `get_core_num()`. ESP32 uses
`APP_CPU` (core 0) and `PRO_CPU` (core 1) terminology. The `HAL_ENABLE_APP_TASK1`
contract needs to be decided:
- **Option A:** `app_task1` pinned to core 0 (`APP_CPU`) via `xTaskCreatePinnedToCore`.
- **Option B:** `app_task1` runs on the same core as `app_task0` in a second FreeRTOS task.

The per-core critical section arrays in `hal_sync.cpp` (`s_critical_depth[2]`,
`s_saved_irq[2]`) can use `xPortGetCoreID()` instead of `get_core_num()`.

### 2. ESP32 variant fragmentation

ESP32 is a family:
- ESP32 (Xtensa LX6, dual-core)
- ESP32-S2 (Xtensa LX7, single-core)
- ESP32-S3 (Xtensa LX7, dual-core)
- ESP32-C3, C6, H2 (RISC-V, single-core)
- ESP32-P4 (RISC-V, dual-core, high-performance)

Each has different toolchains, peripherals (PCNT present on ESP32/S3, absent
on C3), and memory layouts. The initial target should be **ESP32 (original)**
and **ESP32-S3** (most common in production). The PCNT module is not available
on ESP32-C3/C6/H2 and must be stubbed there.

### 3. `HAL_ENABLE_FREERTOS` semantics on ESP32

FreeRTOS is always linked on ESP32 IDF. The flag `HAL_ENABLE_FREERTOS` changes
meaning:
- On RP2040: opt-in, changes mutex/delay/idle paths significantly.
- On ESP32: FreeRTOS is always present; the flag can be a no-op or can be used
  to gate FreeRTOS-aware spinlocks.

Recommendation: for the initial ESP32 backend, always use FreeRTOS APIs
(no non-FreeRTOS code path needed), but keep `HAL_ENABLE_FREERTOS` as an
optional documentation signal.

### 4. LittleFS flash layout

ESP32 LittleFS uses IDF partition tables, while RP2040 uses a flat flash offset.
`hal_littlefs.cpp` currently passes `FS_START`/`FS_END` which are RP2040 flash
addresses. The ESP32 variant must use the `LittleFS.h` from `lorol/LittleFS_esp32`
or the built-in `esp_littlefs` component, which derive the partition from the
partition table rather than explicit addresses.

### 5. Watchdog API differences

RP2040 watchdog uses `watchdog_enable(ms, pause_on_debug)` from pico SDK.
ESP32 IDF provides both the task watchdog (`esp_task_wdt_*`) and the interrupt
watchdog. For a simple "reset if not fed within N ms" use-case,
`esp_task_wdt_init` + `esp_task_wdt_add(NULL)` + `esp_task_wdt_reset()` is the
closest equivalent.

### 6. WireGuard library

The current `hal_wireguard.cpp` depends on
`frameworks/arduino-wireguard-pico-w/`. That library relies on lwIP internals
specific to arduino-pico. Available ESP32 alternatives:
- `ciniml/WireGuard-ESP32` (uses ESP-IDF lwIP)
- `circularuniverse/ESP32-WireGuard`

The shared crypto layer (`impl/shared/wireguard/crypto/`) is pure C and needs
no change.

---

## Files to create for a Phase 1 skeleton

```
src/hal/impl/esp32_arduino/
    hal_adc.cpp
    hal_can.cpp
    hal_dac.cpp
    hal_eeprom.cpp
    hal_gpio.cpp
    hal_gps.cpp
    hal_i2c.cpp
    hal_i2c_slave.cpp
    hal_littlefs.cpp
    hal_mqtt.cpp
    hal_ota.cpp
    hal_pcnt.cpp
    hal_pwm.cpp
    hal_pwm_freq.cpp
    hal_rgb_led.cpp
    hal_rtc.cpp
    hal_serial.cpp
    hal_spi.cpp
    hal_swserial.cpp
    hal_sync.cpp
    hal_system.cpp
    hal_thermocouple.cpp
    hal_time.cpp
    hal_timer.cpp
    hal_uart.cpp
    hal_udp.cpp
    hal_wifi.cpp
    hal_wireguard.cpp
    drivers/esp32/
        esp32_system.h
        esp32_system.cpp
        esp32_fault.h
        esp32_fault.cpp
esp32_lib/
    CMakeLists.txt
scripts/
    build_esp32_lib.sh
```

---

## Quick reference: key differences RP2040 vs ESP32

| Aspect | RP2040 | ESP32 |
|---|---|---|
| CPU | Cortex-M0+ dual-core | Xtensa LX6 dual-core (or LX7/RISC-V in variants) |
| FreeRTOS | Optional (`__FREERTOS` arduino-pico flag) | Always present (IDF) |
| Mutex | `pico/mutex.h` (bare metal) / FreeRTOS sem | FreeRTOS `xSemaphoreCreateMutex` |
| Critical section | `save_and_disable_interrupts` / `restore_interrupts` | `portENTER_CRITICAL` / `portEXIT_CRITICAL` (per-core spinlock) |
| Hardware timer | `alarm_pool` + `hardware_alarm_*` | `esp_timer_create` / `esp_timer_start_once` |
| PWM freq | `hardware/pwm.h` slice/channel | LEDC peripheral |
| RGB LED (WS2812) | PIO state machine | RMT transmit peripheral |
| Pulse counter | GPIO interrupt (software) | Dedicated PCNT peripheral |
| Watchdog | `hardware/watchdog.h` | `esp_task_wdt_*` |
| Device UID | `pico_get_unique_board_id` | `esp_efuse_mac_get_default` |
| Reset reason | `watchdog_hw->scratch[0]` retained | `esp_reset_reason()` |
| DAC | Not present | 8-bit DAC on GPIO25/GPIO26 |
| I2C slave | `pico/critical_section.h` | IDF I2C slave driver |
| SPI class | `SPIClassRP2040` | `SPIClass` (standard Arduino) |
| WiFi/network | arduino-pico lwIP | WiFi.h (Arduino-ESP32 / ESP-IDF lwIP) |
| Flash FS | LittleFS with flat offset | LittleFS with partition table |
| WireGuard library | arduino-wireguard-pico-w | WireGuard-ESP32 (evaluate) |
