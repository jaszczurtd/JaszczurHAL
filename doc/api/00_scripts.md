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
| Prepare a native Windows workstation | `powershell -NoProfile -ExecutionPolicy Bypass -File .\runmefirst.ps1` | Prepares the pinned managed Python environment, native toolchains, source components, Cortex-Debug user paths, and the Windows host self-check. |
| Synchronize managed dependencies | `./third_party/update_components.sh` | Fetches missing components and replaces managed installations that differ from tracked pins. |
| Verify dependencies without changing them | `./third_party/update_components.sh --verify-only` | Checks all managed component versions, commits, required files, PMD archive state, built picotool, and the RISC-V toolchain stamp. |
| Refresh all tracked generated files | `python3 scripts/sync_generated.py --write` | Runs the feature, board, example, root VS Code, and SBOM generators and lists every file changed during synchronization. |
| Verify all tracked generated files | `python3 scripts/sync_generated.py --check` | Runs every generator in read-only verification mode and fails on missing or stale output. |
| Run the complete repository gate | `./runalltests.sh` | Cleans managed gate outputs and runs tests, Valgrind, static analysis, CPD, target builds, and example builds. |
| Operate a firmware project | `vscode/entry/jh-vscode <action> --project <dir>` on Unix or `vscode/entry/jh-vscode.cmd ...` on Windows | Provides the stable build, upload, monitor, board-selection, IntelliSense, and clean CLI used by VS Code projects. |
| Build or flash an ESP-IDF project | `python3 scripts/build_esp_idf.py <action> --project <dir>` | Runs the `build`, `artifacts`, or `flash` action; resolves the ESP target/board metadata; prepares the pinned SDK on demand; and validates the relocatable multi-image manifest. |
| Build checked-in examples | `scripts/examples_dispatcher.py build --target <target>` | Builds example manifests through the same `jh-vscode` and CMake dispatcher used by firmware projects. |
| Build native RP parity fixtures | `scripts/build_rp_native_parity_fixtures.sh` | Builds USB multicore and SDLogger probes for all supported native target/runtime combinations. |

### Artifact policy

