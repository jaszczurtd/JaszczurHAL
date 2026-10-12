<a id="19---resistive-touch-controllers"></a>

# 19 - Reading touch input with TSC2007 and STMPE610

This example reads the TSC2007 and STMPE610 resistive-touch controllers and
prints their measurements to the debug console. Both devices share I2C bus 0
but are initialized independently.

`device_tree.json` describes the wiring of every assembly, and the program
takes the I2C bus, pins, clock (100 kHz) and both addresses from the
generated header. TSC2007 uses its default address `0x48` and STMPE610 its
default `0x41`. The table below repeats the wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| I2C SDA | GP4 | PB9, CN10 pin 5 / D14 | GPIO4 |
| I2C SCL | GP5 | PB8, CN10 pin 3 / D15 | GPIO5 |

On ESP32-S3-Zero I2C uses GPIO4/GPIO5 rather than GPIO25/GPIO24, because the
board does not have those pins.

Use external pull-up resistors on the I2C lines. Build with a VS Code task,
or run `scripts/examples_dispatcher.py build --target rp2040 --example 19_touch`
from the repository root.
