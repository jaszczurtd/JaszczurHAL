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
| ESP-IDF | `esp_idf_version.conf` | `esp-idf/` | Native ESP32-family SDK and tool bootstrap |
| picotool | `picotool_version.conf` | `picotool/` | Source for the native RP upload/metadata utility |

JaszczurHAL-owned BearSSL, cJSON, LodePNG, JPEG and FatFs integration wrappers,
along with the lwIP port configuration, remain tracked in their thematic
`src/hal/network/`, `src/hal/codecs/`, and `src/hal/storage/` domains. Unity
integration remains in the test infrastructure. Their upstream source trees
are managed here. The cJSON, LodePNG, TJpg_Decoder and Unity helpers require clean
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

ESP-IDF is pinned to an exact release commit and fetched on demand with
`scripts/ensure_esp_idf.sh --enable`. Its recursive submodules are part of the
verified checkout set. The same command idempotently runs the official
ESP-IDF installer for `ESP_IDF_TARGETS`, then verifies the toolchain and Python
environment. Set `JH_ESP_IDF_DIR` or pass `--dir` to verify and use an external
checkout without replacing it. Source `third_party/esp-idf/export.sh` in each
terminal that invokes ESP-IDF tools directly.

The production runner accepts a project directory, resolves the
`esp32s3` target and `waveshare-esp32-s3-zero` board descriptors, prepares the
pinned SDK environment, and validates the complete build requirements:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/fixtures/esp32s3_phase3 \
  --target esp32s3 --board waveshare-esp32-s3-zero --clean
```

This is the compile/link fixture used by CI and the local gate. It validates
the complete delivered ESP32-S3 feature graph and artifacts without claiming
runtime hardware acceptance.

The runner generates `sdkconfig` defaults from the board flash/PSRAM facts,
uses the shared `app_main()` entry and generated feature-resolved ESP-IDF
component graph, rejects features outside the target's allowlist, and emits the
relocatable `jh_esp_idf_artifacts.json` manifest. That manifest records every flash image
with its offset, size, and SHA-256, the partition-table profile, the final
`sdkconfig` digest, the exact ESP-IDF commit, and actual compiler, CMake, Ninja,
Python, esptool, and ESP-IDF tool-registry provenance. The default output is
`<project>/.build/esp-idf/<target>/<board>/`; `--output` may select another
directory below the project or repository `.build` root.

`scripts/build_esp_idf_phase0.py` remains a thin compatibility wrapper around
that runner for the isolated Phase 0 fixture. New project and CI commands use
`scripts/build_esp_idf.py`. ESP32-S3 includes baseline system/sync/GPIO/ADC/
serial/simple-PWM/timer backends. Its feature-resolved component graph adds
UART, I2C controller/target, SPI, PWM_FREQ, RMT/RGB, PCNT, stack-guard
configuration, native connectivity/services, and optional APP_TASK1 dispatch.

`security/esp_idf_tools.json` is the reviewed snapshot of the pin-selected tool
environment. It records six binary/data tools and eleven first-party Espressif
Python tools declared by the core ESP-IDF requirements, with exact versions,
upstreams, and normalized SPDX licenses. The SBOM generator consumes this
snapshot directly; the tool entries are not duplicated in
`security/third_party.json`.

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
