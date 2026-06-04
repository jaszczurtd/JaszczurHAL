# STM32G474 Porting Progress

Last updated: 2026-06-03 (added driver pool portability analysis)

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
- `src/hal/impl/stm32g474/hal_time.cpp` (always-available `hal_time_from_components`)

Nature of the implementation:
- a minimal, safe skeleton for the later switch to STM32 HAL/LL,
- no dependency on `Arduino.h` or the Arduino libraries,
- critical modules have a working API and internal state (placeholder behaviour),
- the code carries TODO markers wherever the STM32 wiring should ultimately go.

### 3. Default STM32 feature profile (initial)
`stm32_lib/CMakeLists.txt` enables by default:
- `HAL_ENABLE_WIFI`
- `HAL_ENABLE_TIME`
- `HAL_ENABLE_EEPROM`
- `HAL_ENABLE_GPS`
- `HAL_ENABLE_THERMOCOUPLE`
- `HAL_ENABLE_DS18B20`
- `HAL_ENABLE_SWSERIAL`
- `HAL_ENABLE_I2C_SLAVE`
- `HAL_ENABLE_EXTERNAL_ADC`
- `HAL_ENABLE_PWM_FREQ`
- `HAL_ENABLE_RGB_LED`
- `HAL_ENABLE_CAN`
- `HAL_ENABLE_DISPLAY`
- `HAL_ENABLE_UNITY`

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
- `hal_dac` - DAC1, 12-bit (ch0 → PA4, ch1 → PA5).
- `hal_pcnt` - hardware pulse counter on TIM2 (external clock mode).
- `hal_adc` - **ADC1**, single-ended, polled, one regular conversion per
  `hal_adc_read()`. The first read lazily brings ADC1 up (ADC12 clock,
  internal regulator + startup wait, single-ended calibration, enable) and
  routes the requested pin to analog mode on demand. ADC kernel clock is
  HCLK/1, so the HSI16 bring-up clock gives a 16 MHz ADC clock. Pin → channel
  map per RM0440: PA0..PA3 → IN1..IN4, PB0 → IN15, PB1 → IN12, PB11 → IN14,
  PB12 → IN11, PB14 → IN5, PC0..PC3 → IN6..IN9. Example:
  `examples/g474_adc_read`.

These register sequences follow RM0440 but are pending on-silicon validation on
a real Nucleo-G474RE (that is what the `examples/g474_*` programs are for).

## Driver pool analysis - portability to STM32G474

Assessment of the existing JaszczurHAL driver pool and how much of it can be
reused on the STM32 backend (RP2040 is the only fully-driven target today).

### Two distinct classes of "driver"

**1. Portable HAL-level drivers** - live directly in `src/hal/`, guarded only by
`HAL_ENABLE_*` (not by target), written against the HAL's own API
(`hal_i2c`, `hal_serial`, `hal_sync`):
`hal_digipot`, `hal_crypto`, `hal_kv`, `hal_modem_at`, `hal_simcom_a76xx`,
`hal_pid_controller`, `hal_soft_timer`, `hal_config`.

`src/hal/hal_digipot.cpp` is the reference pattern (see its header comment:
"compiles and runs on every backend that provides hal_i2c"). **These already
work on STM32** as long as the underlying bus HAL exists.

**2. Vendor Arduino libraries** - in `src/hal/impl/arduino/drivers/`:
`ADS1X15`, `Adafruit_BusIO/GFX/ILI9341/MCP9600/NeoPixel/SSD1306/ST7735_ST7789`,
`DS3231`, `DallasTemperature`, `MAX6675`, `MCP2515`, `OneWire`, `PCF8563`.
**All depend on `Arduino.h` / `Wire` / `SPI`.** They are wrapped by the Arduino
device-HALs (`hal_thermocouple.cpp`, `hal_rtc.cpp`, `hal_can.cpp`, …), each
guarded by `#if HAL_TARGET_IS_RP2040` and `#include`-ing `<Wire.h>`/`<SPI.h>`
directly. **These cannot be ported 1:1** - the realistic path is to rewrite the
device logic as portable `src/hal/` drivers (the digipot pattern).

