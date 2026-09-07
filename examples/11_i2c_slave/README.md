# 11 - Exposing registers over I2C

This example runs the microcontroller as an I2C slave at address `0x42`.
An external bus controller can read its register map. The application sets
a status marker to `0xA5`, updates a counter and uptime every second, and
prints the I2C transaction count to the console.

## Wiring

| Signal | RP family | STM32G474 |
|---|---|---|
| SDA | GP4 | PB9 |
| SCL | GP5 | PB8 |

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
  --project examples/11_i2c_slave --target rp2040 --board pico
```

The configuration also includes RP2350 ARM, RP2350 RISC-V, and STM32G474.
