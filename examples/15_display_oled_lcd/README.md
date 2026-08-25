# 15 - OLED and character LCD

This project exercises both portable display paths:

- SSD1306 128x64 OLED over I2C and the shared buffered `hal_display` API;
- HD44780-compatible 16x2 LCD in four-bit GPIO mode (`RW` tied to GND).

The devices are initialized independently, so either one may be omitted from
the bench setup. The firmware still compiles both drivers in every gate build.

| Signal | RP family | STM32G474 |
| --- | --- | --- |
| OLED SDA / SCL | GP4 / GP5 | PB9 / PB8 |
| LCD RS / E | GP12 / GP11 | PC0 / PC1 |
| LCD D4..D7 | GP10..GP7 | PC2..PC5 |

Build with `../../vscode/entry/jh-vscode build --project . --target rp2040`
or select another supported target from the generated project manifest.
