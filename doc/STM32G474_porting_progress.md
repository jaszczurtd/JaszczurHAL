# STM32G474 Porting Progress

Last updated: 2026-06-23 (audit aligned with current repo state)

## Goal
Provide and harden a non-Arduino `STM32G474` target for JaszczurHAL so the
STM32 backend reaches practical parity with the existing RP2040/Arduino-pico paths
where the hardware model is compatible.

## Delivered scope (foundational bring-up)

### 1. New static-library build path for STM32
Added files:
- `stm32_lib/CMakeLists.txt`
- `stm32_lib/toolchain_stm32g474.cmake`
- `scripts/build_stm32_lib.sh`

What they provide:
- a separate CMake target `JaszczurHAL` for STM32G474,
- a dedicated `arm-none-eabi-*` toolchain,
- a convenient build script analogous to the existing
  `scripts/build_rp2040_lib.sh`.

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
- a project-owned register-level STM32G474 backend with shared-driver reuse
  where possible,
- no dependency on Arduino libraries (non-Arduino builds use local compatibility
  headers where Arduino-origin drivers need common types),
- core modules that used to be placeholders now have real STM32G474 backends
  for GPIO, timer, PWM, ADC, I2C, SPI, UART, serial/debug, RTC, RGB LED,
  thermocouples, display transport, and fault/reset diagnostics,
- the code carries TODO markers wherever the STM32 wiring should ultimately go.

### 3. Default STM32 feature profile
`stm32_lib/CMakeLists.txt` enables by default:
- `HAL_ENABLE_I2C`
- `HAL_ENABLE_I2C_SLAVE`
- `HAL_ENABLE_SPI`
- `HAL_ENABLE_UART`
- `HAL_ENABLE_DAC`
- `HAL_ENABLE_PCNT`
- `HAL_ENABLE_MCP401X`
- `HAL_ENABLE_MAX5395`
- `HAL_ENABLE_MCP9600`
- `HAL_ENABLE_MAX6675`
- `HAL_ENABLE_BH1750`
- `HAL_ENABLE_TSC2007`
- `HAL_ENABLE_STMPE610`
- `HAL_ENABLE_IRSMALL_DECODER`
- `HAL_ENABLE_EXTERNAL_ADC`
- `HAL_ENABLE_DS18B20`
- `HAL_ENABLE_MCP2515`
- `HAL_ENABLE_MCP251XFD`
- `HAL_ENABLE_STM32G474_FDCAN`
- `HAL_ENABLE_GPS`
- `HAL_ENABLE_HD44780`

This is still a conservative default profile: it enables the bus/core pieces
plus the shared HAL-level drivers that are already known to build on STM32G474.
Other modules remain opt-in through project config or `EXTRA_HAL_DEFINES`.

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
  - `./scripts/build_stm32_lib.sh --clean`
- **Examples build system** - STM32G474-targeted examples compile to
  ELF/BIN/HEX without errors using the unified CMake build:
  ```bash
  cmake -S examples -B build_examples_stm32 \
        -DJH_EXAMPLE_TARGET=stm32g474 \
        -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"
  cmake --build build_examples_stm32
  ```

## How to build for the real STM32G474
After installing the Arm toolchain:

```bash
./scripts/build_stm32_lib.sh --clean
```

Optionally:

```bash
./scripts/build_stm32_lib.sh --clean \
  -p /path/to/project \
  -D HAL_DISABLE_ASSERTS
```

## Real backends delivered (beyond the skeleton)
The following modules are real, register-level backends under
`JH_STM32G474_HW` (no longer placeholders):

- `hal_gpio` - direction + digital read/write + EXTI interrupt attach
  (pin id = `port*16 + pin`). `hal_gpio_attach_interrupt()` now configures
  SYSCFG EXTI routing, trigger edge (rising/falling/both), NVIC enable for
  EXTI0..4 / EXTI9_5 / EXTI15_10, and callback dispatch from IRQ context.
  `hal_gpio_set_irq_priority()` now sets the NVIC priority for all GPIO EXTI
  IRQ groups.
- `hal_i2c` - I2C master on bus 0 and bus 1 with full HAL API parity:
  - bus 0 -> I2C1, bus 1 -> I2C2,
  - `hal_i2c_init_bus(bus, sda_pin, scl_pin, clock_hz)` now uses caller pin
    parameters (no ignored placeholders), validates SDA/SCL AF mapping for the
    selected controller, configures GPIO AF/open-drain/pull-up, and rejects
    invalid pin pairs with `HAL_ASSERT`,
  - bus clock selection now honors requested HAL clock tiers via TIMINGR
    presets (100 kHz / 400 kHz / 1 MHz at the current 16 MHz bring-up clock),
  - register-level transfer path (`write`, `read`, `write_read`, `is_busy`) is
    active for both buses when initialized,
  - `hal_i2c_bus_clear_bus()` now performs real GPIO-level 9-clock recovery and
    STOP generation using the provided SDA/SCL pins.
