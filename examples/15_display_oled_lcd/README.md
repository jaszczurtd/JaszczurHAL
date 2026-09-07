# 15 - OLED and character LCD

This example displays data on an SSD1306 128×64 OLED and an HD44780-compatible
16×2 character LCD. The OLED uses I2C and the buffered `hal_display` API.
The LCD uses four-bit GPIO mode; connect its `RW` pin to GND.

The displays are initialized independently. Either one can be omitted,
although each build still includes both drivers.

| Signal | RP family | STM32G474 |
| --- | --- | --- |
| OLED SDA / SCL | GP4 / GP5 | PB9 / PB8 |
| LCD RS / E | GP12 / GP11 | PC0 / PC1 |
| LCD D4..D7 | GP10..GP7 | PC2..PC5 |

Run `../../vscode/entry/jh-vscode build --project . --target rp2040`
from this example's directory. Other available targets are listed in the
generated project configuration.
