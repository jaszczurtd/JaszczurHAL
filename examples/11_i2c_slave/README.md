# 11 - Exposing registers over I2C

This example runs the microcontroller as an I2C slave at address `0x42`.
An external bus controller can read its register map. The application sets
a status marker to `0xA5`, updates a counter and uptime every second, and
prints the I2C transaction count to the console.

## Wiring

`device_tree.json` describes the wiring of every assembly, and the program
takes the bus and its pins from the generated header. The table below
repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| SDA | GPIO4 | PB9, CN10 pin 5 / D14 | GPIO4 |
| SCL | GPIO5 | PB8, CN10 pin 3 / D15 | GPIO5 |

Connect a second I2C controller, connect the grounds, and provide pull-ups
compatible with the devices' logic voltages. `HAL_ENABLE_I2C_SLAVE` enables
the module.

## Register map in the supplied code

| Constant | Starting index | Written value |
|---|---|---|
| `REG_STATUS` | `0` | 8-bit `0xA5` marker |
| `REG_COUNTER` | `1` | 16-bit counter |
| `REG_MILLIS_HI` | `2` | Upper 16 bits of uptime in milliseconds |
| `REG_MILLIS_LO` | `4` | Lower 16 bits of uptime in milliseconds |

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/11_i2c_slave --assembly pico
```

The other assemblies are `pico2-arm`, `pico2-riscv`, `nucleo` and
`esp32s3-zero`.
