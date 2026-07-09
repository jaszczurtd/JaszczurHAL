# HD44780 Character LCD

Portable HD44780-compatible 16x2 LCD example using the shared
`HD44780` driver over HAL GPIO.

The example uses 4-bit mode and assumes the LCD `RW` pin is tied to GND.

| Signal | RP2040 GPIO | STM32G474 pin id |
|---|---:|---:|
| RS | GP12 | PC0 (`32`) |
| E  | GP11 | PC1 (`33`) |
| D4 | GP10 | PC2 (`34`) |
| D5 | GP9  | PC3 (`35`) |
| D6 | GP8  | PC4 (`36`) |
| D7 | GP7  | PC5 (`37`) |

Build targets:

```bash
../../vscode/entry/jh-vscode build --project . --target rp2040
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