Repository-owned generated build artifacts belong below `.build/`; managed
component installations belong below `third_party/`. The directory model,
target/board cache isolation, and generated-file ownership are defined in
[Build Directories And Generated Files](../FwProjectWorkflow.md#build-directories-and-generated-files).

## Tooling interfaces

`config/tooling/` contains versioned, repository-owned data shared by scripts,
generated files, CMake, and host bootstrap code. Each JSON document has
`schemaVersion: 1` and one domain owner:

| Data file | Ownership |
|---|---|
| `artifacts.json` | Names archive metadata files and tracked generated outputs. |
| `board_components.json` | Defines valid board components, providers, and exclusive slots. |
| `examples.json` | Defines the checked-in active example registry. |
| `managed_components.json` | Defines managed source/tool components, validation metadata, default order, and compatibility launchers. |

Python consumers load these documents through `scripts/tooling_contract.py`.
Named artifact paths are projected by `scripts/repository_layout.py`. CMake
does not parse JSON during ordinary configuration: the board generator writes
`cmake/generated/jh_board_components_registry.cmake` from
`board_components.json`.

After changing board-component data or another generator input, refresh and
verify all tracked projections through the shared runner:

```bash
python3 scripts/sync_generated.py --write
python3 scripts/sync_generated.py --check
```

Keep protocol and format literals close to their operations. In particular,
explicit `encoding="utf-8"` arguments document the on-disk text format and
are intentionally not replaced by a global string constant. User-facing
messages and one-off syntax tokens likewise stay with the code that owns them.

## Repository-Level Orchestrators

These scripts are intentionally outside `scripts/` because they are top-level
workflow entrypoints.

### `runmefirst.sh`

One-time, idempotent setup for Debian/Ubuntu-like systems. It:

- removes the repository `.build/` tree before setup;
- installs compiler, CMake, Ninja, Python, Java, Valgrind, clang-tidy, cppcheck,
  OpenOCD, `gdb-multiarch`, serial, libusb, and other host packages;
- invokes `third_party/update_components.sh`;
- installs `osv-scanner` and `cve-bin-tool`;
- installs a udev rule for RP2040/RP2350 BOOTSEL/picotool USB access and the
  app-mode `/dev/ttyACM*` port used by the automatic 1200-bps reset;
- checks for a persistent LAN-scoped OTA TCP/8266 callback rule and asks before
  changing the firewall or installing `iptables-persistent`;
- configures the repository Git hooks;
- verifies that every required tool is available.

The script uses `sudo` for system packages, `/usr/local/bin`, the udev rule,
and an explicitly approved firewall change. It downloads tools and
dependencies, so it requires network access. The focused firewall helper is
`scripts/configure_ota_firewall.py`; it supports `--check`, explicit
`--interface` / `--network`, and confirmed or `--yes` provisioning.

### `runmefirst.ps1`

Idempotent native Windows setup. It prints its complete plan before making a
change, uses short user-local tool/build roots, creates the pinned Python 3.12
environment with hash-verified pyserial, synchronizes source components, and
resolves CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD, and picotool. Compatible
system tools are reused unless `-Force` is selected. A system OpenOCD is reused
only when its required interface and target scripts can also be resolved;
otherwise setup falls back to the authenticated managed archive.
It records the verified executable set, managed Python, and short build root in
`.build/windows/host-environment.json` for the shared firmware runtime. Editor
mode also preserves and updates the standard VS Code user `settings.json` with
the Windows-specific Cortex-Debug OpenOCD and GNU Arm paths; it creates a
recoverable `.jaszczurhal.bak` file before changing existing settings.

`-VerifyOnly` is read-only. `-ConfigureHost` explicitly allows the documented
long-path settings to be repaired, and `-InstallExtensions` explicitly allows
VS Code profile changes. `-FirmwareOnly` keeps editor checks visible but
optional for headless firmware builders and CI, and skips Cortex-Debug profile
configuration. `-VerifyOnly` checks the configured debugger paths without
writing. The script never elevates itself. See
[Native Windows Setup](../windows_setup.md) for host requirements, commands,
paths, and the current support boundary.

### `scripts/windows_host_inventory.ps1`

Read-only Windows PowerShell 5.1 probe used by `runmefirst.ps1` for its final
host-requirements check. It reports the Windows build and architecture, long-path
settings, Git, Python, CMake, Ninja, GNU Arm, GNU RISC-V, OpenOCD, picotool,
VS Code extensions, and optional repository line-ending checks. Required
failures produce a nonzero exit code. `-Json` emits structured records,
`-RepoPath` enables checkout checks, and `-FirmwareOnly` keeps editor items
visible but optional. The script never changes the host and is also useful as
a standalone setup diagnostic.

### `third_party/update_components.sh`

The normal dependency-management entrypoint. It is a compatibility launcher
for `scripts/component_manager.py all`, which processes fifteen baseline
components in the dependency order declared by
`config/tooling/managed_components.json`:

1. BearSSL
2. cJSON
3. LodePNG
4. TJpg_Decoder
5. FatFs
6. Unity
7. lwIP
8. littlefs
9. BTstack
10. Semtech SX126x driver
11. FreeRTOS-Kernel
12. Pico SDK
13. PMD CPD
14. picotool
15. RISC-V toolchain

ESP-IDF is the sixteenth managed component but remains opt-in because its
checkout, recursive submodules, and target tools are large. The production
ESP-IDF runner prepares it on first use; focused setup is available through
`scripts/ensure_esp_idf.sh --enable` or `JH_ENABLE_ESP_IDF=1`.

Normal mode makes each managed installation match its tracked configuration.
`--verify-only` performs no fetch, extraction, checkout replacement, or build.
picotool verification includes its required commands and the USB/signing
capabilities enabled by the currently available dependencies.
See [Managed Third-Party Components](../../third_party/README.md) for the pin and
directory layout.

### `runalltests.sh`

The complete local quality gate. Before running its eight gates, it invokes
`scripts/sync_generated.py --write` for tracked feature, board, example, root
VS Code, and SBOM projections. A local run therefore repairs deterministic
generated drift and prints the changed-artifact list again in its final
summary. `--check-generated` selects read-only verification instead. CI uses
the same shared runner in check mode, so the list of generators is maintained
in one place. `-j N`, `--jobs N`, and `-jN` select build parallelism. The gates
are:

1. required tools and managed-component verification;
2. host tests, including the optional FreeRTOS POSIX suite;
3. Valgrind memcheck;
4. cppcheck;
5. clang-tidy for host/shared code and the STM32 backend, using both the
   `JH_STM32_HOST_SANITY` host-compiler database and the real ARM database;
6. PMD CPD duplicate detection across owned C/C++ implementations and Python
   scripts;
7. STM32, RP2040/RP2350, native FreeRTOS, RP feature-profile, and clean
   ESP32-S3/ESP-IDF builds with artifact validation;
8. every declared RP example, native parity fixture builds, and STM32
   examples.

The script removes only its managed `.build/gate`, `.build/examples`, and
`.build/tests` trees at startup. It exits on the first failed gate.
Gate 3 runs every directly registered native C/C++ test executable labelled
`memcheck`. `MEMCHECK_REQUIRED_TESTS` remains a required critical subset and
prevents those suites from silently leaving the selection. Python, CMake, and
shell driver tests are excluded: wrapping their parent interpreter would
measure that host tool rather than cross-compiled firmware or child processes.
The Valgrind configuration uses fair thread scheduling so the native FreeRTOS
POSIX scheduler tests are included without stalling. CTest progress is streamed
unfiltered to both the terminal and `.build/gate/logs/jh_memcheck.log`.

### `vscode/entry/jh-vscode` and `jh-vscode.cmd`

The Unix and Windows launchers start one public Python entrypoint and shared
firmware-project CLI. The Windows launcher validates Python 3 plus pyserial and
preserves the CLI arguments and exit-code behavior. Firmware configuration uses
Ninja by default, passes the active Python interpreter, exports compile
commands, and resolves platform picotool/toolchain paths. Native Windows CMake
trees use the bootstrap's short build root while final artifacts keep their
manifest paths. `debug-tools` reports the verified OpenOCD, Arm-capable GDB,
scripts root, and target configuration used by Cortex-Debug. Generated Linux
settings select `gdb-multiarch`; Windows uses the bootstrap-managed GNU Arm
GDB. Actions, options, device
safeguards, and monitor behavior are documented only in
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

By default, each build verifies the static library, ELF/BIN/UF2 artifact
probes, core-entry symbols, and optional example firmware. `--library-only`
builds only the `JaszczurHAL` CMake target and verifies the linkable
`libJaszczurHAL.a` archive. Default output is `.build/static/<target>/<board>/`.

Important options are `--target`, `--platform`, `--board`, `--sdk-dir`, `--toolchain`,
`--picotool-dir`, `--picotool-build-dir`, `--example`, `--freertos`,
`--library-only`, `--project-config`, repeatable `-D`, `--output`, `--clean`,
and `--jobs`.
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

### `scripts/build_esp_idf.py`

Production project runner for targets whose board descriptor selects the
`esp-idf` provider. It exposes three actions:

| Action | Behavior |
|---|---|
| `build` | Optionally removes the selected output with `--clean`, generates project/board/SDK inputs, builds with the pinned ESP-IDF, captures toolchain provenance, and validates artifacts. |
| `artifacts` | Revalidates an existing build and rewrites the deterministic `jh_esp_idf_artifacts.json` manifest without invoking the compiler. |
| `flash` | Revalidates the existing build, requires `--port`, and invokes ESP-IDF flash with the complete image/offset set before validating the flash log and manifest again. |

`--project` is required. `--target` defaults to `esp32s3`; its target descriptor
selects `waveshare-esp32-s3-zero` when `--board` is omitted. `--output` must
remain below either the project or repository `.build` root. Repeatable
`--source` arguments replace automatic discovery; otherwise the runner includes
supported files in the project root and recursively under `src/`. Repeatable
`--feature` and `--define` arguments extend the project configuration.
`--idf-dir` or `JH_ESP_IDF_DIR` selects an exact compatible external checkout.

The runner consumes `boards/` and the feature registry directly. Target-
required features participate in the resolved set, while requested or
transitive features outside `supportedFeatures` fail with
`[JH-CFG-UNSUPPORTED]`. The ESP32-S3 allowlist contains required FreeRTOS,
the delivered Phase 2 peripheral flags, and the Phase 3 network/service graph.
Its system, sync, GPIO, ADC, simple PWM, serial/debug, and timer sources form
the baseline. The runner also owns `HAL_PROVIDE_APP_ENTRY`, the exact
target/board selectors, generated `sdkconfig` defaults, and the controlled
component graph. It writes the resolved source/dependency lists into the
generated project CMake input consumed by the ESP-IDF component.

The output manifest uses only build-relative paths. It records ordered flash
images and hashes; build artifacts; target, board, feature, partition, and
`sdkconfig` facts; the ESP-IDF version/commit; actual compiler, CMake, Ninja,
IDF Python and esptool versions; and the pinned `tools.json` digest.
`scripts/build_esp_idf_phase0.py` is a compatibility wrapper that supplies the
old fixture arguments to this production runner.

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

`scripts/component_manager.py` owns the cross-platform implementation for Git
clone/fetch/ref/origin/submodule checks, archive download and SHA-256,
ZIP/`tar.gz` extraction, atomic replacement, content manifests, and version
stamps. The focused `ensure_*.sh` files are Unix compatibility launchers that
forward their existing CLI to this Python manager. Component validation
metadata, default ordering, and launcher mappings live in the versioned
`config/tooling/managed_components.json` model.

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

Synchronizes `third_party/FatFs` from the exact commit of the project-owned
`jaszczurtd/ff16` repository recorded in `third_party/fatfs_version.conf`.
That repository mirrors ChaN's unchanged R0.16 archive. The helper verifies the
repository origin, exact commit, required source and license files, and supports
`--verify-only`, `--repo-root`, and `--dir`.

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

### Semtech SX126x managed component

`python3 scripts/component_manager.py component sx126x` synchronizes
`third_party/sx126x_driver` from `sx126x_driver_version.conf`. It verifies the
clean exact commit, Clear BSD license, base driver and version source set, and
the HAL/status/register headers. The normal component updater includes it on
Linux and Windows; no shell-only focused wrapper is required.

### `scripts/ensure_freertos_kernel.sh`

Synchronizes or verifies FreeRTOS-Kernel and its required RP/STM32 ports.
Without an enable condition it is a no-op. It runs when:

- `--enable`, `--freertos`, `--force`, or `--verify-only` is passed;
- `EXTRA_HAL_DEFINES` contains `HAL_ENABLE_FREERTOS`; or
- `HAL_ENABLE_FREERTOS` is present in the environment.

`--kernel-dir` and `JH_FREERTOS_KERNEL_DIR` select an external checkout that is
verified but not replaced. Managed submodules and the kernel version are also
checked. The native RP and STM32G474 direct CMake integrations call
`scripts/component_manager.py` directly; this shell wrapper is the compatibility
entrypoint used by the static-library build helpers.

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

### `scripts/ensure_pmd.sh`

Installs or verifies the PMD 7.26.0 binary distribution pinned in
`third_party/pmd_version.conf`. The manager authenticates the ZIP SHA-256,
tracks the complete extracted-file manifest, resolves the platform launcher,
and checks the reported PMD version. A Java runtime is required; Linux
`runmefirst.sh` installs the headless default runtime.

### `scripts/ensure_riscv_toolchain.sh`

Installs the pinned Raspberry Pi prebuilt
`riscv32-unknown-elf` toolchain for the native `rp2350-riscv` target. It maps
the host architecture to the matching release asset, extracts the archive into
`third_party/riscv-toolchain`, records a component stamp, and verifies the GCC
major version.

If the executable, content manifest, archive identity, or stamp differs from
the tracked configuration, normal mode replaces the installation.
`--verify-only` performs no download or extraction. Authenticated assets cover
x86-64 and AArch64 Linux plus native AMD64 Windows.

## Example And VS Code Support Scripts

### `scripts/examples_dispatcher.py`

Consumes the checked-in example registry from `config/tooling/examples.json`
and exposes five subcommands:

| Command | Behavior |
|---|---|
| `generate` | Regenerates each example's manifest, VS Code settings, tasks, launch configuration, and keybinding reference. |
| `generate-template` | Regenerates the shared settings, tasks, extension, and keybinding snippets under `vscode/examples`. |
| `check-template` | Fails when the shared snippets or any checked-in example `.vscode` file differs from the shared generators. |
| `list` | Prints every registered project with expanded `targets` and `gateTargets`. |
| `build` | Builds supported examples and variants through `vscode/entry/jh-vscode`. |

`build` requires `--target` with one of `rp2040`, `rp2350-arm`,
`rp2350-riscv`, or `stm32g474`. Repeatable `--example` limits the run,
`--gate` restricts it to base/variant configurations whose generated
`gateTargets` contain the requested target, `--jobs` controls parallel example
projects, and `--verbose` records invoked commands in managed per-example logs
below `.build/examples`.

The JSON registry is the source used by `generate`; the generated manifests are
the source consumed by `build`. The `list` action reports the current full and
default-gate matrices without maintaining duplicate counts here.
RISC-V WiFi examples remain excluded while RP2350 RISC-V + CYW43 is
unsupported.

See [JaszczurHAL Examples](../../examples/README.md) for the target matrix,
application interface, and build commands.

### `scripts/sync_generated.py`

Single repository-level runner for every tracked generated artifact. `--write`
refreshes the feature registry, static board registry, example VS Code files,
root VS Code files, and repository SBOM. `--check` invokes their read-only
verification modes
and fails on missing or stale output. The runner snapshots tracked and
non-ignored files before execution, then prints the paths changed during the
run. `--report-file <path>` also stores that final list for callers such as
`runalltests.sh`.

### `scripts/generate_board_config.py`

Validates the JSON target, board, and capability descriptors under `boards/`
and resolves one target/board pair into generated CMake configuration and
machine-readable metadata. CMake and the board tests call it directly.
`--validate-only` checks the complete registry, `--list targets|boards` and
`--default-board` provide discovery, while `--feature` and `--define` add the
validated build overlay used for generated output. `--output-dir` and
`--output-root` must stay within the caller-owned build tree. Provider/backend
definitions are projected consistently into
`jh_board_resolved.json.boardCompileDefinitions`, generated
`JH_BOARD_COMPILE_DEFINITIONS`, and `jh_board_config.h` macros for direct
compiler use. The generated GCC/Clang link-signature reference uses a
`constructor, used` root so target/board/feature mismatches remain link errors
with section garbage collection enabled.

The generated header also exposes the selected target descriptor ID, backend,
MCU and subtype names, CPU description and core count, FPU presence, and
total/usable RAM as `HAL_TARGET_*` facts. System architecture snapshots consume
those facts directly. Board-specific program-flash capacity remains available
as `HAL_BOARD_EXPECTED_FLASH_BYTES`.

`--write-static` refreshes the tracked `jh_board_registry.h`,
`jh_board_fallback_config.h`, and board-component CMake registry. The first two
come from `boards/`; the CMake projection comes from
`config/tooling/board_components.json`. `--check-static` rejects missing or
stale copies. CI runs the check independently of per-build board generation.

### `scripts/generate_hal_features.py`

Validates the closed `HAL_ENABLE_*` / `HAL_DISABLE_*` namespace and the
target-independent dependency graph under `config/features/`. `--write`
atomically refreshes the tracked production C header and CMake resolver, while
`--check` compares them without writing. `--lint` accepts repeatable
`--input-root` arguments and checks raw `hal_project_config.h` files and project
manifests for unknown symbols, unsupported `=0` values, and direct requests for
derived symbols. It also rejects conditional feature definitions outside a
matching `#ifndef` guard and non-scalar CMake definition lists. Findings fail
the command by default; `--report-only` is an explicit manual diagnostic mode.

`--effective` reuses the `jh-vscode` resolver to enumerate declared targets,
target profiles, and variants without reading gitignored local board state. It
checks constraints and active duplicate requests after layer precedence has
been applied. A standard `.vscode/jaszczurhal.project.json` creates the declared
axes; an unpaired `hal_project_config.h` with at least one HAL feature request
creates one axis-free direct context. Standalone headers without requests and
reference manifests remain raw-lint-only inputs. `--resolution-output <path>`
writes deterministic
`requestedFeatures`, `resolvedFeatures`, closure digests, and direct-request
provenance for each effective configuration. The registry test freezes the
matrix digest in `config/effective-features-baseline.json`. Each record maps to
a checked unique target/board/request tuple for the C preprocessors and a
checked unique request set for the target-independent CMake resolver.

The generated C header is included by `hal_config.h` and expands every
target-independent transitive implication. Its generated `HAL_CONFIG_VERBOSE`
section reports every active registered feature after the remaining
configuration rules run. The generated CMake resolver supplies the same
closure to RP and STM32G474 source and dependency selection. The ESP-IDF runner
also resolves that closure, then enforces the target's `supportedFeatures`
allowlist before its controlled minimal component graph is configured. Board
generation uses the resolved set for `featureHash` and the link signature, while
retaining the direct set as `requestedFeatures`. `jh-vscode` resolves the registry after
manifest profile and variant overlays, exposes the result through
`featureResolution`, and uses the resolved set for preflight and OTA decisions
while passing direct requests to CMake.

Conditional defaults, provider choices, board capability checks, and target
constraints remain in `hal_config.h`. CI runs `--check` and strict raw/effective
lint, and uploads the deterministic resolution report. Installed RP and
STM32G474 packages carry the generated feature/board headers, resolved board
JSON, link-signature header, and reference source; a direct compiler consumer
can compile and link those package artifacts without invoking Python.

### `scripts/board_registry.py`

Import-only projection of the validated `boards/` descriptors into the target
and board model consumed by `jh-vscode`, project generators, and the example
dispatcher. It deliberately contains no independent registry or command-line
interface; descriptor files remain the source of truth.

### `scripts/tooling_contract.py` and `scripts/repository_layout.py`

Import-only loaders for the versioned data models under `config/tooling/`.
`tooling_contract.py` validates the common schema and typed fields;
`repository_layout.py` exposes named archive metadata and tracked generated
artifact paths. Domain catalogs remain separate JSON documents rather than one
global string module. The inventory, projection commands, and format-ownership
rules are defined in [Tooling interfaces](#tooling-interfaces).

### `scripts/vscode_task_config.py`

Import-only source of truth for generated VS Code extensions, keybinding
references, task definitions, board-picker input, and managed Cortex-Debug
profiles. It also provides the migration and synchronization helpers used by
`sync-board-picker`. Generated projects, the standalone project generator, and
the drift tests import these functions instead of maintaining separate JSON
templates. The user-facing behavior of every generated task is documented in
[Generated VS Code Tasks](../../vscode/README.md#generated-vs-code-tasks).

### `vscode/tools/create-vscode-example.py`

Generates a standalone dispatcher-backed firmware project with a manifest,
blink application, HAL project configuration, launch configuration, shared
Unix/Windows task commands, extension recommendations, and keybinding
reference. The generated VS Code settings include `cmake.configureSettings` for
the initial target and board, allowing CMake Tools to configure the shared
dispatcher directly. `--target` and `--board` select the initial profile.
`--force` replaces only files owned by the generator in the requested project
directory; `--dry-run` lists the paths without writing them.

### `vscode/tools/manage_vscode_extensions.py`

Reads the shared extension recommendation list and checks the selected VS Code
profile with `code --list-extensions`. The default mode is read-only and exits
nonzero when recommendations are missing. `--install` prints the missing list
and requests confirmation before invoking `code --install-extension`.
`--install --yes` records explicit non-interactive consent. The command verifies
the complete list after installation. `--code` and `JH_VSCODE_CODE` select a
specific VS Code command.

### `vscode/tools/configure_cortex_debug.py`

Reads the verified Windows `host-environment.json`, checks that OpenOCD, GNU
Arm GCC, and adjacent GDB executables exist, and merges their absolute paths
into the standard VS Code user profile as the Windows-specific Cortex-Debug
settings. The updater preserves unrelated JSONC settings, comments, nesting,
and trailing commas. It creates `settings.json.jaszczurhal.bak` before changing
an existing profile and replaces the settings file atomically. `--check` is
read-only, `--yes` confirms a non-interactive update, and `--settings` supports
an explicitly selected VS Code profile or a test fixture. `runmefirst.ps1`
invokes this helper automatically outside `-FirmwareOnly` mode.

### `scripts/configure_ota_firewall.py`

Idempotently inspects and configures persistent inbound TCP access for the
host-side OTA callback. The shared entrypoint selects a Linux or Windows
backend, finds the RFC1918 network on the default IPv4 interface, scopes the
rule to that interface and subnet, and defaults to TCP/8266. Active UFW and
firewalld installations use their native persistent configuration; the Linux
fallback uses `iptables-nft`/`iptables` with
`iptables-save` plus the `netfilter-persistent` boot loader, enabled through
systemd when available.
An unfiltered `INPUT` policy already permits the callback and requires no
additional package or rule.

On Windows, the backend accepts only an active network whose connection
profile is `Private`. It manages one named Windows Defender Firewall inbound
rule restricted to that profile, interface alias, RFC1918 source subnet, TCP,
and the selected local port. Inspection and planning run without elevation;
applying the rule requires the caller to restart the command in an already
elevated PowerShell. The helper never starts an elevated process or changes a
network profile.

Interactive mode prints the full rule scope and asks before making a change.
`--check` is read-only, `--dry-run` prints the complete plan, `--interface`
and `--network` override automatic route detection, `--port` selects a
different fixed callback port, and `--yes` supports deliberate
non-interactive provisioning. Only RFC1918 IPv4 networks are accepted, and
setup refuses to expose a port already used by a listener.

### `scripts/ota_firewall_common.py`

Import-only interface shared by the Linux and Windows OTA firewall backends. It
owns the validated interface/subnet value, RFC1918 checks, and the common
`SetupError` failure type. Direct callers should use
`scripts/configure_ota_firewall.py` so platform selection, consent, and exit
codes remain consistent.

### `scripts/ota_firewall_windows.py`

Internal Windows Defender Firewall backend selected by
`scripts/configure_ota_firewall.py`. It discovers active Private IPv4
networks through NetTCPIP, parses existing NetSecurity rules, validates the
interface/subnet/port scope, and creates or verifies the persistent inbound
rule only after the entrypoint has obtained consent. Its command runner is a
test seam, and the module remains internal to the public entrypoint.

### `scripts/rp_ota_artifacts.py`

Internal native RP firmware packaging helper used by CMake. `package` wraps an
application BIN in the versioned JaszczurHAL OTA header with target, load
offset, generation, version and payload SHA-256. The HMAC field remains
unsigned until the VS Code upload action applies the project password.
`merge-uf2` combines the copy-to-RAM boot applier UF2 with the application UF2,
rejecting conflicting address blocks and normalizing block numbering. Build
artifacts remain below the resolved `.build/` directory. See
[Native OTA Workflow](../OTAWorkflow.md) for the complete consumer workflow
around these artifacts.

### `scripts/vscode_library_workspace.py`

Owns the repository-root VS Code static-library workflow. The `select` action
validates target/board pairs against `boards/` and stores the active profile in
gitignored local state. `build`, `refresh-intellisense`, `install`, `clean`, and
`config-dump` then resolve the same build and install paths from that profile.

RP builds delegate to `build_rp_native_lib.sh --library-only`, STM32G474 builds
delegate to `build_stm32_lib.sh`, and mock builds select the root `hal_mock`
CMake target. Every build exports `compile_commands.json`; the IntelliSense
actions write a local `.vscode/c_cpp_properties.json` without changing tracked
settings. Clean removes only the active profile's managed build/install trees.

`sync-vscode` deterministically writes the tracked root tasks, settings,
extension recommendations, and keybinding reference from the board registry.
Use `sync-vscode --check` to reject drift without changing files.

This is not the firmware-project workflow. Dispatcher-backed projects use
`jh-vscode <action> --project <dir>`.

### `scripts/vscode_refresh_intellisense.sh`

Compatibility wrapper for the former repository IntelliSense entrypoint. It
maps `mock`, `rp2040`, `rp2350-arm`, `rp2350-riscv`, `stm32`, or `stm32g474` to
the default board, selects that library profile, and delegates to
`vscode_library_workspace.py refresh-intellisense`.

### `scripts/vscode_clear_build_artifacts.sh`

Manual full-clean helper. It removes the entire repository `.build/` tree and
nothing outside it. There are no options. This also removes cached target
builds, examples, tests, IntelliSense data, and the built picotool executable;
ignored component sources under `third_party/` are retained. The root VS Code
`Project: Clean` task deliberately uses the scoped library-workspace action
instead.

## Static Analysis And Security Scripts

### `scripts/run_cpd.py`

Runs the managed PMD Copy/Paste Detector over owned C/C++ implementation
sources and Python files below `scripts/`. Every production, test, or example
C/C++ duplicate group from 100 tokens and every Python-script group from 50
tokens blocks the gate; there is no baseline or accepted-debt list. Generated
and vendored implementations are excluded. The report also gives
duplicate-token coverage globally and for the mock, RP2040, STM32G474, shared,
remaining portable, and Python-script scopes. Overlapping token ranges count
only once. Deterministic source lists and XML reports are written to the
requested output directory below `.build/`.

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

`--inventory` and `--output` override the default input and output paths.
`--check` generates a temporary candidate, compares it with the selected
output, and fails without modifying the tracked file when it is missing or
stale. The generator uses only the Python standard library.

### `scripts/check_sbom.sh`

Compatibility wrapper that delegates to `scripts/generate_sbom.py --check`.
The shared `scripts/sync_generated.py --check` runner is the repository and CI
freshness gate.

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
  `jh-vscode` CLI and VS Code task interface.
- [Target and board profiles](../boards_profiles_howto.md) documents descriptor
  fields and how registry defaults merge with project manifests.
- [VS Code Entry Changelog](../../vscode/CHANGELOG.md) records implemented
  workflow capabilities and compatibility decisions.
- [Windows Runtime](../../vscode/windows/runtime/README.md) records the native
  Windows runtime boundary and remaining device-adapter work.
- [Native RP Neutral Firmware](../../vscode/neutral_fw/rp_native/README.md)
  explains the default-identity image used by `jh-vscode clear-identity`.
- [JaszczurHAL Examples](../../examples/README.md) documents the example registry,
  target coverage, application entry interface, variants, and build commands.
- [Managed Third-Party Components](../../third_party/README.md) documents tracked
  pins, ignored installations, updater behavior, and external checkout policy.
- [Security Supply Chain](../security_supply_chain.md) documents SBOM generation,
  vulnerability scanners, CI policy, and update/triage rules.
