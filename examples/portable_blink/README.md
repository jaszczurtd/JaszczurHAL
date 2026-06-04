# portable_blink - one demo, two real targets

Proves JaszczurHAL portability concretely: the **same** application logic
([`blink_app.c`](blink_app.c), portable `hal_*` only) is built and run on
**RP2040** and **STM32G474** from a single example folder.

```
portable_blink/
  blink_app.h / blink_app.c   # portable logic (hal_* only) - shared by both
  portable_blink.ino          # RP2040/Arduino entry: setup()/loop()
  hal_project_config.h        # target/flags (auto-detect on Arduino)
  .vscode/                    # tasks: "Build (RP2040)" and "Build (STM32G474)"
  g474/                       # STM32G474 entry: main() + build.sh / CMake
```

`blink_app.c` references only `hal_gpio` / `hal_serial` / `hal_system` and the
portable `hal_get_reset_reason()` - no target symbols. The only per-board line
is the LED pin (`BLINK_LED_PIN`). The detailed Cortex-M fault dump lives solely
in the G474 entry (`g474/main.c`); on RP2040 you still get the portable reset
reason. Same source, two backends, graceful capability degradation.

| | RP2040 (Pico) | STM32G474 (Nucleo-G474RE) |
|---|---|---|
| Entry | `portable_blink.ino` (setup/loop) | `g474/main.c` (main + super-loop) |
| LED | GP25 | PA5 (LD2) |
| Console | USB serial @115200 | USART2 / ST-Link VCP @115200 |
| Build | arduino-cli / VS Code | `g474/build.sh` or CMake |

---

## Build for RP2040 (arduino-cli, Linux Mint / Debian-like)

```bash
sudo apt install arduino-cli            # or the official installer
arduino-cli core install rp2040:rp2040  # earlephilhower core
cd examples/portable_blink
arduino-cli compile --fqbn rp2040:rp2040:rpipico \
  --build-property "compiler.cpp.extra_flags=-I '$PWD' -I '$PWD/../../src'" \
  --build-property "compiler.c.extra_flags=-I '$PWD' -I '$PWD/../../src'" .
arduino-cli upload  --fqbn rp2040:rp2040:rpipico -p /dev/ttyACM0 .
```

Or open this folder in VS Code and run task **Build (RP2040)** (`Ctrl+Shift+B`).

## Build for STM32G474 / Nucleo-G474RE (Linux Mint / Debian-like)

### Host setup

```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio
sudo usermod -aG dialout "$USER"        # serial access; then log out/in
sudo udevadm control --reload-rules     # ST-Link/libusb access for st-flash
sudo udevadm trigger
# Unplug/replug the Nucleo/ST-Link after installing stlink-tools or reloading udev.
```

`dialout` is for the ST-Link virtual COM port (`/dev/ttyACM*`). `st-flash`
uses libusb and depends on the ST-Link udev rule instead. After adding yourself
to `dialout`, start a new login session or run:

```bash
newgrp dialout
```

### Build and flash

```bash
cd examples/portable_blink/g474
./build.sh                              # -> build/portable_blink_g474.{elf,bin,hex}
st-flash --reset write build/portable_blink_g474.bin 0x08000000
```

If the target is already in an odd state, use connect-under-reset:

```bash
st-flash --connect-under-reset --reset write build/portable_blink_g474.bin 0x08000000
```

(OpenOCD alternative: `openocd -f board/st_nucleo_g4.cfg -c "program build/portable_blink_g474.elf verify reset exit"`.)

### Serial console

The demo prints on USART2 through the ST-Link VCP at 115200 baud.

```bash
tio -L                                  # list known serial devices
tio /dev/ttyACM0 -b 115200
```

If `/dev/serial/by-id/` exists, prefer the stable ST-Link name:

```bash
tio /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_* -b 115200
```

Press the board RESET button after opening `tio` to see the startup banner
again. To leave `tio`, press `Ctrl-T`, then `Q`. To log output:

```bash
tio /dev/ttyACM0 -b 115200 -t -l --log-file nucleo.log
```

### STM32G474 troubleshooting

If `st-flash` prints `libusb requires write access to USB device nodes`, the
board is visible but the current USB device node was created before the ST-Link
udev rule applied. Reload udev, unplug/replug the board, then retry. The
`dialout` group affects only the serial console (`/dev/ttyACM*`), not
`st-flash`.

If `st-flash` says `Please use 'connect under reset'`, that is an `st-flash`
mode, not a shell command:

```bash
st-flash --connect-under-reset --reset write build/portable_blink_g474.bin 0x08000000
```

If the app runs after flashing but the physical RESET button seems to leave the
board silent, first try an ST-Link reset:

```bash
st-flash reset
```

If `st-flash reset` starts the app but the physical button still does not, check
BOOT0 wiring/jumpers and option bytes. Read option bytes without writing them:

```bash
st-flash --connect-under-reset --area=option read /tmp/g474_option.bin 64
hexdump -C /tmp/g474_option.bin
```

A healthy Nucleo-G474RE observed during bring-up returned:

```text
00000000  aa f8 ef fb
```

That corresponds to normal read protection level 0 (`0xAA`) and main-flash boot
settings. Do not write option bytes unless you know exactly what you are
changing.

Build warnings such as `_write is not implemented and will always fail` come
from `newlib-nano` + `nosys.specs`. They mean standard POSIX-style syscalls are
stubbed on bare metal. This demo prints through the dedicated USART2 debug
driver, so the warnings are harmless for blink. They can be removed later by
adding STM32G474 syscall stubs that route `_write` to USART2 and provide safe
minimal implementations for `_read`, `_sbrk`, `_exit`, etc.

## Expected output (both targets)

```
=== JaszczurHAL portable blink ===
UID: ....
reset reason: power-on
LED on  t=0 ms
LED off t=500 ms
...
```

LED blinks at **1 Hz**; `t=` advances at wall-clock rate. On STM32G474 a prior
crash additionally prints the retained fault frame (PC/CFSR/HFSR/BFAR) on the
next boot.
