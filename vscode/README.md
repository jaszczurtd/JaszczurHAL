# JaszczurHAL VS Code Entry

This directory contains the shared VS Code firmware workflow for projects that
use JaszczurHAL: build, debug build, upload, UF2 upload, serial monitor,
IntelliSense refresh, board/port helpers, and USB identity cleanup.

The stable public surface is `entry/`. Project `.vscode/tasks.json` files should
call `entry/jh-vscode` on Unix and `entry/jh-vscode.cmd` on Windows, and keep
project-specific behavior in configuration.
Portable CLI, configuration, CMake, artifact, OTA, and persistent-monitor logic
lives under `runtime/`. Host operations use a lazy platform adapter; the Linux
implementation and compatibility entrypoints live under `linux/runtime/`.
The native Windows adapter provides COM identity, process ownership, BOOTSEL
volume discovery, durable UF2 upload, and build locks. These runtime directories
are implementation details.
They ship as regular Python packages with an `__init__.py` in every level, so
`vscode.runtime` always resolves inside this repository.
For the full firmware project model, see
[`doc/FwProjectWorkflow.md`](../doc/FwProjectWorkflow.md). For the complete
native RP OTA contract, including firewall and recovery, see
[`doc/OTAWorkflow.md`](../doc/OTAWorkflow.md).

## Host Launchers

Both host launchers execute `entry/jh_vscode.py`, which imports the shared
runtime. The Unix launcher uses `python3`. The Windows launcher selects the
first Python 3 interpreter that imports `pyserial` in this order:

1. `JH_VSCODE_PYTHON`, when explicitly configured;
2. `.build/windows/venv/Scripts/python.exe` under the JaszczurHAL root;
3. `py -3`;
4. `python`.

An explicit `JH_VSCODE_PYTHON` must name the interpreter executable, without
extra arguments. A missing suitable interpreter reports exit code 8 with a
host-setup diagnostic. `runmefirst.ps1` owns the managed environment, and the
launcher also accepts an explicitly resolved interpreter through
`JH_VSCODE_PYTHON`.

Generated `tasks.json` files keep the Unix command in `command` and add a
Windows override that reads `jaszczurhal.vscodeEntryWindows`. Generated
`settings.json` files point that setting at the adjacent `jh-vscode.cmd`.
This keeps task labels and arguments identical on both hosts.

Windows device actions use native COM and volume APIs; they do not require WSL,
Git Bash, or a POSIX compatibility layer.

## CLI Contract

```text
jh-vscode <action> [options]
```

Implemented actions:

```text
build
build-debug
upload
upload-uf2
upload-ota
ota-discover
monitor
monitor-probe
monitor-any
refresh-intellisense
clean
select-board
sync-board-picker
list-ports
change-port
clear-identity
config-dump
debug-tools
```

Compatibility note: `debug` is accepted as an alias for `build-debug` only for
early migration work. New tasks should use `build-debug`.

`change-port` selects a serial port interactively or through `--port` and saves
it as user-local `uploadPort` in `.vscode/jaszczurhal.local.json`.

`debug-tools --project <path> --json` resolves a verified Arm-capable GDB,
OpenOCD executable, scripts root, and board-family interface/target scripts.
Generated Linux settings select `gdb-multiarch`, installed by
`runmefirst.sh`. Script validation is target-specific, so a distro OpenOCD
that contains the selected STM32 profile remains usable even when it predates
another target family.
On native Windows these paths come from the bootstrap host-environment record;
`runmefirst.ps1` writes `openocd` and `armToolchainPath` to the Windows-specific
Cortex-Debug user settings. The command output remains available for diagnosis.

`list-ports --json` reports the compatibility `bootsel` path list and structured
`bootselRecords`. Each structured record includes the mount, device path,
Windows volume GUID, label, and filesystem when the platform exposes them.

`sync-board-picker` refreshes the `boardSelection` input and its automatic
folder-open task from the current `boards/` registry. It also creates or repairs
the managed Cortex-Debug profiles in `launch.json` for RP2040, RP2350 ARM, and
STM32G474/ST-Link. The ELF path comes from the tracked project manifest, legacy
JaszczurHAL profiles are migrated, and configurations with consumer-owned names
are preserved. Generated projects run the synchronization when a trusted
workspace opens. VS Code may require one-time approval through `Tasks: Manage
Automatic Tasks`; `Project: Select board` remains the dynamic terminal fallback.

