# STM32G474 Porting Progress

Last updated: 2026-06-05 (shared MCP2515 CAN driver added)

## Goal
Provide a new `STM32G474` target skeleton for JaszczurHAL with no dependency on
the Arduino layer, so the STM32 backend can be developed in parallel with the
existing Arduino/RP2040 backend.

## Delivered scope ("2)" - skeleton)

### 1. New static-library build path for STM32
Added files:
- `stm32_lib/CMakeLists.txt`
- `stm32_lib/toolchain_stm32g474.cmake`
- `build_stm32_lib.sh`

What they provide:
- a separate CMake target `JaszczurHAL` for STM32G474,
- a dedicated `arm-none-eabi-*` toolchain,
- a convenient build script analogous to the existing `build_arduino_lib.sh`.

### 2. New source backend `impl/stm32g474`
Added files:
- `src/hal/impl/stm32g474/hal_sync.cpp`
- `src/hal/impl/stm32g474/hal_system.cpp`
- `src/hal/impl/stm32g474/hal_gpio.cpp`
- `src/hal/impl/stm32g474/hal_adc.cpp`
- `src/hal/impl/stm32g474/hal_pwm.cpp`
- `src/hal/impl/stm32g474/hal_timer.cpp`
- `src/hal/impl/stm32g474/hal_serial.cpp`
- `src/hal/impl/stm32g474/hal_spi.cpp`
- `src/hal/impl/stm32g474/hal_i2c.cpp`
- `src/hal/impl/stm32g474/hal_uart.cpp`
- `src/hal/impl/stm32g474/hal_dac.cpp`
- `src/hal/impl/stm32g474/hal_pcnt.cpp`
- `src/hal/impl/stm32g474/hal_gps.cpp`
- `src/hal/impl/stm32g474/hal_time.cpp` (always-available `hal_time_from_components`)
- `src/hal/impl/stm32g474/port/` (startup, system init, debug UART, fault capture, atomic stubs, register map)
- `src/hal/impl/stm32g474/drivers/stm32g474/` (SoC-specific fault and system drivers)

Nature of the implementation:
- a minimal, safe skeleton for the later switch to STM32 HAL/LL,
- no dependency on Arduino libraries (non-Arduino builds use local compatibility
  headers where Arduino-origin drivers need common types),
- core modules are moving from placeholders to register-level G474 backends,
- the code carries TODO markers wherever the STM32 wiring should ultimately go.

### 3. Default STM32 feature profile (initial)
`stm32_lib/CMakeLists.txt` enables by default:
- `HAL_ENABLE_I2C`
- `HAL_ENABLE_SPI`
- `HAL_ENABLE_UART`
- `HAL_ENABLE_DAC`
- `HAL_ENABLE_PCNT`
- `HAL_ENABLE_MCP401X`
- `HAL_ENABLE_MAX5395`
- `HAL_ENABLE_MCP9600`
- `HAL_ENABLE_MAX6675`
- `HAL_ENABLE_EXTERNAL_ADC`
- `HAL_ENABLE_CAN`
- `HAL_ENABLE_GPS`

This narrows the scope to the backend "core" and simplifies the first porting
stages.

## Validation

### OK
- The standard repo build/test stayed green:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- The new `stm32_lib` build on the host compiler (syntax/dependency
  sanity-check) passed:
  - `cmake -S stm32_lib -B build_stm32_host`
  - `cmake --build build_stm32_host`
- The real ARM target now builds end-to-end (the `JH_STM32G474_HW` hardware
  paths compile) once the Arm toolchain is installed:
  - `./build_stm32_lib.sh --clean`
- **Examples build system** - all 12 STM32G474-targeted examples
  (01_blink, 02_debug_helper, 03_soft_timer_table, 04_crypto, 14_uart,
  17_pid_controller, 19_timer_ext, 20_i2c_scan, 21_adc_read, 22_gps_uart,
  23_external_adc_ads1115, 24_can_mcp2515)
  compile to ELF/BIN/HEX without errors using the unified CMake build:
  ```bash
  cmake -S examples -B build_examples_stm32 \
        -DJH_EXAMPLE_TARGET=stm32g474 \
        -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"
  cmake --build build_examples_stm32
  ```

## How to build for the real STM32G474
After installing the Arm toolchain:

```bash
./build_stm32_lib.sh --clean
```

Optionally:

```bash
./build_stm32_lib.sh --clean \
  -p /path/to/project \
  -D HAL_DISABLE_ASSERTS
```

## Real backends delivered (beyond the skeleton)
The following modules are real, register-level backends under
`JH_STM32G474_HW` (no longer placeholders):

