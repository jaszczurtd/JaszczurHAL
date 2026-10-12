# 15 - OLED and character LCD

This example displays data on an SSD1306 128×64 OLED and an HD44780-compatible
16×2 character LCD. The OLED uses I2C and the buffered `hal_display` API.
The LCD uses four-bit GPIO mode; connect its `RW` pin to GND.

The displays are initialized independently. Either one can be omitted,
although each build still includes both drivers.

`device_tree.json` describes the wiring of every assembly, and the program
takes the I2C bus, the OLED address (`0x3C`) and the LCD lines from the
generated header. The table below repeats that wiring. The I2C bus runs at
400 kHz.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| OLED SDA / SCL | GPIO4 / GPIO5 | PB9 / PB8 (D14 / D15) | GPIO4 / GPIO5 |
| LCD RS / E | GPIO12 / GPIO11 | PC0 / PC1 | GPIO12 / GPIO11 |
| LCD D4..D7 | GPIO10..GPIO7 | PC2..PC5 | GPIO10..GPIO7 |

On ESP32-S3-Zero the OLED bus uses GPIO4/GPIO5 rather than GPIO25/GPIO24,
which do not exist on the ESP32-S3, and the LCD uses GPIO12..GPIO7 rather
than GPIO32..GPIO37, which this board does not make available.

Run `../../vscode/entry/jh-vscode build --project . --assembly pico`
from this example's directory. The other assemblies are `pico2-arm`,
`pico2-riscv`, `nucleo` and `esp32s3-zero`.
