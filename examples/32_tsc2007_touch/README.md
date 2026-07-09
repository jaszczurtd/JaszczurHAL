# TSC2007 Touch Controller

Portable TSC2007 resistive touch controller example using the shared
`hal_tsc2007` driver over HAL I2C.

| Signal | RP2040 GPIO | STM32G474 pin id |
|---|---:|---:|
| SDA | GP4 | PB9 (`25`) |
| SCL | GP5 | PB8 (`24`) |

Build targets:

```bash
../../vscode/entry/jh-vscode build --project . --target rp2040
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
