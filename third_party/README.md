# Third-Party Dependencies

## FreeRTOS-Kernel

STM32G474 FreeRTOS builds expect a local upstream FreeRTOS kernel checkout at:

```text
third_party/FreeRTOS-Kernel/
```

The checkout is fetched or verified by:

```bash
./scripts/ensure_freertos_kernel.sh --enable
```

The pinned repo/ref live in `../freertos_core_version.conf`; the fetched
directory is ignored by git and should not be committed.

The CMake integration uses this exact kernel layout:

```text
FreeRTOS-Kernel/include/
FreeRTOS-Kernel/portable/GCC/ARM_CM4F/
FreeRTOS-Kernel/portable/MemMang/heap_4.c
```

Projects may keep the checkout elsewhere and pass:

```bash
-DJH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel
```

or set:

```bash
JH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel
```

The RP2040 backend does not use this directory; it relies on
arduino-pico's built-in FreeRTOS mode instead.

## Pico SDK

The native RP2040 / RP2350 backend builds directly against the official
upstream Pico SDK (raspberrypi/pico-sdk), decoupled from the arduino-pico
carrier. The checkout is expected at:

```text
third_party/pico-sdk/
```

The checkout is fetched or verified by:

```bash
./scripts/ensure_pico_sdk.sh --enable
```

The pinned repo/ref/version live in `../pico_sdk_version.conf`; the fetched
directory is ignored by git and should not be committed. `runmefirst.sh` runs
this step automatically, alongside the FreeRTOS kernel.

The native build consumes the checkout via `PICO_SDK_PATH`:

```bash
export PICO_SDK_PATH="$(pwd)/third_party/pico-sdk"
```

or point the build elsewhere:

```bash
./scripts/ensure_pico_sdk.sh --enable --sdk-dir /path/to/pico-sdk
# or
export JH_PICO_SDK_DIR=/path/to/pico-sdk
```

SDK submodules needed by the native build are listed in `PICO_SDK_SUBMODULES`:
`lib/tinyusb` (native `hal_usb` takeover) and `lib/mbedtls` (the mbedtls target
picotool uses for RP2350 hashing/signing). lwIP and the CYW43 driver are
intentionally excluded because JaszczurHAL vendors its own. The arduino-pico
backend does NOT use this directory.

## picotool

picotool is NOT part of the Pico SDK - it is a separate upstream repo that must
be BUILT (it links libusb and uses the SDK). The native RP2040/RP2350 flashing
workflow (UF2 upload, reboot, binary-info, RP2350 signing) needs it. Checkout +
build live at:

```text
third_party/picotool/          # source
third_party/picotool/build/picotool   # built binary
```

Fetched and built by:

```bash
./scripts/ensure_picotool.sh --enable
```

The pinned repo/ref/version live in `../picotool_version.conf` (kept in step with
the Pico SDK; SDK 2.2.0 requires picotool >= 2.1.1). The directory is git-ignored
and not committed. `runmefirst.sh` builds it automatically after the Pico SDK.

Requires the pinned Pico SDK (run `ensure_pico_sdk.sh` first) plus
`libusb-1.0-0-dev` + `pkg-config` for USB device access. Build against a
different SDK with `--sdk-dir`, or a different checkout with `--picotool-dir`.

Signing/hashing (RP2350 signed images / secure boot) comes from the Pico SDK's
mbedtls target - enabled by `PICO_SDK_SUBMODULES=lib/mbedtls` in
`pico_sdk_version.conf` (initialised by `ensure_pico_sdk.sh`), not by picotool
itself. The script rebuilds picotool automatically when USB support or SDK
signing support is missing from an existing build.

## RISC-V toolchain (rp2350-risc-v)

The `rp2350-risc-v` target (RP2350 Hazard3 core) needs a bare-metal RISC-V GCC
with the triple the Pico SDK auto-detects (`riscv32-unknown-elf`). The ARM
targets (`rp2040`, `rp2350-arm`) use `arm-none-eabi` and do NOT need this.
Checkout:

```text
third_party/riscv-toolchain/bin/riscv32-unknown-elf-gcc
```

Fetched or verified by:

```bash
./scripts/ensure_riscv_toolchain.sh --enable
```

The pinned release/version live in `../riscv_toolchain_version.conf` - the
official upstream Raspberry Pi prebuilt (`raspberrypi/pico-sdk-tools` releases,
co-versioned with the SDK), independent of arduino-pico. The directory is
git-ignored; `runmefirst.sh` fetches it after picotool. The native rp2350-risc-v
build points the SDK at it:

```bash
export PICO_TOOLCHAIN_PATH="$(pwd)/third_party/riscv-toolchain"
```
