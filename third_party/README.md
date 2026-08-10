# Managed Third-Party Components

This directory contains two kinds of entries:

- tracked `*_version.conf` files that pin every managed component;
- git-ignored source, tool, and toolchain installations reconstructed from
  those pins.

Synchronize all components with:

```bash
./third_party/update_components.sh
```

The updater fetches missing components and replaces any installation whose
version or Git commit differs from its tracked configuration. To check the
current installations without changing them, run:

```bash
./third_party/update_components.sh --verify-only
```

`runmefirst.sh` invokes the updater after installing its Linux host
prerequisites. `runmefirst.ps1` uses the same Python component manager for
native Windows. Individual `scripts/ensure_*.sh` helpers remain available as
focused Unix compatibility launchers, while the central updater is the normal
entry point.

## Source dependencies

| Component | Pin | Managed checkout | Purpose |
|-----------|-----|------------------|---------|
| BearSSL | `bearssl_version.conf` | `BearSSL/` | TLS engine used by host, RP and STM32 builds |
| cJSON | `cjson_version.conf` | `cJSON/` | JSON parser and utility API |
| LodePNG | `lodepng_version.conf` | `lodepng/` | Memory-oriented PNG codec |
| TJpg_Decoder | `jpeg_version.conf` | `TJpg_Decoder/` | Tiny JPEG Decompressor core for RGB565 output |
| FatFs | `fatfs_version.conf` | `FatFs/` | FAT filesystem core used by shared SD storage |
| Unity | `unity_version.conf` | `Unity/` | Test framework used by host and target-side test builds |
| lwIP | `lwip_version.conf` | `lwip/` | TCP/IP stack used by the JaszczurHAL CYW43 integration |
| littlefs | `littlefs_version.conf` | `littlefs/` | Filesystem core used by native RP and STM32G474 storage |
| BTstack | `btstack_version.conf` | `BTstack/` | BLE host stack used by the CYW43 Bluetooth integration |
| Semtech SX126x driver | `sx126x_driver_version.conf` | `sx126x_driver/` | Portable SX1261/SX1262 command driver for the LoRa provider |
| FreeRTOS-Kernel | `freertos_core_version.conf` | `FreeRTOS-Kernel/` | Native RP SMP and STM32G474 FreeRTOS kernel |
| Pico SDK | `pico_sdk_version.conf` | `pico-sdk/` | Native RP2040/RP2350 SDK |
| picotool | `picotool_version.conf` | `picotool/` | Source for the native RP upload/metadata utility |

JaszczurHAL-owned BearSSL, cJSON, LodePNG, JPEG, FatFs and Unity integration
wrappers, along with the lwIP port configuration, remain tracked under
`src/hal/impl/shared/frameworks/`. Their upstream source trees are managed
here. The cJSON, LodePNG, TJpg_Decoder and Unity helpers require clean
exact-commit checkouts; verify-only mode rejects local or untracked changes.
Configured repository origins are enforced, including the project-owned
BearSSL, LodePNG, FatFs and Unity repositories. The `jaszczurtd/ff16` checkout
is a direct mirror of ChaN's unchanged R0.16 archive and replaces the unreliable
runtime download from `elm-chan.org`. The littlefs checkout is consumed directly
by the native RP and STM32G474 CMake recipes; target-specific flash adapters
remain tracked in their respective backend directories.

The Semtech checkout is kept clean at the exact `v2.5.0` commit. Its tracked
Clear BSD license copy is `LICENSE.SX126X`. Stage 1 LoRa integration will use
only `sx126x.c` and `sx126x_driver_version.c`; optional LR-FHSS and BPSK source
sets remain excluded until separately reviewed.

The Pico SDK submodules required by native builds are listed in
`PICO_SDK_SUBMODULES` in `pico_sdk_version.conf`. JaszczurHAL deliberately uses
the separately pinned `lwip/` checkout instead of the SDK's lwIP submodule.

External FreeRTOS or Pico SDK checkouts can still be selected through the
documented `JH_FREERTOS_KERNEL_DIR`, `JH_PICO_SDK_DIR`, and helper-script path
options. Such user-managed paths are verified but are never replaced.

## Built tools and toolchains

PMD 7.26.0 is installed from its authenticated binary ZIP below
`third_party/pmd/`. The component manager verifies the archive digest, complete
extracted-file manifest, and reported PMD version. A system Java runtime is the
only host requirement; `runmefirst.sh` installs `default-jre-headless`.
`scripts/run_cpd.py` owns the source scope and thresholds used identically by
`runalltests.sh` and CI.

picotool sources live in `third_party/picotool`, while all generated build
artifacts and the executable live under:

```text
.build/tools/picotool/
```

The `rp2350-riscv` target uses the prebuilt toolchain pinned by
`riscv_toolchain_version.conf` and installed at:

```text
third_party/riscv-toolchain/bin/riscv32-unknown-elf-gcc
```

Its release identity and complete extracted-file manifest are recorded inside
the ignored installation so the updater can replace stale or modified content.
The pin includes authenticated x86-64/AArch64 Linux assets and the native AMD64
Windows ZIP. Native ARM targets continue to use `arm-none-eabi`; they do not
consume this RISC-V toolchain.

Native Windows host archives are pinned in `windows_tools_version.conf`.
`runmefirst.ps1` places managed Python, CMake, Ninja, GNU Arm, OpenOCD,
picotool, and RISC-V tools below a short user-local root and records resolved
executables in `resolved-tools.json`.

picotool needs `libusb-1.0-0-dev` and `pkg-config` for USB access. It builds
against the pinned Pico SDK and uses the SDK's Mbed TLS submodule for RP2350
hashing and signing support. On both Linux and Windows, verification requires
the expected `load`, `verify`, and `reboot` commands. A Linux installation is
rebuilt when newly available libusb or SDK Mbed TLS support exposes a missing
USB or `seal` capability instead of accepting a version-only match.
