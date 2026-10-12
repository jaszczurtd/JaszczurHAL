<a id="12---i2c-scanner-and-stm32g474-hardware-verification"></a>

# 12 - Finding I2C devices on STM32G474

This example scans I2C addresses `0x08`-`0x77` and prints the devices that
acknowledge. Use it to check wiring and basic I2C operation on every
assembly in `device_tree.json`, including I2C1 on STM32G474.

On NUCLEO-G474RE SCL uses PB8 (D15), and SDA uses PB9 (D14). The bus runs at
100 kHz. On STM32G474 the TIMINGR settings use the HSI16 clock selected for
I2C, independently of SYSCLK/PCLK1. The NUCLEO console uses USART2 through
the ST-Link Virtual COM Port at **115200 8N1**.

The application calls `hal_i2c_scan_bus()` with `hal_watchdog_feed` to
service the watchdog after each probe. It prints the results and waits two
seconds before scanning again.

The PB9/PB8 connection was previously tested with PCF8563 (`0x51`) and
DS3231 (`0x68`) modules. This scanner uses 100 kHz;
`examples/16_rtc_backends` uses the same connection at 400 kHz.

## Hardware wiring

`device_tree.json` describes the wiring of every assembly, and the program
takes the bus, its pins and its rate from the generated header. The table
below repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| SDA | GPIO4 | PB9, CN10 pin 5 / D14 | GPIO4 |
| SCL | GPIO5 | PB8, CN10 pin 3 / D15 | GPIO5 |

On Pico and Pico 2 the bus uses GPIO4/GPIO5 rather than GPIO25/GPIO24, which
drive the LED and sense VBUS there. On ESP32-S3-Zero it uses GPIO4/GPIO5,
because GPIO24 and GPIO25 do not exist on the ESP32-S3.

NUCLEO-G474RE connection:

```
Nucleo-G474RE                 I2C device (e.g. PCF8563 RTC, AT24C256, BME280)
  PB8 (D15, SCL) ───┬──────── SCL
  PB9 (D14, SDA) ──┬┼──────── SDA
  3V3 ─────────────┼┼──[4.7k]─┘   (SDA pull-up)
                   └─────[4.7k]──── 3V3   (SCL pull-up)
  3V3 ──────────────────────────── VCC
  GND ──────────────────────────── GND
```

Use external 2.2-10 kΩ pull-ups to 3V3. STM32 internal pull-ups do not replace
the external resistors required for this connection. Check whether the module
already includes them.

For a DS3231 module with a `+ D C NC -` connector, use:

| Module pin | Connect to |
|---|---|
| `+` | `3V3` |
| `D` | `D14` / `PB9` / SDA |
| `C` | `D15` / `PB8` / SCL |
| `NC` | Leave unconnected |
| `-` | `GND` |

Power the module from 3.3 V so its on-board I2C resistors also pull the lines
up to 3.3 V.

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
  .build/examples/12_i2c_scan/stm32g474/nucleo/firmware.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

Alternatively, upload through OpenOCD:
`vscode/entry/jh-vscode upload --project examples/12_i2c_scan --target stm32g474`.

## Expected output

With a PCF8563 connected at `0x51`:

```
=== JaszczurHAL I2C scanner ===
assembly nucleo: I2C bus 0 at 100000 Hz (external pull-ups to 3V3 required)
scanning 0x08..0x77 ...
  device @ 0x51
scanning 0x08..0x77 ...
  device @ 0x51
...
```

The reported addresses should match the connected devices. A result shows
that the device acknowledges its address during the scan.

## Troubleshooting

| Symptom | What to check |
|---|---|
| `(no devices found)` after every scan | Check module power, pull-ups, SDA/SCL wiring, and the address range. |
| Every address from `0x08` to `0x77` responds | SDA may be stuck low because of a short or missing pull-up. Do not treat this as a list of detected devices. |
| No console output | Check the port and baud rate. After opening the terminal, press RESET B2 to see startup messages. |
| `st-info --probe` finds no device | Check the USB connection to CN1 ST-LINK. Read system messages with `dmesg \| tail`; check serial-port availability separately. |
| Address differs from the device documentation | The scanner prints 7-bit addresses. Some datasheets show the address shifted by one bit to leave room for the read/write bit. |

## Notes

This example uses bus 0, or I2C1. The STM32G474 implementation also supports
bus 1 (I2C2) with a valid SDA/SCL alternate-function pair. Both controllers
select HSI16 as their clock source, so the 16 MHz TIMINGR settings remain
valid when SYSCLK or the APB clock changes.