## Generated VS Code Tasks

The generator writes the same task labels and arguments on both hosts. Unix
uses `jaszczurhal.vscodeEntry`; the Windows override uses
`jaszczurhal.vscodeEntryWindows`. The maintained tasks are:

| Task | CLI action | Behavior |
|---|---|---|
| `Project: Build` | `build` | Builds the active target and board in Release mode and publishes its stable artifacts. This is the default VS Code build task. |
| `Project: Build (Debug)` | `build-debug` | Uses a separate Debug CMake cache, publishes the Debug ELF, and serves as the pre-launch task for every managed Cortex-Debug profile. |
| `Project: Upload` | `upload` | Builds and uploads through the active target backend: verified CDC-to-BOOTSEL UF2 for RP or OpenOCD for STM32G474. |
| `Project: Upload (UF2 / BOOTSEL)` | `upload-uf2` | Builds an RP image, validates the UF2, and copies it to one verified BOOTSEL volume. It refuses ambiguous volumes. |
| `Project: Upload (OTA)` | `upload-ota --interactive` | Builds and authenticates the OTA image, discovers matching native RP devices, and prompts when an explicit device choice is required. |
| `Project: Discover OTA devices` | `ota-discover` | Lists compatible OTA responders and their address, target, generation, slot, and boot state. |
| `Project: List ports` | `list-ports` | Shows serial records, project identity matches, and BOOTSEL candidates without opening a device. |
| `Project: Change port` | `change-port` | Selects a serial port interactively and stores it in the gitignored local project configuration. |
| `Project: Serial Monitor` | `monitor --lock-policy replace-own` | Starts the persistent project monitor and may replace only another verified JaszczurHAL monitor owning the same port. |
| `Project: Debug Probe Monitor` | `monitor-probe --lock-policy replace-own` | Starts the persistent serial monitor for the configured debug-probe identity. |
| `Project: Serial Monitor (Any)` | `monitor-any --lock-policy wait` | Waits for any eligible serial port and never displaces another owner. |
| `Project: Refresh IntelliSense` | `refresh-intellisense` | Builds the compile-database target and writes the patched database at the stable path consumed by cpptools. |
| `Project: Clean` | `clean` | Removes the project artifacts and its matching managed CMake trees after path-safety validation. |
| `Project: Clear USB Identity` | `clear-identity` | Builds neutral RP firmware and flashes it only after the current USB identity or BOOTSEL selection passes the normal safety checks. |
| `Project: Config Dump` | `config-dump` | Prints the fully resolved manifest, local overrides, target, board, paths, upload configuration, and HAL feature resolution. |
| `Project: Select board` | `select-board --interactive` | Selects the target and board in the terminal and persists the selection locally. |
| `Project: Select board (GUI)` | `select-board --selection ...` | Uses the generated VS Code picker and persists the selected target/board pair locally. |
| `Project: Sync board picker` | `sync-board-picker` | Runs once on trusted folder open, refreshes picker values, and creates or repairs the managed RP2040, RP2350 Arm, and STM32G474 debug profiles while preserving consumer-owned profiles. |
| `Project: Build variant: <id>` | `build --variant <id>` | Appears only for declared example variants and builds that manifest variant through the normal artifact pipeline. |

The Run and Debug panel exposes three Cortex-Debug launch configurations:

- `Project: Debug Firmware` for RP2040 through CMSIS-DAP/Picoprobe;
- `Project: Debug Firmware (RP2350 ARM)` through CMSIS-DAP/Picoprobe;
- `Project: Debug Firmware (STM32G474 / ST-Link)` through ST-Link.

Each invokes `Project: Build (Debug)` first and then loads the resulting ELF
with the profile-specific probe, OpenOCD configuration, and reset policy.

Common options:

```text
--project <path>       Firmware module directory.
--target <id>          Override active target family for this invocation.
--board <id>           Override active board within the target.
--variant <id>         Select an example variant declared by the manifest.
--selection <t:b>      Persist target/board selection; GUI labels are accepted.
--interactive          Prompt for target/board selection in the terminal.
--port <port>          Override configured upload/monitor port.
--bootsel-volume <id>  Select one BOOTSEL drive root or Windows volume GUID.
--host <address>       Bypass OTA discovery and use this device address.
--baud <baud>          Serial monitor baud rate, default 115200.
--lock-policy <mode>   Serial monitor lock policy: wait, replace-own, replace-any.
--allow-unverified-port
                       Expert-only serial upload to an explicitly selected port
                       that does not match configured USB identity.
--verbose              Enable verbose output.
--json                 Emit machine-readable output where supported.
--help                 Show help.
--version              Show tool version.
```

