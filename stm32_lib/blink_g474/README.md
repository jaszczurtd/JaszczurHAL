# STM32G474 bring-up: blink + exception-info

First **real** (non-stub) step of the JaszczurHAL STM32G474 backend. Targets
the **Nucleo-G474RE**.

## What this is

A small bare-metal firmware that exercises the newly real parts of the
`impl/stm32g474` backend:

| Capability | File | Status |
|---|---|---|
| Boot (vector table, `.data`/`.bss` init) | `port/startup_stm32g474.c` | real |
| Clock + 1 kHz SysTick time base | `port/system_stm32g474.c` | real (HSI 16 MHz) |
| `hal_millis/micros/delay` | `drivers/stm32g474/stm32g474_system.cpp` | real (SysTick) |
| GPIO digital out (`hal_gpio_*`) | `stm32g474/hal_gpio.cpp` | real |
| Debug console (USART2, ST-Link VCP) | `port/g474_debug_uart.c` | real |
| Fault capture (HardFault/Mem/Bus/Usage) | `port/exception_info.c` | real, modelled on teltonika-tdf `crash_dump` |
| Device UID (`UID_BASE`), `__WFI` idle | `drivers/stm32g474/stm32g474_system.cpp` | real |

The backend is chosen by the canonical target switch `-DHAL_TARGET_STM32G474`
(see `src/hal/hal_target.h`). Building for ARM then derives `JH_STM32G474_HW`,
which selects real register code over the host stubs — so the existing host
unit-test build (target auto-detected as `MOCK`) is unaffected.

## Pin / console map

- **LED**: LD2 = **PA5** → JaszczurHAL pin id `5` (`port A (0) * 16 + 5`).
- **Console**: **USART2** PA2/PA3 (AF7) → ST-Link Virtual COM Port, **115200 8N1**.

The flat `uint8_t` pin API is mapped as `pin = port_index*16 + pin_number`
(A=0..G=6), e.g. PB0 = 16, PC13 = 45.

## Quick start on Linux Mint / Debian-like (NUCLEO-G474RE)

Tested flow for Linux Mint 21/22 (and Ubuntu/Debian). All commands assume this
example directory:

```bash
cd JaszczurHAL/stm32_lib/blink_g474
```

### 1. Install prerequisites

```bash
sudo apt update
# ARM bare-metal toolchain + build tools
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi cmake build-essential
# Flashing (ST-Link) + a serial terminal
sudo apt install stlink-tools tio        # 'tio' optional; minicom/picocom also fine
```

Verify the toolchain is on PATH:

```bash
arm-none-eabi-gcc --version   # any 10.x–13.x works
st-info --version
```

> If `apt`'s `gcc-arm-none-eabi` is too old or missing, install the official
> Arm GNU toolchain and prepend it to PATH:
> ```bash
> export PATH=/opt/arm-gnu-toolchain-*/bin:$PATH
> ```

### 2. One-time permissions (so flashing / serial work without sudo)

```bash
# Serial access (Virtual COM Port shows up as /dev/ttyACM0):
sudo usermod -aG dialout "$USER"
# stlink-tools ships udev rules; reload them:
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Log out and back in (or reboot) for the `dialout` group to take effect, then
plug the NUCLEO-G474RE into USB (the **ST-LINK** USB connector, CN1).

### 3. Build

```bash
./build.sh                  # normal blink
FAULT_TEST=1 ./build.sh     # variant that forces a fault to test capture

# or via CMake:
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../toolchain_stm32g474.cmake
cmake --build build
```

Output: `build/blink_g474.{elf,bin,hex,map}`.

### 4. Flash

```bash
# Check the probe is seen:
st-info --probe          # should list a STM32G4 / 0x469 chip id

# Flash the raw binary to flash base:
st-flash --reset write build/blink_g474.bin 0x08000000
```

Alternatives:

```bash
# OpenOCD (sudo apt install openocd):
openocd -f board/st_nucleo_g4.cfg \
        -c "program build/blink_g474.elf verify reset exit"

# Official ST CLI, if installed:
STM32_Programmer_CLI -c port=SWD -w build/blink_g474.bin 0x08000000 -rst
```

### 5. Observe the serial console

```bash
tio /dev/ttyACM0 -b 115200
#   or:  minicom -D /dev/ttyACM0 -b 115200
#   or:  picocom -b 115200 /dev/ttyACM0      (exit: Ctrl-A Ctrl-X)
#   or:  screen /dev/ttyACM0 115200          (exit: Ctrl-A K)
```

Expected output (press the black **RESET** button if you connected the
terminal after flashing):

```
=== JaszczurHAL STM32G474 bring-up ===
UID: 0011223344556677
Clean boot (no prior fault).
LED on  t=0 ms
LED off t=500 ms
LED on  t=1000 ms
...
```

**Pass criteria:**
- The green user LED **LD2 (PA5)** blinks at **1 Hz** → clock + GPIO are real.
- The `t=` value advances at wall-clock rate (500 ms per line) → the SysTick
  time base is real, not the old `g_millis += ms` stub.
- UART text appears → console path works.

**Fault self-test** (`FAULT_TEST=1` build): after ~6 toggles the firmware
dereferences an illegal address. The fault handler prints the live dump,
resets, and the **next boot** reports the retained record:

```
Triggering fault self-test...
*** FAULT *** kind=3 PC=0x080001AA LR=0x... xPSR=0x...
CFSR=0x00000400 HFSR=0x40000000 MMFAR=... BFAR=0xFFFFFFF0
Resetting...
=== JaszczurHAL STM32G474 bring-up ===
Last reset was a FAULT: BUSFAULT
  PC   =0x080001AA
  ...
```

### Troubleshooting

| Symptom | Fix |
|---|---|
| `st-info --probe` finds nothing | Use the CN1 (ST-LINK) USB port, not CN... power-only; check cable; `dmesg \| tail` should show `ttyACM0`. |
| Permission denied on `/dev/ttyACM0` | `dialout` group not active yet — log out/in; confirm with `groups`. |
| `st-flash` permission error | Reload udev rules (step 2) or run once with `sudo`. |
| Terminal silent but LED blinks | Wrong port/baud, or terminal opened before reset — press RESET (B2). |
| `arm-none-eabi-gcc: command not found` | Install toolchain or prepend the official Arm toolchain to PATH (step 1). |

## Next steps (backend growth)

EXTI/GPIO interrupts → real `hal_uart` RX (IRQ/DMA) → I2C/SPI/ADC/PWM via LL →
IWDG watchdog → PLL clock (170 MHz). At the point richer peripherals are
needed, pull STM32CubeG4 LL drivers and retire the hand-written
`port/stm32g474_regs.h`.
