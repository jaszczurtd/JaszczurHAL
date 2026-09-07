<a id="19---resistive-touch-controllers"></a>

# 19 - Reading touch input with TSC2007 and STMPE610

This example reads the TSC2007 and STMPE610 resistive-touch controllers and
prints their measurements to the debug console. Both devices share I2C bus 0
but are initialized independently.

| Target | SDA | SCL | TSC2007 | STMPE610 |
| --- | --- | --- | --- | --- |
| RP family | GP4 | GP5 | `0x48` default | `0x41` default |
| STM32G474 | PB9 | PB8 | `0x48` default | `0x41` default |

Use external pull-up resistors on the I2C lines. Build with a VS Code task,
or run `scripts/examples_dispatcher.py build --target rp2040 --example 19_touch`
from the repository root.