For module actions, `--project` means the firmware module directory. It does not
mean the repository root. Examples:

```text
jh-vscode build --project /home/user/projects/router-reset/reseter
jh-vscode build --project /home/user/projects/Fiesta/src/Clocks
jh-vscode clear-identity --project /home/user/projects/Fiesta/src/ECU
```

`build`, `build-debug`, `debug-tools`, `upload`, `upload-uf2`, `upload-ota`, `ota-discover`,
`refresh-intellisense`, `clean`, `change-port`, and `clear-identity` require
`--project`. Actions that touch a device must fail before accessing serial
ports, BOOTSEL disks, or build artifacts when the target module is ambiguous.

Firmware projects use `toolchain: "cmake"` and the shared JaszczurHAL
dispatcher. `jh-vscode` resolves the active target/board, configures CMake, and
runs the maintained firmware targets:

```text
firmware
firmware_debug
firmware_upload
firmware_compile_db
```

By default, `jh-vscode` configures CMake with Ninja, exports compile commands,
and passes the currently running Python interpreter explicitly. Generated and
migrated projects set `cmake.sourceDir` in `.vscode/jaszczurhal.project.json`
to the shared multi-target dispatcher, for example
`${project}/../../libraries/JaszczurHAL/cmake/jh_firmware_project`, and pass the
module directory as `JH_PROJECT_DIR`. `cmake.generator` provides an explicit
generator override.

Native Windows reads the verified tool and short build-root state produced by
`runmefirst.ps1`. CMake caches stay below that short root, while final firmware
artifacts remain in the manifest `buildDir`. `refresh-intellisense` writes the
patched database to the stable artifact location used by VS Code. A build
attempt first removes stable uploadable firmware, then republishes artifacts
only after the selected target succeeds.

The `rp2040`, `rp2350-arm`, and `rp2350-riscv` targets build directly with the
official Pico SDK. RP firmware exposes HAL-owned USB CDC. With a selected
serial port, normal `upload` releases the project monitor,
performs a 1200-bps DTR touch, waits for the single BOOTSEL drive and copies the
UF2. STM32G474 uses the registry's native target recipe, supports bare-metal
and FreeRTOS firmware variants, and uploads through OpenOCD.

## Adding Project Source Files

