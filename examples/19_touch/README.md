# 19 - Resistive touch controllers

This project exercises the TSC2007 and STMPE610 controllers. Both share I2C bus
0 and are initialized independently; samples from each detected device are
printed through the debug console.

| Target | SDA | SCL | TSC2007 | STMPE610 |
| --- | --- | --- | --- | --- |
| RP family | GP4 | GP5 | `0x48` default | `0x41` default |
| STM32G474 | PB9 | PB8 | `0x48` default | `0x41` default |

External I2C pull-ups are required. Build with the generated VS Code manifest
or `scripts/examples_dispatcher.py build --target rp2040 --example 19_touch`
from the HAL root.