- `hal_i2c_slave` - I2C target/slave register-map backend on bus 0 and bus 1:
  - bus 0 -> I2C1, bus 1 -> I2C2,
  - uses the same SDA/SCL AF mapping and default pins as the I2C master backend,
  - configures conservative 100 kHz/16 MHz `TIMINGR` fields for slave-mode
    data setup/hold timing,
  - clears `OA1EN` before writing a new own address during re-init,
  - configures own-address match, RX/TX/ADDR/STOP/NACK/error interrupts, and
    serves the standard `HAL_I2C_SLAVE_REG_MAP_SIZE` register-map protocol,
  - flushes TXDR on NACK/STOP so an unconsumed transmit byte cannot leak into
    the next master read.
- `hal_dac` - DAC1, 12-bit (ch0 -> PA4, ch1 -> PA5).
- `hal_pcnt` - hardware pulse counter on TIM2 (external clock mode).
- `hal_pwm` / optional `hal_pwm_freq` - register-level TIM PWM output on
  mapped TIM2/TIM3/TIM4/TIM15/TIM16/TIM17 channels, including PA5/LD2 simple
  PWM and frequency-controlled channels. GPIO alternate-function output for
  `hal_pwm_freq` is deferred until the first duty write.
- `hal_timer` - TIM6-backed 1 MHz alarm scheduler with the same low-level
  alarm/cancel/reschedule contract used by the RP2040 backend. Long delays are
  chunked across 16-bit TIM6 periods, callback return values can reschedule the
  same alarm, logical pools are enforced in software, and the shared
  `hal_timer_ext.cpp` managed-timer layer provides start/stop/pause/resume.
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
- `hal_can` - generic CAN facade with backend selection. `HAL_ENABLE_MCP2515`
  enables classic CAN over the shared MCP2515 HAL-SPI backend;
  `HAL_ENABLE_MCP251XFD` enables external MCP2517FD/MCP2518FD CAN FD over SPI;
  `HAL_ENABLE_STM32G474_FDCAN` enables the native FDCAN1 register backend with
  fixed Message RAM layout, RX FIFO0, TX buffers, filters, modes and
  state/error counters. Target `hal_can.cpp` owns handle lifetime/dispatch.
- `hal_display` - **ILI9341** plus **ST7735/ST7789/ST7796S** via shared
  HAL-only SPI/GPIO drivers (`impl/shared/drivers/display/ili9341_driver.*`,
  `impl/shared/drivers/display/st77xx_driver.*`) and **SSD1306** via the shared HAL I2C
  driver (`impl/shared/drivers/display/ssd1306_driver.*`). Rendering (geometry, text,
  bitmaps) runs through the portable GFX engine (`impl/shared/drivers/display/jh_gfx.*`).
  The whole stack lives in one shared `impl/shared/drivers/display/hal_display.cpp`
  used by both STM32G474 and RP2040. Init, rotation, inversion, fill, bitmap
  writes, geometry and text rendering are present; DMA/bulk-write optimization
  remains future work.
- `hal_thermocouple` - **MCP9600/MCP9601** via the shared HAL I2C driver
  (`impl/shared/drivers/mcp9600/mcp9600_driver.*`) and **MAX6675** via the shared Arduino-free
  GPIO bit-bang driver (`impl/shared/drivers/max6675/max6675_driver.*`). The same driver logic
  is used by STM32G474 and RP2040.
- `hal_serial` - debug USART2 (ST-Link VCP) for `hal_debug_*` output.
- `hal_uart` - USART1 hardware UART (TX/RX, configurable baud, used as GPS
  transport).
- `hal_system` / fault diagnostics - full reset/fault path on STM32G474:
  - reset cause classification from `RCC->CSR` (`IWDGRSTF`, `WWDGRSTF`,
    `SFTRSTF`, `PINRSTF`, `BORRSTF`, `LPWRRSTF`, `OBLRSTF`) with flag clear
    via `RMVF`,
  - retained exception-frame handoff from `exception_info` (`.noinit`) into
    `hal_get_last_fault()` (`pc/lr/psr`), with reset reason override to
    `HARDFAULT`,
  - stack guard support (`hal_stack_guard_init/check`) using a canary at the
    linker-provided stack limit and retained `STACK_OVERFLOW` marker on
    corruption.
- `hal_sync` - spinlock mutex plus PRIMASK-backed critical sections on real
  Cortex-M builds; host sanity builds keep critical sections as no-ops.

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

`src/hal/hal_digipot.cpp` plus `src/hal/impl/shared/drivers/digipot/` is the reference
pattern: the public module owns handles/locking/dispatch, while chip drivers own
validation, init and I/O sequences. **These already work on STM32** as long as
the underlying bus HAL exists.

**2. Former vendor Arduino libraries.** The old Arduino `Wire`/`SPI`-bound
device libraries have been replaced by shared HAL-level drivers for RTC,
display, thermocouple, OneWire/DS18B20, external ADC, RGB LED, CAN over SPI and
related devices. These modules now follow the same portable pattern as
`hal_digipot`: public facade plus shared chip logic over HAL SPI/I2C/GPIO.

### Deciding factor: bus state on STM32

