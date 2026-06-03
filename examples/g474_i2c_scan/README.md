# STM32G474 I2C scanner — hardware verification

Verifies the **real** `hal_i2c` backend on a **Nucleo-G474RE** by scanning the
I2C bus and printing every address that ACKs. Use it to confirm the bare-metal
I2C1 master works on silicon before relying on it.

- **I2C1**: SCL = **PB8**, SDA = **PB9** (Arduino headers: D15 = SCL, D14 = SDA)
- 100 kHz standard mode (TIMINGR tuned for the 16 MHz HSI bring-up clock)
- Console: USART2 / ST-Link Virtual COM Port @ **115200 8N1**

> Status: the I2C register sequence (I2C v2, AUTOEND) compiles and is written to
> RM0440, but has **not** been validated on silicon in this repo. This example
> is exactly that validation step — run it on your Nucleo-G474RE.

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

External pull-ups (2.2k–10k to 3V3) are **required** — STM32 internal pull-ups
are too weak for reliable I2C. Many breakout boards already include them.

## Build & flash (Linux Mint / Debian-like)

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio
sudo usermod -aG dialout "$USER"      # then log out/in for serial access

cd examples/g474_i2c_scan
./build.sh                            # -> build/g474_i2c_scan.{elf,bin,hex}

st-info --probe                       # confirm the ST-Link sees the G474
st-flash --reset write build/g474_i2c_scan.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

(OpenOCD alternative: `openocd -f board/st_nucleo_g4.cfg -c "program build/g474_i2c_scan.elf verify reset exit"`.)

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
| **Every** address 0x08..0x77 reports a device | SDA stuck low (shorted, or no pull-up so the line floats) — not real ACKs |
| Console silent, board powered | Wrong port/baud, or terminal opened before reset — press the black RESET (B2) |
| `st-info --probe` finds nothing | Use the CN1 (ST-LINK) USB port; check `dmesg \| tail` for `ttyACM0` |
| Found address is off by one | Remember these are 7-bit addresses; some datasheets quote the 8-bit (shifted) form |

## Notes

- This is bus 0 = I2C1 only. The HAL `bus 1` path exists but has no second
  controller wired on this backend yet.
- The TIMINGR constant assumes I2CCLK = 16 MHz (HSI/PCLK1 default of the
  bring-up). If you raise the core clock via PLL, recompute TIMINGR
  (`I2C_TIMINGR_100K_16MHZ` in `port/stm32g474_regs.h`).
