# STM32G474 GPS reader

Reads a live NMEA GPS receiver on a **Nucleo-G474RE** and prints the decoded
fix. This is the STM32 counterpart of the RP2040 `07_gps` sketch (which is an
Arduino `.ino`); it is built with the bare-metal `stm32_lib` toolchain.

- Parsing is the shared portable engine (`impl/shared/frameworks/gps/gps_nmea_parser.cpp` +
  `hal_gps_core.cpp`).
- Transport: **USART1** (`hal_uart`, polled), GPS at **9600 8N1**.
- Console: USART2 / ST-Link Virtual COM Port @ **115200 8N1**.

## Hardware wiring

```
Nucleo-G474RE              GPS module (e.g. u-blox NEO-6M/7M/M8)
  PA10 (USART1 RX) ──────── TX
  PA9  (USART1 TX) ──────── RX   (optional, for sending config)
  3V3 ────────────────────  VCC
  GND ────────────────────  GND
```

The receiver's **TX** must reach the board's **PA10**. Share a common ground.

## Build & flash (Linux Mint / Debian-like)

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio

cd examples/g474_gps
./build.sh                            # -> build/g474_gps.{elf,bin,hex}

st-info --probe
st-flash --reset write build/g474_gps.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

## Expected output

Before a fix the program reports the parser counters; once the receiver locks:

```
=== JaszczurHAL G474 GPS reader (USART1 @ 9600) ===
Wiring: GPS TX -> PA10, GND -> GND, VCC -> 3V3
waiting for fix: chars=512 ok=4 fail=0
  lat (1e7 deg)   : 481173000
  lon (1e7 deg)   : 115166666
  altitude (cm)   : 54540
  speed (x100kmph): 0
  course (x100)   : 0
  sats used       : 8
  sats in view    : 11
  fix quality     : 1
  fix mode        : 3
  hdop (x100)     : 90
  h-accuracy (cm) : 250
  age (ms)        : 120
```

(Values are scaled integers to avoid pulling in float `printf`: latitude/
longitude are 1e-7 deg, speed/course/DOP are ×100, altitude/accuracy in cm.)

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `chars=0` forever | GPS TX not on PA10; receiver unpowered; wrong baud |
| `ok=0 fail>0` | Baud/framing mismatch (this build is fixed 9600 8N1) |
| `waiting for fix` indefinitely | No sky view / cold start - give it minutes outdoors |
| Console silent | Wrong port/baud, or terminal opened before reset (press B2) |

## Notes

- The USART backend is polled, so `hal_gps_update()` is called every loop and
  reporting is throttled separately - don't gate the polling behind a delay.
- Register-level USART (`JH_STM32G474_HW`) follows RM0440 but is pending
  on-silicon validation, like the I2C / ADC backends.
- GPS transport is decoupled from the parser: `HAL_ENABLE_GPS` requires a
  transport (`HAL_ENABLE_UART` here, or `HAL_ENABLE_SWSERIAL` on RP2040).
- Default UART is USART1 (`HAL_GPS_UART_PORT`); pins are forwarded to
  `hal_uart_create()`. USART2 is reserved for the debug console here.
