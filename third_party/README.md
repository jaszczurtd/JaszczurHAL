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

`runmefirst.sh` invokes the updater after installing its host prerequisites.
Individual `scripts/ensure_*.sh` helpers remain available for focused workflows,
but the central updater is the normal entry point.

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

The Pico SDK submodules required by native builds are listed in
`PICO_SDK_SUBMODULES` in `pico_sdk_version.conf`. JaszczurHAL deliberately uses
the separately pinned `lwip/` checkout instead of the SDK's lwIP submodule.

External FreeRTOS or Pico SDK checkouts can still be selected through the
documented `JH_FREERTOS_KERNEL_DIR`, `JH_PICO_SDK_DIR`, and helper-script path
options. Such user-managed paths are verified but are never replaced.

## Built tools and toolchains

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

Its release identity is recorded inside the ignored installation so the updater
can replace a stale or unidentifiable toolchain. Native ARM targets continue to
use `arm-none-eabi`; they do not consume this RISC-V toolchain.

picotool needs `libusb-1.0-0-dev` and `pkg-config` for USB access. It builds
against the pinned Pico SDK and uses the SDK's Mbed TLS submodule for RP2350
hashing and signing support.
