# 06 - Reading thermocouple temperatures

This example reads MCP9600 and MAX6675 devices through the shared
`hal_thermocouple` API. It reports thermocouple and ambient temperature for
the MCP9600, and thermocouple temperature for the MAX6675. Readings are
printed to the debug console once per second.

The devices are initialized independently. Failure to initialize one does not
prevent attempting the other. The configuration enables both drivers through
`HAL_ENABLE_MCP9600` and `HAL_ENABLE_MAX6675`, and I2C through
`HAL_ENABLE_I2C`.

## Wiring and settings

| Signal | RP family | NUCLEO-G474RE |
|---|---|---|
| MCP9600 SDA / SCL | GP4 / GP5 | PB9 / PB8 |
| MAX6675 SCLK | GP18 | PA5, CN10 pin 11 / D13 |
| MAX6675 CS | GP17 | PB6, CN10 pin 17 / D10 |
| MAX6675 MISO | GP16 | PA6, CN10 pin 13 / D12 |

The MCP9600 uses I2C bus 0, address `0x67`, and the standard I2C clock rate.
The application selects a type-K thermocouple and sets the filter to `2`.
Use modules and thermocouples that match these settings; the MAX6675 supports
type K.

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/06_thermocouple --target rp2040 --board pico
```

RP2350 ARM, RP2350 RISC-V, and STM32G474 configurations are also available.