- `hal_gpio` - direction + digital read/write (pin id = `port*16 + pin`).
- `hal_i2c` - I2C1 master (SCL=PB8, SDA=PB9, AF4, 100 kHz, AUTOEND).
- `hal_dac` - DAC1, 12-bit (ch0 -> PA4, ch1 -> PA5).
- `hal_pcnt` - hardware pulse counter on TIM2 (external clock mode).
- `hal_adc` - **ADC1**, single-ended, polled, one regular conversion per
  `hal_adc_read()`. The first read lazily brings ADC1 up (ADC12 clock,
  internal regulator + startup wait, single-ended calibration, enable) and
  routes the requested pin to analog mode on demand. ADC kernel clock is
  HCLK/1, so the HSI16 bring-up clock gives a 16 MHz ADC clock. Pin -> channel
  map per RM0440: PA0..PA3 -> IN1..IN4, PB0 -> IN15, PB1 -> IN12, PB11 -> IN14,
  PB12 -> IN11, PB14 -> IN5, PC0..PC3 -> IN6..IN9.
- `hal_spi` - **SPI1/SPI2**, hardware register-level polling transfers (8-bit
  full-duplex), Arduino-style transaction API, AF5 pin setup, software NSS,
  SPI modes 0-3, MSB/LSB order, clock prescaler selection. Default pins:
  bus 0 -> SPI1 PA6/PA7/PA5, bus 1 -> SPI2 PB14/PB15/PB13.
- `hal_can` - **MCP2515** via the shared HAL-only driver
  (`impl/shared/mcp2515/mcp2515_driver.*`) over `hal_spi`, `hal_gpio`,
  `hal_system` and `hal_sync`.
- `hal_display` - **ILI9341** plus **ST7735/ST7789/ST7796S** via shared
  HAL-only SPI/GPIO drivers (`impl/shared/display/ili9341_driver.*`,
  `impl/shared/display/st77xx_driver.*`) and **SSD1306** via the shared HAL I2C
  driver (`impl/shared/display/ssd1306_driver.*`). Rendering (geometry, text,
  bitmaps) runs through the portable GFX engine (`impl/shared/display/jh_gfx.*`).
  The whole stack lives in one shared `impl/shared/display/hal_display.cpp`
  used by both STM32G474 and RP2040. Init, rotation, inversion, fill, bitmap
  writes, geometry and text rendering are present; DMA/bulk-write optimization
  remains future work.
- `hal_thermocouple` - **MCP9600/MCP9601** via the shared HAL I2C driver
  (`impl/shared/mcp9600/mcp9600_driver.*`) and **MAX6675** via the shared Arduino-free
  GPIO bit-bang driver (`impl/shared/max6675/max6675_driver.*`). The same driver logic
  is used by STM32G474 and RP2040.
- `hal_serial` - debug USART2 (ST-Link VCP) for `hal_debug_*` output.
- `hal_uart` - USART1 hardware UART (TX/RX, configurable baud, used as GPS
  transport).
- `hal_sync` - spinlock mutex plus PRIMASK-backed critical sections on real
  Cortex-M builds; host sanity builds keep critical sections as no-ops.

### Still placeholders (compile but use RAM-only state)
- `hal_pwm` - stores values in array, no timer output.
- `hal_timer` - slot management only, no hardware timer/IRQ.

## Driver pool analysis - portability to STM32G474

Assessment of the existing JaszczurHAL driver pool and how much of it can be
reused on the STM32 backend (RP2040 is the only fully-driven target today).

### Two distinct classes of "driver"

**1. Portable HAL-level drivers** - public facades live directly in `src/hal/`,
with reusable chip/protocol logic under `src/hal/impl/shared/` when useful.
They are guarded only by `HAL_ENABLE_*` (not by target) and written against the
HAL's own API (`hal_i2c`, `hal_serial`, `hal_sync`):
`hal_digipot`, `hal_crypto`, `hal_kv`, `hal_modem_at`, `hal_simcom_a76xx`,
`hal_pid_controller`, `hal_soft_timer`, `hal_config`.

`src/hal/hal_digipot.cpp` plus `src/hal/impl/shared/digipot/` is the reference
pattern: the public module owns handles/locking/dispatch, while chip drivers own
validation, init and I/O sequences. **These already work on STM32** as long as
the underlying bus HAL exists.

**2. Vendor Arduino libraries** - in `src/hal/impl/arduino/drivers/`:
`Adafruit_NeoPixel`, `DS3231`, `PCF8563`.
**All depend on `Arduino.h` / `Wire` / `SPI`.** They are wrapped by the Arduino
device-HALs (`hal_thermocouple.cpp`, `hal_rtc.cpp`, `hal_can.cpp`, ...), each
guarded by `#if HAL_TARGET_IS_RP2040` and `#include`-ing `<Wire.h>`/`<SPI.h>`
directly. **These cannot be ported 1:1** - the realistic path is to rewrite the
device logic as portable `src/hal/` drivers (the digipot pattern).

