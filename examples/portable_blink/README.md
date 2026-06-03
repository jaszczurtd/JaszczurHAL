# portable_blink — one demo, two real targets

Proves JaszczurHAL portability concretely: the **same** application logic
([`blink_app.c`](blink_app.c), portable `hal_*` only) is built and run on
**RP2040** and **STM32G474** from a single example folder.

```
portable_blink/
  blink_app.h / blink_app.c   # portable logic (hal_* only) — shared by both
  portable_blink.ino          # RP2040/Arduino entry: setup()/loop()
  hal_project_config.h        # target/flags (auto-detect on Arduino)
  .vscode/                    # tasks: "Build (RP2040)" and "Build (STM32G474)"
  g474/                       # STM32G474 entry: main() + build.sh / CMake
```

`blink_app.c` references only `hal_gpio` / `hal_serial` / `hal_system` and the
portable `hal_get_reset_reason()` — no target symbols. The only per-board line
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

## Build for STM32G474 (Linux Mint / Debian-like)

```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio
sudo usermod -aG dialout "$USER"        # then log out/in (serial access)

cd examples/portable_blink/g474
./build.sh                              # -> build/portable_blink_g474.{elf,bin,hex}
st-flash --reset write build/portable_blink_g474.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

(OpenOCD alternative: `openocd -f board/st_nucleo_g4.cfg -c "program build/portable_blink_g474.elf verify reset exit"`.)

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