### Deciding factor: bus state on STM32

| Bus  | STM32G474 status | Consequence |
|------|------------------|-------------|
| I2C  | Full Wire-style API (`begin_transmission`/`write`/`end`/`request_from`/`read`) in `impl/stm32g474/hal_i2c.cpp` | I2C device drivers are portable today |
| SPI  | Only `init`/`lock`/`unlock` in `impl/stm32g474/hal_spi.cpp` | **No transfer primitive anywhere in the HAL** |

Critical blocker: `hal_spi.h` declares **no `transfer` function**. In the Arduino
backend, SPI transfers go straight through Arduino `SPI.h`; STM32 SPI today is
just mutex + pin bookkeeping. **Every SPI-based driver is blocked** until a
`hal_spi_transfer*` primitive is added to the API and implemented on STM32.

### Module gap on STM32
Modules with Arduino + mock impl but no `impl/stm32g474`:
`can, display, ds18b20, eeprom, external_adc, i2c_slave, littlefs, mqtt,
onewire, ota, pwm_freq, rgb_led, rtc, swserial, thermocouple, udp, wifi,
wireguard`.

### Portability tiers

**🟢 Easy - mirror the digipot pattern (rewrite vendor logic on `hal_i2c`):**
- `hal_rtc` (PCF8563, DS3231) - plain I2C register access
- `hal_external_adc` (ADS1115) - plain I2C
- `hal_thermocouple` MCP9600 part - plain I2C

We do not port the Adafruit/vendor libraries; we extract their register maps and
write a portable driver on `hal_i2c`. Low risk, existing coverage in `tests/`.

**🟡 Blocked until an SPI transfer primitive exists:**
- `hal_can` (MCP2515) - SPI
- `hal_display` (ILI9341/ST7735/ST7789 - SPI; SSD1306 - I2C or SPI)
- `hal_thermocouple` MAX6675 part - SPI read
Prerequisite: add `hal_spi_transfer()` to `hal_spi.h` + implement SPI1/2/3 on G474.

**🟡 OneWire (bit-bang + timing):**
- `hal_ds18b20`, `hal_onewire` - OneWire relies on Arduino `digitalWrite` and
  precise timing/IRQ. Feasible on `hal_gpio` + `hal_time`, but needs a careful
  delay port. DallasTemperature sits on OneWire.

**🟡 `hal_rgb_led`** (NeoPixel/WS2812) - 800 kHz critical timing; on STM32 this is
a rewrite (PWM+DMA or SPI), not a library port.

**🔴 Not a "driver port" - different effort entirely:**
- `hal_wifi / hal_udp / hal_mqtt / hal_wireguard` - tied to Pico-W (CYW43) +
  PubSubClient + `arduino-wireguard-pico-w`. STM32G474 has no radio → not a port
  but a different transport (e.g. via the already-portable SIMCom modem).
  Effectively N/A for a bare G474.
- `hal_littlefs / hal_eeprom / hal_ota` - STM32 flash/storage specific, not
  vendor-driver ports.
- `hal_swserial / hal_i2c_slave / hal_pwm_freq` - STM32 peripheral work.

### Recommended order
1. **Add `hal_spi_transfer` first** (API + G474 impl) - unblocks CAN, display and
   MAX6675 in one move; biggest multiplier.
2. **I2C quick wins** via the digipot pattern: rtc (PCF8563/DS3231),
   external_adc (ADS1115), thermocouple (MCP9600).
3. **OneWire** as a portable driver on `hal_gpio` + `hal_time` → unblocks ds18b20.
4. Remainder (rgb_led, storage, connectivity) - separate decisions, not pure ports.

## Remaining work for the next stages
1. Replace the remaining `impl/stm32g474` placeholders with real STM32 HAL/LL calls.
2. A real `hal_timer_*` implementation (hardware timers/IRQ).
3. `hal_system` integration (watchdog, MCU UID, bootloader, time).
4. Add hardware smoke-tests (GPIO/UART/I2C/SPI/ADC) on an STM32G474 board.
5. Gradually unlock further modules (`HAL_ENABLE_*`) as the port progresses.