Source discovery and the complete `JH_PROJECT_SOURCES` contract are defined in
[Adding Project Source Files](../doc/FwProjectWorkflow.md#adding-project-source-files).
That document is the only source for project layout and manifest examples.

## New Project Generator

Use `tools/create-vscode-example.py` to create a standalone, CMake-first VS Code
firmware project next to your other firmware repositories:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-vscode-example
```

The generated project contains a small blink application, project-local
`.vscode/` files, and a `hal_project_config.h` with `HAL_PROVIDE_APP_ENTRY`.
It does **not** carry a project-local firmware `CMakeLists.txt`; the manifest
points `cmake.sourceDir` at the shared JaszczurHAL dispatcher and stores the
initial `target`/`board` selection. Use `--target` and `--board` to choose a
non-default initial board:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output /home/user/projects/jaszczurhal-stm32-example \
  --target stm32g474 \
  --board nucleo-g474re
```

Feature flags in the project header, final target profile, and active example
variant accept bare `HAL_ENABLE_X` or `HAL_ENABLE_X=1`. `jh-vscode` rejects
`=0` and CMake generator expressions with `[JH-CFG-VALUE]` before CMake
configure. In definition-list inputs, every `HAL_ENABLE_*` entry must be a
standalone simple token separated with semicolons; whitespace does not separate
multiple feature definitions.

After applying the active target profile and variant, `jh-vscode` validates
unknown and derived symbols, resolves transitive implications, and checks
registry requirements and conflicts. `config-dump` exposes this result as
`featureResolution`, with `registryDigest`, `requestedFeatures`,
`resolvedFeatures`, `resolvedFeaturesDigest`, and per-request `provenance`.
Requested features remain the CMake inputs; preflight and OTA eligibility use
the resolved set.

The project header is a macro-only input loaded before target auto-detection;
do not include JaszczurHAL headers or use derived target/board selectors in it.
Feature definitions used for source selection must be unconditional
`#define HAL_ENABLE_X` or `#define HAL_ENABLE_X 1`; only a same-symbol
`#ifndef HAL_ENABLE_X` guard is supported. Do not put feature definitions under
any other conditional branch because the early collector reads the file
textually.

The generated project uses `jh-vscode` for the same actions as migrated
projects:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode build-debug --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode select-board --project "$PWD" --interactive
```

`build-debug` configures `CMAKE_BUILD_TYPE=Debug` in a separate per-target and
per-board CMake cache. It publishes the result through the same stable artifact
paths as `build`, so upload tasks always use the most recently successful
build.

Generated Cortex-Debug launch configurations provide profiles for RP2040,
RP2350 Arm, and STM32G474 with explicit OpenOCD configuration files, while the
extension resolves GDB from its configured Arm toolchain path. Select the
matching target and board with `Project: Select board` before starting a
profile because the shared Debug pre-launch task builds the active selection.
RP profiles use `interface/cmsis-dap.cfg` plus the matching RP target script. A
Pico in BOOTSEL remains only the target, so OpenOCD needs a separate
CMSIS-DAP/Picoprobe connected to its SWD pins. The STM32G474 profile uses the
NUCLEO-G474RE's on-board ST-Link through `board/st_nucleo_g4.cfg`, connects
under hardware reset, and requires no external probe wiring. Generated RP
profiles also set a validated adapter speed explicitly: 5 MHz for RP2040 and
2 MHz for RP2350. Without that setting, OpenOCD falls back to 100 kHz and
RP2350 flash discovery can exceed GDB's default remote timeout on Windows.

The full generated project should live outside `libraries/JaszczurHAL/vscode/`.
The `vscode/examples/` directory remains a place for lightweight configuration
snippets, not a checked-in firmware project.

The shared snippets and all checked-in example `.vscode` files have one drift
check:

```bash
scripts/examples_dispatcher.py check-template
scripts/examples_dispatcher.py generate-template
scripts/examples_dispatcher.py generate
```

The first command is suitable for CI. The second rewrites the shared snippets
from the task and extension registries. The third rewrites the generated VS
Code files for every example declared in the example registry.

## VS Code Extensions

Check the active VS Code profile against the shared recommendation list:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Install missing entries interactively:

```bash
python3 vscode/tools/manage_vscode_extensions.py --install
```

`--install --yes` provides explicit non-interactive consent for bootstrap or
automation. Every installation is followed by `code --list-extensions`
verification. Use `--code <path>` or `JH_VSCODE_CODE` when the VS Code command
is outside `PATH`.

## VS Code Keyboard Shortcuts

Project `.vscode/keybindings.reference.json` files are references only. VS Code
does not load them automatically and there is no repository-local keyboard
shortcut activation step. Shortcuts work only when the matching entries are
present in the real VS Code user file:

```text
~/.config/Code/User/keybindings.json
```

After migrating a project, make sure that global shortcuts call the maintained
task labels:

```text
Ctrl+Shift+1  Project: Build
Ctrl+Shift+2  Project: Upload
Ctrl+Shift+3  Project: Serial Monitor
Ctrl+Shift+4  Project: Upload (UF2 / BOOTSEL)
Ctrl+Shift+5  Project: Debug Probe Monitor
Ctrl+Shift+6  Project: Refresh IntelliSense
Ctrl+Shift+7  Project: Clean
Ctrl+Shift+8  Project: Upload (OTA)
Ctrl+Shift+9  Project: Config Dump
Ctrl+Shift+Alt+1  Project: Select board (GUI)
Ctrl+Shift+Alt+2  Project: Select board
Ctrl+Shift+Alt+3  Project: Discover OTA devices
```

Old bindings such as `Project: Monitor (persistent)` or
`Project: Monitor (Debug Probe)` will open the "Show all tasks" prompt once
the compatibility aliases are removed from project `tasks.json`.

If `Ctrl+Shift+3` does not start the monitor, inspect the real user
`keybindings.json` first. The correct binding is:

```json
{
    "key": "ctrl+shift+3",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Serial Monitor"
}
```

After editing the user keybindings file, reload the VS Code window if the old
binding is still cached.

After a successful `build`, `upload`, or `upload-uf2`, `jh-vscode` prints a
compact ELF memory map overview when a `firmware.elf` artifact is available.
The overview is derived from `arm-none-eabi-objdump -h`, groups allocated
sections by FLASH/XIP, SRAM, PSRAM, and OTHER, and shows VMA/LMA placement,
section sizes, and short notes. Set `JH_VSCODE_MEMORY_OVERVIEW=0` to suppress
this extra console output.

The serial monitor defaults to `--lock-policy wait`. `replace-own` may stop only
another JaszczurHAL monitor for the same project after validating its versioned
ownership marker, PID, and process start identity. The legacy `replace-any`
spelling follows the same ownership restriction and never terminates an
unrelated process.

For identity-enabled Pico projects, an implicitly configured monitor port may
follow the single CDC device matching the project's verified USB identity when
the saved path disappears or the host assigns a different `ttyACM` or COM
number.
This also covers replacing a board with another unit running the same firmware.
The monitor waits instead of guessing when zero or multiple devices match. An
explicit `--port` remains pinned to the requested path.

For identity-enabled serial uploads, `upload` verifies the selected serial port
before opening it. Linux combines pyserial metadata with by-id aliases and
structured sysfs descriptors, including when pyserial has no matching record.
Windows enumerates COM ports through pyserial and uses VID, PID, serial number,
manufacturer, product, interface, location, and HWID.
Every configured stable field must match; incomplete metadata remains
unverified. First flashing a clean serial-only board must be an explicit
operation with
`--allow-unverified-port --port <port>`. Default VS Code tasks must not pass
this flag.

## Configuration Precedence

Target/board selection, manifest overlays, settings fallback, and local state
precedence are defined only in
[Target And Configuration Resolution](../doc/FwProjectWorkflow.md#target-and-configuration-resolution).

## Minimal Project Manifest

The maintained manifest example and overlay rules live in
[Minimal Manifest](../doc/FwProjectWorkflow.md#minimal-manifest).
Machine validation uses `schema/jh_vscode_project.schema.json`.

## USB Identity

USB identity means firmware descriptors visible to the host as
manufacturer/product, for example in `lsusb`, `dmesg`, VS Code Serial Monitor,
`/dev/serial/by-id`, or Windows COM metadata. Manifests may additionally pin
`usbVid`, `usbPid`, `usbSerialNumber`, `usbInterface`, or `usbLocation`. A COM
number is only a local selection stored in
`.vscode/jaszczurhal.local.json`; it is never treated as stable identity.

For native RP builds, identity is injected through CMake cache entries:

```text
-DJH_USB_MANUFACTURER="Jaszczur"
-DJH_USB_PRODUCT="Router Reset"
```

If identity is disabled or incomplete, the build uses JaszczurHAL's default USB
descriptors.

`clear-identity` is a separate action. It requires `--project`, builds the
neutral firmware from `neutral_fw/rp_native/` through the selected native Pico
SDK target and board, omits custom USB identity cache entries, and flashes a
verified serial target or one unambiguous BOOTSEL device.

`upload-uf2` is RP-family/UF2-only and intentionally keeps manual BOOTSEL simple:
it builds the project, validates every 512-byte UF2 block, requires exactly one
allowed BOOTSEL volume, and refuses to guess when multiple devices are visible.
Linux mounts a single unmounted FAT candidate through `udisksctl` when needed.
Windows reads drive label and filesystem through WinAPI and uses the volume GUID
as the snapshot identity. Both hosts accept only `RPI-RP2`, `RP2350`, or
`RPI-RP2350`. Use target-neutral `upload` for STM32/OpenOCD.

When multiple BOOTSEL volumes are intentionally connected, pass
`--bootsel-volume <drive-root-or-volume-guid>`. The same value may be stored as
`bootselVolume` in the gitignored `.vscode/jaszczurhal.local.json`; a drive root
such as `E:\` is local to one Windows host. The upload still verifies the label
and FAT filesystem before using the selection.

For native RP targets, target-neutral `upload` uses the configured/verified CDC
port for the 1200-bps reset and then follows the same single-drive UF2 safety
rules. The runtime snapshots volume GUIDs before the touch and waits only for a
new allowed volume or an explicitly selected volume. When the configured CDC
path is stale because the selected board is already in BOOTSEL, `upload` falls
back to the single visible BOOTSEL device
instead of rejecting the missing serial identity. When a replacement board is
already running compatible firmware, a stale saved path may instead be replaced
by the single CDC port matching the project's verified USB identity. Zero or
multiple identity matches remain an error. An explicit `--port` never uses
either fallback. The first flash still requires manual BOOTSEL because blank
firmware has no CDC reset endpoint.

When `upload` finds this project's persistent serial monitor on the upload
port, it sends a per-port cooperative release request, waits for the monitor to
close the handle, keeps a short-lived project marker while the upload is in
progress, and lets the monitor reconnect after the board returns. A bounded
fallback may stop only the process whose PID and start identity still match the
ownership marker. Stale markers, PID reuse, and foreign port owners are never
terminated. Busy-port diagnostics include the validated marker PID when an OS
owner lookup is unavailable, as it is for COM ports on Windows.

Windows copies UF2 data as a plain stream, flushes the destination handle, and
closes it before reporting success. Read-only media, a disappearing drive,
short writes, and truncated or inconsistent UF2 artifacts fail with upload exit
code 6. The validator accepts both ordinary per-family sequences and merged OTA
images whose single global block sequence spans multiple family IDs. Native
WinAPI, merged-OTA, and copy-path automation is covered in CI. Real-device
Windows smoke passed on both RP2040/Pico and RP2350/Pico 2, including volume
identity, UF2 validation and copy, reboot, and verified CDC reconnection.

## Native RP OTA

`upload-ota` is the network update path for native `rp2040` and `rp2350-arm`
WiFi board builds with `HAL_ENABLE_OTA`. It builds the project, locates its
`.ota` artifact, signs the image header with the configured password, transfers
it into the device's staging slot and waits for device acceptance. The password
is not embedded in the unsigned build artifact. The `rp2350-riscv` target can
build the OTA container and boot applier, but the current RISC-V board profile
has no CYW43 transport, so it has no operational network upload path.

`ota-discover` broadcasts a JaszczurHAL discovery request and lists hostname,
address, target, port, slot size, image generation and boot mode. Upload
automatically selects one device matching the active target and configured
hostname. If several match, the generated task uses `--interactive`; automation
should set manifest `ota.host` or pass `--host <address>` explicitly.

The `ota` and `artifacts.ota` manifest fields are defined in
[OTA Manifest Configuration](../doc/FwProjectWorkflow.md#ota-manifest-configuration).
Keep secrets outside the tracked manifest with `ota.passwordEnv`. An inline
`ota.password` is intended only for development examples. Empty passwords are
rejected unless `ota.allowEmptyPassword` is explicitly true.
The host callback defaults to TCP/8266. `runmefirst.sh` offers a persistent,
LAN-scoped firewall rule for that port after explicit confirmation.

Firmware integration, first installation, host firewall rules, keyboard
shortcuts, trial confirmation, rollback, recovery, and troubleshooting are
owned by [Native RP OTA Workflow](../doc/OTAWorkflow.md).

The first installation still uses the merged UF2 through BOOTSEL. That image
contains the boot applier and application. Later OTA boots are trials:
application code must call `hal_ota_confirm_boot_ex()` only after its startup
self-tests and required services succeed. Otherwise the boot applier rolls
back after the configured attempt limit. Manual BOOTSEL remains the recovery
path.

The reference application is
[`examples/25_ota`](../examples/25_ota/README.md). Its built-in WiFi transport
supports Pico W/RP2040 and Pico 2 W/RP2350 ARM. The RP2350 RISC-V image and boot
applier are buildable, but the repository does not yet provide a supported
CYW43 network backend for that ISA.

## Exit Codes

```text
0   Success.
1   Generic failure.
2   Invalid CLI usage.
3   Missing or invalid project configuration.
4   Ambiguous or unsafe device selection.
5   Build failed.
6   Upload failed.
7   Monitor failed, including a host without pyserial.
8   Unsupported action, platform path, or incomplete launcher dependencies.
```

Exit code 7 also covers a host without pyserial: the monitor core stays
importable and reports the missing dependency instead of failing at import
time. Exit code 8 covers every host operation the active platform adapter does
not implement and a Windows launcher without a usable Python 3 plus pyserial.

## Generated Files And Build Cache

Artifact roots, compile databases, generated adapters, target/board cache
isolation, stale-cache reset, and cache-key ownership are defined only in
[Build Directories And Generated Files](../doc/FwProjectWorkflow.md#build-directories-and-generated-files).