| Bus  | STM32G474 status | Consequence |
|------|------------------|-------------|
| I2C  | Full master/controller API in `impl/stm32g474/hal_i2c.cpp`; slave/target register-map API in `impl/stm32g474/hal_i2c_slave.cpp` | I2C device drivers are portable today; STM32 can also expose a simple I2C target register map |
| SPI  | **Hardware SPI1/SPI2** with Arduino-style transaction + transfer API; non-Arduino builds provide `<SPI.h>` (`SPIClass`/`SPISettings`) backed by `hal_spi_*` | SPI device drivers can now be ported behind the HAL / Arduino-compatible shim |
| UART | USART1 hardware (TX/RX, configurable baud) - used as GPS transport | UART-based peripherals are portable today |

SPI is no longer blocked at the bus layer. The first STM32 implementation is
polling-based rather than DMA, but it is hardware-backed and matches the
Arduino driver surface closely enough for the shared SPI CAN/display drivers.
MCP9600/MCP9601 and MAX6675 are handled separately by shared HAL-level drivers.
ADS1X15/ADS1115 is also now handled by a shared HAL-level driver used by both
RP2040 and STM32G474.
OneWire and DS18B20 now follow the same shared HAL-level path over
`hal_gpio` + `hal_time`.
Default
G474 pins: bus 0 -> SPI1 PA6/PA7/PA5, bus 1 -> SPI2 PB14/PB15/PB13; CS remains
a normal GPIO owned by each driver.

### Module gap on STM32
Modules still missing a real STM32G474 backend, or still blocked by a missing
STM32 storage/transport layer:
`mqtt, ota, udp, wifi, wireguard`.

### Portability tiers

The shared-driver migration has already covered the highest-value portable
device classes: RTC, external ADC, OneWire/DS18B20, display, RGB LED,
thermocouples, CAN/MCP2515, CAN FD/MCP251XFD, digipot, BH1750, and PGA2311.

`hal_external_adc` / ADS1115 has completed this path and now lives in
`src/hal/impl/shared/drivers/ads1x15/ads1x15_driver.*`.

`hal_onewire` and `hal_ds18b20` have completed this path and now live in
`src/hal/impl/shared/drivers/onewire/`.

`hal_display` has completed this path: ILI9341, ST7735/ST7789/ST7796S (SPI) and
SSD1306 (I2C) now share one HAL-only stack under `src/hal/impl/shared/drivers/display/`,
used identically by STM32G474 and RP2040. MAX6675 is handled separately by the
shared bit-bang HAL GPIO driver. The remaining display work is a bulk-write/DMA
evaluation if TFT throughput needs it.

`hal_rgb_led` has completed the shared-NeoPixel-core path on STM32G474 using a
cycle-timed GPIO transport. A PWM+DMA or SPI transport can still be evaluated
later if WS2812 throughput or interrupt latency becomes a practical issue.

`hal_rtc` has also completed this path: both PCF8563 and DS3231 now run through
shared HAL I2C drivers with STM32G474 and RP2040 wrappers using the same device
logic.

**🔴 Not a "driver port" - different effort entirely:**
- `hal_wifi / hal_udp / hal_mqtt / hal_wireguard` - tied to Pico-W (CYW43) +
  PubSubClient + `arduino-wireguard-pico-w`. STM32G474 has no radio -> not a port
  but a different transport (e.g. via the already-portable SIMCom modem).
  Effectively N/A for a bare G474.
- `hal_ota` - STM32 flash/update specific, not a vendor-driver port.
- `hal_swserial` - implemented through the shared HAL GPIO/timing/sync driver.

### Recommended order
1. **On-silicon validation first** - confirm the delivered register-level
  backends on Nucleo-G474RE, especially GPIO IRQ routing, TIM6 alarm jitter,
  PWM channel mapping, SPI/I2C pin-map validation, RTC wiring, and RGB LED
  timing margins.
2. **Widen STM32-targeted regression coverage** - add focused tests for the
  STM32-specific backends beyond `hal_system` and `hal_timer`, especially
  GPIO IRQ, PWM, I2C, SPI, CAN, and RTC integration seams.
3. **Remaining peripheral gaps** - define a separate OTA/update strategy if
  firmware updates become part of the target requirements.
4. **Optional performance follow-up** - evaluate display bulk-write and DMA
  paths only if measured TFT throughput or CPU cost justifies the added
  backend complexity.

## Remaining work for the next stages
1. On-silicon validation on Nucleo-G474RE for all register-level backends,
   including TIM6 timer alarm jitter/latency checks.
2. FreeRTOS hardware/runtime validation and module hardening after the Stage 7
   `app_task0`/`app_task1` task entry mode.
3. Add hardware smoke-tests (GPIO/UART/I2C/SPI/ADC/CAN/timer/RTC/display) on an
  STM32G474 board.
4. Expand STM32-targeted regression coverage beyond the current `hal_system`
  and `hal_timer` focused tests.
5. Gradually unlock further modules (`HAL_ENABLE_*`) where the missing STM32
  storage/transport/backend work is actually complete.