### Deciding factor: bus state on STM32

| Bus  | STM32G474 status | Consequence |
|------|------------------|-------------|
| I2C  | Full Wire-style API (`begin_transmission`/`write`/`end`/`request_from`/`read`) in `impl/stm32g474/hal_i2c.cpp` | I2C device drivers are portable today |
| SPI  | **Hardware SPI1/SPI2** with Arduino-style transaction + transfer API; non-Arduino builds provide `<SPI.h>` (`SPIClass`/`SPISettings`) backed by `hal_spi_*` | SPI device drivers can now be ported behind the HAL / Arduino-compatible shim |
| UART | USART1 hardware (TX/RX, configurable baud) - used as GPS transport | UART-based peripherals are portable today |

SPI is no longer blocked at the bus layer. The first STM32 implementation is
polling-based rather than DMA, but it is hardware-backed and matches the
Arduino driver surface closely enough for MCP2515 and display-driver bring-up.
MCP9600/MCP9601 and MAX6675 are handled separately by shared HAL-level drivers.
ADS1X15/ADS1115 is also now handled by a shared HAL-level driver used by both
RP2040 and STM32G474.
OneWire and DS18B20 now follow the same shared HAL-level path over
`hal_gpio` + `hal_time`.
Default
G474 pins: bus 0 -> SPI1 PA6/PA7/PA5, bus 1 -> SPI2 PB14/PB15/PB13; CS remains
a normal GPIO owned by each driver.

### Module gap on STM32
Modules with Arduino + mock impl but no `impl/stm32g474`:
`eeprom, i2c_slave, littlefs, mqtt,
ota, pwm_freq, rgb_led, swserial, udp, wifi, wireguard`.

### Portability tiers

**🟢 Easy - mirror the digipot pattern (rewrite vendor logic on `hal_i2c`):**
- `hal_rtc` (PCF8563, DS3231) - plain I2C register access

We do not port the Adafruit/vendor libraries; we extract their register maps and
write a portable driver on `hal_i2c`. Low risk, existing coverage in `tests/`.

`hal_external_adc` / ADS1115 has completed this path and now lives in
`src/hal/impl/shared/ads1x15/ads1x15_driver.*`.

`hal_onewire` and `hal_ds18b20` have completed this path and now live in
`src/hal/impl/shared/onewire/`.

`hal_display` has completed this path: ILI9341, ST7735/ST7789/ST7796S (SPI) and
SSD1306 (I2C) now share one HAL-only stack under `src/hal/impl/shared/display/`,
used identically by STM32G474 and RP2040. MAX6675 is handled separately by the
shared bit-bang HAL GPIO driver. The remaining display work is a bulk-write/DMA
evaluation if TFT throughput needs it.

**🟡 `hal_rgb_led`** (NeoPixel/WS2812) - 800 kHz critical timing; on STM32 this is
a rewrite (PWM+DMA or SPI), not a library port.

**🔴 Not a "driver port" - different effort entirely:**
- `hal_wifi / hal_udp / hal_mqtt / hal_wireguard` - tied to Pico-W (CYW43) +
  PubSubClient + `arduino-wireguard-pico-w`. STM32G474 has no radio -> not a port
  but a different transport (e.g. via the already-portable SIMCom modem).
  Effectively N/A for a bare G474.
- `hal_littlefs / hal_eeprom / hal_ota` - STM32 flash/storage specific, not
  vendor-driver ports.
- `hal_swserial / hal_i2c_slave / hal_pwm_freq` - STM32 peripheral work.

### Recommended order
1. **I2C drivers** - Via the shared/portable HAL pattern: rtc (PCF8563/DS3231) is now **DONE**.
2. **Display bulk-write path** over SPI, then decide whether DMA is worth adding for TFT throughput.
3. Remainder (rgb_led, storage, connectivity) - separate decisions, not pure ports.

## Remaining work for the next stages
1. Replace the remaining `impl/stm32g474` placeholders with real STM32 register code:
   - `hal_pwm` - needs TIM output compare / PWM mode on a general-purpose timer.
   - `hal_timer` - hardware timer alarms / periodic IRQ (TIM6/TIM7 or similar).
2. `hal_system` full implementation (watchdog, MCU UID via OTP, reboot reason via RCC->CSR).
3. On-silicon validation on Nucleo-G474RE for all register-level backends.
4. FreeRTOS integration (enables true `app_task1` parallelism on STM32).
5. Add hardware smoke-tests (GPIO/UART/I2C/SPI/ADC/CAN) on an STM32G474 board.
6. Gradually unlock further modules (`HAL_ENABLE_*`) as the port progresses.
