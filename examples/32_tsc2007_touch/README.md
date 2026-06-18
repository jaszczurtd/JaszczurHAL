# TSC2007 Touch Controller

Portable TSC2007 resistive touch controller example using the shared
`hal_tsc2007` driver over HAL I2C.

| Signal | RP2040 GPIO | STM32G474 pin id |
|---|---:|---:|
| SDA | GP4 | PB9 (`25`) |
| SCL | GP5 | PB8 (`24`) |

Build targets:

```bash
cmake --build build_examples_rp2040 --target 32_tsc2007_touch_rp2040
cmake --build build_examples_stm32 --target 32_tsc2007_touch_stm32g474
```
