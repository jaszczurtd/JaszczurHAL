# STM32G474 Porting Progress

Last updated: 2026-06-03

## Goal
Provide a new `STM32G474` target skeleton for JaszczurHAL with no dependency on
the Arduino layer, so the STM32 backend can be developed in parallel with the
existing Arduino/RP2040 backend.

## Delivered scope ("2)" — skeleton)

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

- `hal_gpio` — direction + digital read/write (pin id = `port*16 + pin`).
- `hal_i2c` — I2C1 master (SCL=PB8, SDA=PB9, AF4, 100 kHz, AUTOEND).
- `hal_dac` — DAC1, 12-bit (ch0 → PA4, ch1 → PA5).
- `hal_pcnt` — hardware pulse counter on TIM2 (external clock mode).
- `hal_adc` — **ADC1**, single-ended, polled, one regular conversion per
  `hal_adc_read()`. The first read lazily brings ADC1 up (ADC12 clock,
  internal regulator + startup wait, single-ended calibration, enable) and
  routes the requested pin to analog mode on demand. ADC kernel clock is
  HCLK/1, so the HSI16 bring-up clock gives a 16 MHz ADC clock. Pin → channel
  map per RM0440: PA0..PA3 → IN1..IN4, PB0 → IN15, PB1 → IN12, PB11 → IN14,
  PB12 → IN11, PB14 → IN5, PC0..PC3 → IN6..IN9. Example:
  `examples/g474_adc_read`.

These register sequences follow RM0440 but are pending on-silicon validation on
a real Nucleo-G474RE (that is what the `examples/g474_*` programs are for).

## Remaining work for the next stages
1. Replace the remaining `impl/stm32g474` placeholders with real STM32 HAL/LL calls.
2. A real `hal_timer_*` implementation (hardware timers/IRQ).
3. `hal_system` integration (watchdog, MCU UID, bootloader, time).
4. Add hardware smoke-tests (GPIO/UART/I2C/SPI/ADC) on an STM32G474 board.
5. Gradually unlock further modules (`HAL_ENABLE_*`) as the port progresses.
