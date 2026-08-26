# 12 - I2C scanner and STM32G474 hardware verification

Verifies the **real** `hal_i2c` backend on a **Nucleo-G474RE** by scanning the
I2C bus and printing every address that ACKs. Use it to confirm the bare-metal
I2C1 master works on silicon before relying on it.

- **I2C1**: SCL = **PB8**, SDA = **PB9** (NUCLEO expansion header: D15 = SCL, D14 = SDA)
- 100 kHz standard mode (TIMINGR uses the explicitly selected HSI16 kernel
  clock and is independent of SYSCLK/PCLK1)
- Uses `hal_i2c_scan()` and passes `hal_watchdog_feed` as the per-probe
  callback; formatting and the two-second repetition stay in the application.
- Console: USART2 / ST-Link Virtual COM Port @ **115200 8N1**

The I2C1 route is physically validated on NUCLEO-G474RE with PCF8563 (`0x51`)
and DS3231 (`0x68`) modules. The scanner uses 100 kHz; the same PB9/PB8 route
is also exercised at 400 kHz by `examples/16_rtc_backends`.

## Hardware wiring

```
Nucleo-G474RE                 I2C device (e.g. PCF8563 RTC, AT24C256, BME280)
  PB8 (D15, SCL) ───┬──────── SCL
  PB9 (D14, SDA) ──┬┼──────── SDA
  3V3 ─────────────┼┼──[4.7k]─┘   (SDA pull-up)
                   └─────[4.7k]──── 3V3   (SCL pull-up)
  3V3 ──────────────────────────── VCC
  GND ──────────────────────────── GND
```

External pull-ups (2.2k-10k to 3V3) are **required** - STM32 internal pull-ups
are too weak for reliable I2C. Many breakout boards already include them.

For a DS3231 module with a `+ D C NC -` connector, use:

| Module pin | Connect to |
|---|---|
| `+` | `3V3` |
| `D` | `D14` / `PB9` / SDA |
| `C` | `D15` / `PB8` / SCL |
| `NC` | Leave unconnected |
| `-` | `GND` |

Power the module from 3.3 V so any on-board I2C pull-ups also terminate at
3.3 V.

## Build & flash (Linux Mint / Debian-like)

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio
sudo usermod -aG dialout "$USER"      # then log out/in for serial access

cd /path/to/JaszczurHAL
vscode/entry/jh-vscode build \
  --project examples/12_i2c_scan --target stm32g474

st-info --probe                       # confirm the ST-Link sees the G474
st-flash --reset write \
  .build/examples/12_i2c_scan/firmware.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

(OpenOCD alternative:
`vscode/entry/jh-vscode upload --project examples/12_i2c_scan --target stm32g474`.)

## Expected output

With, say, a PCF8563 RTC (0x51) on the bus:

```
=== JaszczurHAL G474 I2C scanner ===
I2C1: SCL=PB8, SDA=PB9 (external pull-ups to 3V3 required)
scanning 0x08..0x77 ...
  device @ 0x51
scanning 0x08..0x77 ...
  device @ 0x51
...
```

The address(es) printed must match the device(s) you wired. That confirms
START / address / ACK / STOP all work end-to-end.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `(no devices found)` every round | Missing/weak pull-ups; swapped SDA/SCL; device unpowered; wrong address range |
| **Every** address 0x08..0x77 reports a device | SDA stuck low (shorted, or no pull-up so the line floats) - not real ACKs |
| Console silent, board powered | Wrong port/baud, or terminal opened before reset - press the black RESET (B2) |
| `st-info --probe` finds nothing | Use the CN1 (ST-LINK) USB port; check `dmesg \| tail` for `ttyACM0` |
| Found address is off by one | Remember these are 7-bit addresses; some datasheets quote the 8-bit (shifted) form |

## Notes

- This fixture exercises bus 0 = I2C1. The STM32G474 backend also supports bus
  1 = I2C2 with a valid SDA/SCL alternate-function pair.
- I2C1 and I2C2 explicitly select HSI16 as their kernel clock. The 16 MHz
  TIMINGR presets therefore remain valid when SYSCLK or the APB clock changes.
