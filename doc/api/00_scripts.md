# JaszczurHAL Process Scripts

This document is the central index for scripts that set up, build, validate,
package, and operate JaszczurHAL. It covers every script under `scripts/` and
the main process entrypoints located elsewhere in the repository.

Run commands from the repository root unless a section says otherwise. The
script implementation and its `--help` output are authoritative when this
document and the code disagree.

## Main Entry Points

| Goal | Command | Result |
|---|---|---|
| Prepare a Debian/Ubuntu workstation | `./runmefirst.sh` | Installs host, ARM, analysis, security, USB, and VS Code workflow prerequisites; synchronizes managed components; configures Git hooks. |
| Synchronize managed dependencies | `./third_party/update_components.sh` | Fetches missing components and replaces managed installations that differ from tracked pins. |
| Verify dependencies without changing them | `./third_party/update_components.sh --verify-only` | Checks all managed component versions, commits, required files, built picotool, and the RISC-V toolchain stamp. |
| Run the complete repository gate | `./runalltests.sh` | Cleans managed gate outputs and runs tests, Valgrind, static analysis, target builds, and example builds. |
| Operate a firmware project | `vscode/entry/jh-vscode <action> --project <dir>` | Provides the stable build, upload, monitor, board-selection, IntelliSense, and clean CLI used by VS Code projects. |
| Build checked-in examples | `scripts/examples_dispatcher.py build --target <target>` | Builds example manifests through the same `jh-vscode` and CMake dispatcher used by firmware projects. |
| Build native RP parity fixtures | `scripts/build_rp_native_parity_fixtures.sh` | Builds USB multicore and SDLogger probes for all supported native target/runtime combinations. |

### Artifact policy

