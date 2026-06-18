# Example 33: STMPE610 touch controller

Initializes the STMPE610 resistive touch controller over I2C and prints touch
samples through the debug helper.

Default wiring:

| Target | I2C bus | SDA | SCL | Address |
| --- | --- | --- | --- | --- |
| RP2040 | 0 | GP4 | GP5 | 0x41 |
| STM32G474 | 0 | PB9 | PB8 | 0x41 |

The example uses `debugInit()` and the `deb`/`derr` logging macros from
`tools_c.h`.
