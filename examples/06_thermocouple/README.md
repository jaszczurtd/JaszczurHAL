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

`device_tree.json` describes the wiring of every assembly, and the program
takes the bus, pins and address from the generated header. The table below
repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| MCP9600 SDA / SCL | GP4 / GP5 | PB9 / PB8, CN10 pin 5 / 3 (D14 / D15) | GPIO4 / GPIO5 |
| MAX6675 SCLK | GP18 | PB3, CN10 pin 31 / D3 | GPIO18 |
| MAX6675 CS | GP17 | PB6, CN10 pin 17 / D10 | GPIO17 |
| MAX6675 MISO | GP16 | PA6, CN10 pin 13 / D12 | GPIO16 |

On NUCLEO-G474RE the MAX6675 clock uses PB3 rather than D13 (PA5), because
PA5 drives the LD2 LED.

The MCP9600 uses I2C bus 0, address `0x67`, and the standard I2C clock rate.
The application selects a type-K thermocouple and sets the filter to `2`.
Use modules and thermocouples that match these settings; the MAX6675 supports
type K.

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/06_thermocouple --assembly pico
```

The other assemblies in `device_tree.json` cover Pico 2 (ARM and RISC-V),
NUCLEO-G474RE and ESP32-S3-Zero.