Repository-owned generated build artifacts belong below `.build/`; managed
component installations belong below `third_party/`. The directory model,
target/board cache isolation, and generated-file ownership are defined in
[Build Directories And Generated Files](../FwProjectWorkflow.md#build-directories-and-generated-files).

## Repository-Level Orchestrators

These scripts are intentionally outside `scripts/` because they are top-level
workflow entrypoints.

### `runmefirst.sh`

One-time, idempotent setup for Debian/Ubuntu-like systems. It:

- removes the repository `.build/` tree before setup;
- installs compiler, CMake, Python, Valgrind, clang-tidy, cppcheck, OpenOCD,
  serial, libusb, and other host packages;
- invokes `third_party/update_components.sh`;
- installs `osv-scanner` and `cve-bin-tool`;
- installs a udev rule for RP2040/RP2350 USB access;
- checks for a persistent LAN-scoped OTA TCP/8266 callback rule and asks before
  changing the firewall or installing `iptables-persistent`;
- configures the repository Git hooks;
- verifies that every required tool is available.

The script uses `sudo` for system packages, `/usr/local/bin`, the udev rule,
and an explicitly approved firewall change. It downloads tools and
dependencies, so it requires network access. The focused firewall helper is
`scripts/configure_ota_firewall.py`; it supports `--check`, explicit
`--interface` / `--network`, and confirmed or `--yes` provisioning.

### `third_party/update_components.sh`

The normal dependency-management entrypoint. It invokes all twelve
`scripts/ensure_*.sh` helpers in dependency order:

1. BearSSL
2. cJSON
3. LodePNG
4. TJpg_Decoder
5. FatFs
6. Unity
7. lwIP
8. littlefs
9. FreeRTOS-Kernel
10. Pico SDK
11. picotool
12. RISC-V toolchain

Normal mode makes each managed installation match its tracked configuration.
`--verify-only` performs no fetch, extraction, checkout replacement, or build.
See [Managed Third-Party Components](../../third_party/README.md) for the pin and
directory contract.

### `runalltests.sh`

The complete local quality gate. `-j N`, `--jobs N`, and `-jN` select build
parallelism. The seven gates are:

1. required tools and managed-component verification;
2. host tests, including the optional FreeRTOS POSIX suite;
3. Valgrind memcheck;
4. cppcheck;
5. clang-tidy for host/shared code and the STM32 backend;
6. STM32, RP2040/RP2350, native FreeRTOS, and RP feature-profile builds;
7. every declared RP example, native parity fixture builds, and STM32
   examples.

The script removes only its managed `.build/gate`, `.build/examples`, and
`.build/tests` trees at startup. It exits on the first failed gate.

### `vscode/entry/jh-vscode`

The stable firmware-project CLI used by project tasks and the example
dispatcher. Its actions, options, exit codes, device safeguards, and monitor
behavior are documented only in
[JaszczurHAL VS Code Entry](../../vscode/README.md). Manifest, source-discovery,
target, board, cache, and artifact semantics belong to
[Firmware Project Workflow](../FwProjectWorkflow.md).

## Build Scripts

### `scripts/build_rp_native_lib.sh`

Builds JaszczurHAL with the official Pico SDK. Supported targets are:

| Script target | Pico SDK platform | Default board |
|---|---|---|
| `rp2040` | `rp2040` | `pico` |
| `rp2350-arm` | `rp2350-arm-s` | `pico2` |
| `rp2350-riscv` | `rp2350-riscv` | `pico2` |

The script ensures the Pico SDK and picotool. It additionally ensures
FreeRTOS-Kernel for `--freertos` and the RISC-V toolchain for
`rp2350-riscv`. It can build a portable application with
`--example <directory>`.

Each build verifies the static library, ELF/BIN/UF2 artifact probes, core-entry
symbols, and optional example firmware. Default output is
`.build/static/<target>/<board>/`.

Important options are `--target`, `--platform`, `--board`, `--sdk-dir`, `--toolchain`,
`--picotool-dir`, `--picotool-build-dir`, `--example`, `--freertos`,
`--project-config`, repeatable `-D`, `--output`, `--clean`, and `--jobs`.
Both build output directories must remain below `.build/`.

### `scripts/build_stm32_lib.sh`

Builds the STM32G474 static library with the GNU Arm embedded toolchain.
It accepts project configuration, repeatable HAL definitions, a custom CMake
toolchain file, and an optional FreeRTOS-Kernel path.

Default output:

```text
.build/static/stm32g474/nucleo-g474re/libJaszczurHAL.a
```

`--freertos`, or an explicit `HAL_ENABLE_FREERTOS` definition, invokes
`ensure_freertos_kernel.sh` before CMake configuration. Important options are
`--project-config`, repeatable `-D`, `--freertos`, `--freertos-kernel`,
`--output`, `--toolchain`, `--clean`, and `--jobs`.

### `scripts/build_rp_native_parity_fixtures.sh`

Builds `tests/hardware/rp_usb_multicore` and
`tests/hardware/rp_sdlogger` through the normal `jh-vscode` workflow for:

- RP2040/Pico;
- RP2350 ARM/Pico 2;
- RP2350 RISC-V/Pico 2;
- bare-metal and FreeRTOS on every target.

It cleans only the two managed fixture build trees below `.build/hardware/`.
`--jobs N` controls CMake parallelism. The script is a compile gate; running
the corresponding Python verifiers still requires physical boards and, for
SDLogger, an SPI SD card.

### `scripts/lib/build_artifacts.sh`

Internal shell module sourced by all three build helpers. It defines:

- `jh_build_root <repo>` to normalize `<repo>/.build`;
- `jh_resolve_build_output <repo> <requested> <default-relative>` to normalize
  an output path and reject anything outside `.build/`.

It is a library, not a standalone command.

For full target requirements, options, outputs, and manual CMake equivalents,
see [JaszczurHAL Library Compilation](../lib_compilation.md).

## Managed-Component Scripts

The focused helpers read tracked pins from `third_party/*_version.conf`.
Normally use `third_party/update_components.sh`; call an individual helper only
for a focused build or diagnostic.

### Common checkout behavior

Git-backed managed directories are exact-commit installations. A missing
directory is cloned at the pinned ref. A directory at another commit, or a
non-Git directory in the managed location, is replaced. `--verify-only`
reports a mismatch without modifying it.

Archive-backed managed directories use an exact SHA-256 pin and a deterministic
manifest of the extracted files. A missing or modified installation is replaced
in normal mode and rejected by `--verify-only`.

User-provided FreeRTOS, Pico SDK, and picotool paths are treated as externally
managed. They are verified and are not replaced by their focused helpers.

### `scripts/ensure_bearssl.sh`

Synchronizes `third_party/BearSSL` from
`third_party/bearssl_version.conf`, verifies the exact commit and required
headers/sources, and supports `--verify-only`, `--repo-root`, and `--dir`.
`--enable` and `--force` are accepted for a uniform updater interface.

### `scripts/ensure_cjson.sh`

Synchronizes `third_party/cJSON` from `third_party/cjson_version.conf` and
verifies the exact commit, license, core sources and utility sources. Options
mirror the BearSSL helper.

### `scripts/ensure_lodepng.sh`

Synchronizes `third_party/lodepng` from `third_party/lodepng_version.conf` and
verifies the clean exact commit, license, header and implementation. Options
mirror the BearSSL helper.

### `scripts/ensure_jpeg.sh`

Synchronizes `third_party/TJpg_Decoder` from `third_party/jpeg_version.conf`
and verifies the clean exact commit, license and Tiny JPEG Decompressor core.
Options mirror the BearSSL helper.

### `scripts/ensure_fatfs.sh`

Synchronizes `third_party/FatFs` from ChaN's official R0.16 archive recorded in
`third_party/fatfs_version.conf`. It authenticates the download with SHA-256,
verifies the complete extracted tree plus required source and license files,
and supports `--verify-only`, `--repo-root`, and `--dir`.

### `scripts/ensure_unity.sh`

Synchronizes `third_party/Unity` from `third_party/unity_version.conf` and
verifies the clean exact commit, repository origin, license and core framework
sources. Options mirror the BearSSL helper.

### `scripts/ensure_lwip.sh`

Synchronizes `third_party/lwip` from `third_party/lwip_version.conf`.
In addition to the exact commit and required paths, it verifies the lwIP
major/minor/revision macros against the configured version. Options mirror the
BearSSL helper.

### `scripts/ensure_littlefs.sh`

Synchronizes `third_party/littlefs` from
`third_party/littlefs_version.conf`. It verifies the exact commit, required
core sources and license, and the configured littlefs major/minor API version.
The native RP and STM32G474 builds compile this managed checkout directly.
Options mirror the BearSSL helper.

### `scripts/ensure_freertos_kernel.sh`

Synchronizes or verifies FreeRTOS-Kernel and its required RP/STM32 ports.
Without an enable condition it is a no-op. It runs when:

- `--enable`, `--freertos`, `--force`, or `--verify-only` is passed;
- `EXTRA_HAL_DEFINES` contains `HAL_ENABLE_FREERTOS`; or
- `HAL_ENABLE_FREERTOS` is present in the environment.

`--kernel-dir` and `JH_FREERTOS_KERNEL_DIR` select an external checkout that is
verified but not replaced. Managed submodules and the kernel version are also
checked.

### `scripts/ensure_pico_sdk.sh`

Synchronizes or verifies the official Pico SDK and initializes the submodules
listed in `third_party/pico_sdk_version.conf`. It is enabled through
`--enable`, `--native`, `--force`, `--verify-only`, or
`JH_ENABLE_PICO_SDK`.

`--sdk-dir` and `JH_PICO_SDK_DIR` select an external checkout.
`--no-submodules` skips configured submodule initialization, while
`--with-submodules "A B"` overrides the list for that invocation.

### `scripts/ensure_picotool.sh`

Synchronizes picotool sources and builds the executable against the selected
Pico SDK. Source lives under `third_party/picotool`; generated files and the
executable default to `.build/tools/picotool/`.

It rebuilds when the source checkout changes, the reported picotool version is
wrong, USB support becomes available, or the SDK now provides signing support
that an older build lacks. `--rebuild` forces a clean rebuild.
`--verify-only` checks both source and executable without changing them.

The helper is enabled through `--enable`, `--build`, `--force`,
`--verify-only`, `--rebuild`, or `JH_ENABLE_PICOTOOL`. Its build directory is
required to stay below `.build/`.

### `scripts/ensure_riscv_toolchain.sh`

Installs the pinned Raspberry Pi prebuilt
`riscv32-unknown-elf` toolchain for the native `rp2350-riscv` target. It maps
the host architecture to the matching release asset, extracts the archive into
`third_party/riscv-toolchain`, records a component stamp, and verifies the GCC
major version.

If the executable or stamp differs from the tracked configuration, normal mode
replaces the installation. `--verify-only` performs no download or extraction.
The helper supports x86-64 and AArch64 Linux release assets.

### `scripts/lib/pinned_repo.sh`

Internal shell library shared by the Git-backed component helpers. It provides
colored diagnostics and functions to:

- shallow-fetch an exact ref;
- clone a pinned checkout;
- synchronize or replace a managed checkout;
- verify a checkout ref and required paths;
- initialize selected submodules.

It has no side effects when sourced and is not a standalone command.

## Example And VS Code Support Scripts

### `scripts/examples_dispatcher.py`

Owns the checked-in example registry and exposes three subcommands:

| Command | Behavior |
|---|---|
| `generate` | Regenerates each example's manifest, VS Code settings, tasks, launch configuration, and keybinding reference. |
| `list` | Prints every registered example and its expanded target list. |
| `build` | Builds supported examples and variants through `vscode/entry/jh-vscode`. |

`build` requires `--target` with one of `rp2040`, `rp2350-arm`,
`rp2350-riscv`, or `stm32g474`. Repeatable `--example` limits the run,
`--jobs` controls parallel example projects, and `--verbose` records invoked
commands in per-example logs under `/tmp`.

The Python registry is the source used by `generate`; the generated manifests
are the source consumed by `build`. RISC-V WiFi examples remain excluded while
RP2350 RISC-V + CYW43 is unsupported.

See [JaszczurHAL Examples](../../examples/README.md) for the target matrix,
application contract, and build commands.

### `scripts/configure_ota_firewall.py`

Idempotently inspects and configures persistent inbound TCP access for the
host-side OTA callback. It selects the RFC1918 network on the default IPv4
interface, scopes the rule to that interface and subnet, and defaults to
TCP/8266. Active UFW and firewalld installations use their native persistent
configuration; the fallback uses `iptables-nft`/`iptables` with
`iptables-save` plus the `netfilter-persistent` boot loader, enabled through
systemd when available.
An unfiltered `INPUT` policy already permits the callback and requires no
additional package or rule.

Interactive mode prints the full rule scope and asks before making a change.
`--check` is read-only, `--interface` and `--network` override automatic route
detection, `--port` selects a different fixed callback port, and `--yes`
supports deliberate non-interactive provisioning. Only RFC1918 IPv4 networks
are accepted, and setup refuses to expose a port already used by a listener.

### `scripts/rp_ota_artifacts.py`

Internal native RP firmware packaging helper used by CMake. `package` wraps an
application BIN in the versioned JaszczurHAL OTA header with target, load
offset, generation, version and payload SHA-256. The HMAC field remains
unsigned until the VS Code upload action applies the project password.
`merge-uf2` combines the copy-to-RAM boot applier UF2 with the application UF2,
rejecting conflicting address blocks and normalizing block numbering. Build
artifacts remain below the resolved `.build/` directory. See
[Native RP OTA Workflow](../OTAWorkflow.md) for the complete consumer workflow
around these artifacts.

### `scripts/vscode_refresh_intellisense.sh`

Repository-workspace helper used by the root `.vscode/tasks.json`. It accepts
exactly one profile:

- `rp2040` configures the official Pico SDK RP2040/Pico recipe;
- `stm32` configures the STM32G474 static-library recipe.

It generates a compile database below `.build/intellisense/` and updates
`.vscode/settings.json` so cpptools uses that database.

This is not the firmware-project workflow. Dispatcher-backed projects should
use `jh-vscode refresh-intellisense --project <dir>`.

### `scripts/vscode_clear_build_artifacts.sh`

Repository-workspace clean helper used by the root VS Code task. It removes the
entire repository `.build/` tree and nothing outside it. There are no options.
This also removes cached target builds, examples, tests, IntelliSense data, and
the built picotool executable; ignored component sources under `third_party/`
are retained.

## Static Analysis And Security Scripts

### `scripts/clang_tidy_files.py`

Reads a CMake `compile_commands.json`, selects JaszczurHAL-owned source files,
deduplicates repeated compile entries, and prints anchored file regexes for
`run-clang-tidy`.

Required options are `--build-dir` and `--profile host|stm32`.
`--repo-root` controls path classification. `--output-compile-db` writes the
filtered deterministic database. For STM32 entries, the script adds an
Arm-none-EABI clang target and compiler-reported system includes.

This is an internal quality-gate helper called by `runalltests.sh`, not a
general formatter.

### `scripts/check_documentation_links.py`

Validates repository-local Markdown link targets and anchors across maintained
documentation. The host CTest suite registers it as
`test_documentation_links`, so the normal local and CI test gates reject broken
documentation links.

Run it directly with:

```bash
python3 scripts/check_documentation_links.py .
```

An optional positional argument selects a different repository root.

### `scripts/generate_sbom.py`

Generates a deterministic CycloneDX 1.5 SBOM. It reads
`security/third_party.json`, resolves the JaszczurHAL version from
`VERSION`, and writes `security/sbom.cdx.json`.

`--inventory` and `--output` override the default input and output paths. The
generator uses only the Python standard library.

### `scripts/check_sbom.sh`

Generates the SBOM into a temporary file and compares it byte-for-byte with
the tracked `security/sbom.cdx.json`. It does not modify the tracked SBOM.
A mismatch prints a diff and fails. This is the CI freshness check.

### `scripts/check_vulnerabilities.sh`

Regenerates the tracked SBOM, then runs scanners that are already installed:

- `osv-scanner` scans the repository source recursively;
- when `JH_SECURITY_SCAN_SOURCE=1`, `cve-bin-tool` scans the generated
  CycloneDX SBOM.

The script searches both `PATH` and `~/.local/bin`, does not install scanners,
and warns rather than failing solely because no scanner is available. Scanner
findings and scanner execution failures still propagate as command failures.

See [Security Supply Chain](../security_supply_chain.md) for inventory, SBOM, CI,
triage, and component-update policy.

## Asset Script

### `scripts/image_to_base64.py`

Reads any binary image, Base64-encodes it, and emits a valid C
`static const char[]` declaration. The positional argument is the input image.
Useful options are:

- `--output` / `-o` to write a file instead of standard output;
- `--name` / `-n` to select a valid C identifier;
- `--line-width` to control generated string-literal wrapping.

The misspelled `--otput` option remains a compatibility alias; new commands
should use `--output`.

PNG use is documented in [LodePNG API](18_LodePNG.md#asset-script-png-to-base64).
JPEG use is documented in [JPEG API](19_JPEG.md#asset-script-jpeg-to-base64).

## Related Documentation

- [JaszczurHAL Library Compilation](../lib_compilation.md) describes static
  library and native RP build prerequisites, options, outputs, and manual CMake
  equivalents.
- [Firmware Project Workflow](../FwProjectWorkflow.md) describes
  dispatcher-backed firmware manifests, source discovery, target/board
  resolution, cache ownership, upload, and generated files.
- [JaszczurHAL VS Code Entry](../../vscode/README.md) is the user-facing
  `jh-vscode` CLI and VS Code task contract.
- [Target and board profiles](../boards_profiles_howto.md) documents descriptor
  fields and how registry defaults merge with project manifests.
- [VS Code Entry Changelog](../../vscode/CHANGELOG.md) records implemented
  workflow capabilities and compatibility decisions.
- [Windows Runtime](../../vscode/windows/runtime/README.md) records the current
  Windows-runtime boundary; Linux remains the implemented runtime.
- [Native RP Neutral Firmware](../../vscode/neutral_fw/rp_native/README.md)
  explains the default-identity image used by `jh-vscode clear-identity`.
- [JaszczurHAL Examples](../../examples/README.md) documents the example registry,
  target coverage, application entry contract, variants, and build commands.
- [Managed Third-Party Components](../../third_party/README.md) documents tracked
  pins, ignored installations, updater behavior, and external checkout policy.
- [Security Supply Chain](../security_supply_chain.md) documents SBOM generation,
  vulnerability scanners, CI policy, and update/triage rules.
