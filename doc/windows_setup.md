# Native Windows Setup

JaszczurHAL provides a native Windows bootstrap for the firmware development
environment. It prepares pinned tools without requiring WSL, Git Bash, winget,
Chocolatey, or a global toolchain installation.

The supported host floor is Windows 10 1809 (build 17763), AMD64. Git for
Windows and VS Code must already be installed so the repository can be checked
out and opened. The bootstrap manages Python, CMake, Ninja, GNU Arm Embedded,
GNU RISC-V, OpenOCD, picotool, pyserial, and the pinned source dependencies.
The ESP32-S3 runner additionally prepares the pinned ESP-IDF
checkout and its official target tools on first use; they are cached under
`third_party\esp-idf` and `%USERPROFILE%\.espressif`.
The GNU Arm completeness check includes GDB, and OpenOCD reuse requires the
CMSIS-DAP, ST-Link, RP2040, RP2350, and STM32G4 scripts used by generated
debug configurations.

## Host settings

Native firmware builds require both long-path switches:

- Windows `LongPathsEnabled=1` under
  `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem`;
- Git `core.longpaths=true`.

The default setup verifies these settings and prints the command needed for a
missing Git setting. Passing `-ConfigureHost` explicitly allows the script to
set `core.longpaths`. It can set the Windows registry value only when launched
from a PowerShell session that is already elevated. The script never starts an
elevated process itself.

Endpoint scanning can substantially slow CMake/Ninja builds or quarantine new
`.exe`, `.elf`, and `.uf2` files. Keep the managed tools and build roots short,
and ask the administrator or security team for narrowly scoped exclusions when
measured build performance or quarantines require them. The bootstrap does not
change anti-malware configuration.

## Setup

Keep the checkout on a local Windows volume such as `C:`. Running the native
bootstrap through a WSL UNC path (`\\wsl.localhost\...`) is rejected because
Git for Windows cannot safely own and update that checkout.

Run Windows PowerShell 5.1 or newer from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\runmefirst.ps1
```

The complete plan is printed before the first host or filesystem change. The
default locations are short user-local paths:

```text
%USERPROFILE%\.jh\tools
%USERPROFILE%\.jh\build
```

The managed Python 3.12 base lives below the tools root. Its isolated pyserial
environment is created at `.build\windows\venv`, where `jh-vscode.cmd` finds it
without a global `PATH` change.

The bootstrap also writes `.build\windows\host-environment.json`. The shared
runtime reads this state to select the verified CMake, Ninja, GNU Arm, GNU
RISC-V, OpenOCD, picotool, Python, and managed build root paths. `-VerifyOnly`
checks the state byte-for-byte against the current resolution.

Editor mode also merges the resolved debugger paths into the standard VS Code
user profile as `cortex-debug.openocdPath.windows` and
`cortex-debug.armToolchainPath.windows`. Existing JSONC comments and unrelated
settings are preserved. Before changing an existing file, setup saves
`settings.json.jaszczurhal.bak`; `-VerifyOnly` checks the two values without
writing. `-FirmwareOnly` leaves the VS Code profile unchanged.

Useful modes are:

```powershell
# Allow the documented long-path host settings to be repaired.
.\runmefirst.ps1 -ConfigureHost

# Explicitly allow installation of recommended VS Code extensions.
.\runmefirst.ps1 -InstallExtensions

# Prefer every pinned managed tool over a compatible system installation.
.\runmefirst.ps1 -Force

# Read-only component and host contract check.
.\runmefirst.ps1 -VerifyOnly

# Check a headless firmware builder without requiring or configuring VS Code.
.\runmefirst.ps1 -FirmwareOnly
```

`-VerifyOnly` cannot be combined with either consent switch. A missing,
modified, or stale component fails the check without repair.
`-FirmwareOnly` changes only the final inventory classification: VS Code and
its extensions remain listed as optional, while every firmware prerequisite
stays required. CI combines this mode with `-ConfigureHost` during setup and
uses it again during the read-only verification pass.

The bootstrap reuses compatible system CMake, Ninja, GNU Arm, and OpenOCD
installations unless `-Force` is present. Managed archives are authenticated by
SHA-256, extracted through atomic directory replacement, and recorded with a
complete content manifest. The final report contains every resolved executable
path. OpenOCD reuse additionally requires a complete adjacent or conventional
`share\openocd\scripts` installation; an incomplete system package is skipped
in favor of the managed archive. Running setup again leaves valid components
unchanged.

## Cortex debug and probe drivers

The Windows bootstrap configures Cortex-Debug from its verified host record.
Use `debug-tools` when diagnosing or inspecting the resolved paths for a
particular project:

```powershell
.\vscode\entry\jh-vscode.cmd debug-tools `
  --project .\examples\01_core_runtime `
  --target rp2350-arm --board pico2w --json
```

The result contains `openocd`, `gdb`, `armToolchainPath`, the OpenOCD scripts
root, and the target's diagnostic interface/target configuration pair.
Cortex-Debug resolves `arm-none-eabi-gdb` from the configured toolchain
directory. Generated launch configurations select the complete OpenOCD setup
for each profile and do not depend on project-private
`cortex-debug.gdbPath`, scripts-root, or SVD settings. The managed Raspberry Pi
OpenOCD archive finds its adjacent scripts directory without a global `PATH`
change.

Pico and Pico 2 BOOTSEL USB devices are debug targets and do not provide an
SWD probe over that connection. Cortex debugging requires a separate
Raspberry Pi Debug Probe, a Pico running Debug Probe/Picoprobe firmware, or a
compatible probe wired to SWD. The standard RP configuration uses
`interface/cmsis-dap.cfg`. The NUCLEO-G474RE profile uses its on-board ST-Link
through `board/st_nucleo_g4.cfg`, which selects SWD and the board's hardware
reset behavior; it does not require a separate probe or external SWD wiring.
Generated RP launch profiles set `adapter speed 5000` for RP2040 and
`adapter speed 2000` for RP2350. Do not remove these commands: the bare
CMSIS-DAP/target scripts otherwise fall back to 100 kHz, and RP2350 flash
discovery can exceed GDB's default remote timeout and desynchronize its initial
packet exchange on Windows.

The bootstrap inventories probe devices but does not install, replace, or
rebind Windows USB drivers. If OpenOCD reports that no matching CMSIS-DAP
device exists, check the physical SWD connection and Device Manager first. A
driver change is a separate administrator action: identify the exact probe
interface, review the probe vendor's current Windows instructions, and obtain
consent before changing it. Do not apply a USB driver to the Pico BOOTSEL mass
storage interface.

The native Windows hardware smoke used an official Raspberry Pi Debug Probe
running firmware 2.3.1 and a Pico 2 W as the RP2350 Arm target. Wire probe
`SWDIO` to target `SWDIO`, probe `SWCLK` to target `SWCLK`, and connect their
grounds. Windows exposed the probe through the Microsoft WinUSB driver; no
driver installation or rebinding was required. Managed OpenOCD detected both
Cortex-M33 cores, and managed GNU Arm GDB loaded a Debug ELF, stopped at
`main`, resumed to `app_start`, and detached. A final OpenOCD `reset run`
returned the application USB CDC port. A DoomConsole follow-up also loaded its
Debug ELF and stopped at `app_start` through the same launch-profile contract.

The native STM32 hardware smoke used a NUCLEO-G474RE with its on-board ST-Link
V3J9M3 (`0483:374e`) on Windows 10 LTSC. Managed OpenOCD
`0.12.0+dev (2026-07-01-10:44)` detected a Cortex-M4 r0p1, 512 KiB of dual-bank
flash, six breakpoints, and four watchpoints. Managed GNU Arm GDB programmed
the representative `01_core_runtime` Debug image, stopped first at `main` and
then at `app_start`, detached cleanly, and issued `reset run`. Use the generated
`board/st_nucleo_g4.cfg` profile for this board: a bare
`interface/stlink.cfg` plus `target/stm32g4x.cfg` session may fail target
examination when the board needs the Nucleo hardware-reset configuration.

OpenOCD may report an old Debug Probe/Picoprobe firmware and enable a slower
compatibility workaround. This warning does not prevent SWD debugging. Update
the probe firmware separately using the probe vendor's instructions when the
lower transfer rate matters; the JaszczurHAL bootstrap does not modify probe
firmware.

HTTPS downloads on Windows use the operating system's `curl.exe` and Schannel
trust store with HTTPS-only redirects and TLS 1.2 or newer. This supports
managed enterprise TLS inspection without disabling certificate validation.
Every downloaded archive must still match its pinned SHA-256 before extraction.

## Firmware build layout

Firmware configuration uses Ninja by default on both Windows and Unix. A
project can select another generator with `cmake.generator` in
`.vscode/jaszczurhal.project.json`. The runtime passes its current verified
Python interpreter as `Python3_EXECUTABLE`, enables
`CMAKE_EXPORT_COMPILE_COMMANDS`, and supplies resolved host executable paths as
single process arguments so spaces and semicolon-separated CMake lists remain
intact.

On Windows, CMake caches and compiler dependency files live below the short
bootstrap `BuildRoot`, grouped by a stable project-path hash and target/board.
Final ELF, BIN, HEX, UF2, MAP, OTA, and patched compile-database files remain
under the project's declared `buildDir`. `refresh-intellisense` reads the raw
database from the short CMake tree and writes its stable project copy. Each
successful build also refreshes artifacts from the selected target tree, so a
Ninja no-op after a target switch cannot leave another target's firmware in
`buildDir`. Starting a new build removes the uploadable stable artifact set;
if configuration or compilation fails, a previous target image cannot remain
available for a later upload. `clean` removes both managed locations after
applying the normal path-safety checks.

ESP-IDF projects use their declared `buildDir` directly instead of the short
CMake cache root. The production runner still enforces that the directory is
below a project or repository `.build` root. It records only relative paths in
`jh_esp_idf_artifacts.json`, so the manifest and selected bootloader,
partition-table, application, log, and configuration artifacts can be uploaded
from Windows CI without embedding a runner-specific absolute path.

Use native PowerShell to build the Phase 3 ESP32-S3 compile/link fixture:

```powershell
.\vscode\entry\jh-vscode.cmd build `
  --project .\tests\fixtures\esp32s3_phase3
```

This fixture is compile-only and is not an upload/monitor hardware acceptance
probe. Device projects use `list-ports`, `upload`, and `monitor` with a COM port
reported by the board's USB Serial/JTAG interface.

The selected COM record must match the board registry's `303a:1001` programmer
identity. A stale port, mismatching VID/PID, or several auto-detected matches is
rejected. Upload cooperatively releases a JaszczurHAL-owned monitor and allows
it to reconnect after ESP-IDF resets the board. `--allow-unverified-port` is an
explicit escape hatch for a deliberately selected `--port`; generated tasks do
not use it. ESP32-S3 Debug builds and managed Cortex-Debug profiles are not
provided.

GitHub Actions builds a generated consumer project from a path containing
spaces for RP2040, RP2350 ARM, RP2350 RISC-V, and STM32G474 on native Windows.
The gate checks Ninja configuration, the target static library where
applicable, representative firmware, declared artifacts, the patched compile
database, MSVC warning settings, and the visible disabled classification of
Windows-incompatible POSIX/FreeRTOS/BearSSL host tests. The MSVC job builds and
runs a focused HAL CRC smoke test and the portable BSD socket-header contract
with `/W4 /permissive- /WX`. The full BSD adapter exports POSIX symbol names
and remains a firmware/Linux-host test instead of pretending to implement the
different Winsock ABI. The native BearSSL integration also remains Linux-only
because its harness and transport use Bash and POSIX sockets.

The existing `windows-tooling` job also caches the pinned ESP-IDF checkout and
official tools, performs a clean production build of the compile-only
`tests/fixtures/esp32s3_phase3` project, and uploads its relocatable manifest,
build log, bootloader, partition-table, and application images. A successful CI
build does not establish Phase 3 runtime hardware behavior.

No Windows static-analysis profile is declared by this checkout. The current
managed Windows tool set and this host provide neither `clang-tidy` nor
`cppcheck`, and the MSVC Build Tools are not a pinned bootstrap component.
The strict compiled MSVC warning profile remains the Windows host gate. Add a
static-analysis profile only together with an authenticated, version-pinned
analyzer binary so local and CI results cannot silently drift.

## Troubleshooting

Start with the read-only host and component check:

```powershell
.\runmefirst.ps1 -VerifyOnly
```

Common failure paths are:

- A checkout reached through `\\wsl.localhost\...` is rejected. Clone or move
  the repository to a local Windows volume such as `C:` and run the native
  bootstrap there.
- GNU Arm reports missing C++ headers or Ninja cannot create dependency files.
  Keep `ToolsRoot` and `BuildRoot` short, then verify both Windows and Git
  long-path settings as described above.
- `jh-vscode.cmd` reports an incomplete launcher environment. Re-run setup and
  inspect `.build\windows\host-environment.json`; the launcher requires the
  managed or explicitly selected Python interpreter to import pyserial.
- A COM upload reports access denied or a busy port. Run `Project: List ports`
  or `jh-vscode.cmd list-ports --project <path>` and inspect the reported
  identity and monitor-owner PID. The upload handoff closes only a verified
  JaszczurHAL monitor; close unrelated terminal programs manually.
- An ESP32-S3 COM port is rejected as unverified. Confirm that Device Manager
  or `list-ports --json` reports USB VID/PID `303a:1001` for the selected port;
  disconnect duplicate matching boards or pass the intended verified COM port
  explicitly.
- More than one BOOTSEL device is visible. Disconnect the extra board or pass
  the intended drive root/volume GUID with `--bootsel-volume`; the runtime
  still verifies its label and FAT filesystem.
- Cortex-Debug cannot start OpenOCD or GDB. Run `debug-tools --json` for the
  selected project, confirm the reported files, then check the probe and target
  in Device Manager. Driver changes remain a separate administrator action.
- OTA callback discovery works but transfer cannot connect back to the host.
  Keep the active Windows network profile `Private` and inspect the scoped rule
  without changing it:

  ```powershell
  .\.build\windows\venv\Scripts\python.exe `
    .\scripts\configure_ota_firewall.py --check
  ```

Device selection, monitor ownership, BOOTSEL safety, and task behavior are
described in [JaszczurHAL VS Code Entry](../vscode/README.md). OTA recovery and
trial/rollback diagnostics are in [Native OTA Workflow](OTAWorkflow.md).

## Current support boundary

The native launcher, shared build runtime, generated VS Code task override,
line-ending policy, component manager, host bootstrap, four-family CMake
firmware matrix, COM/BOOTSEL upload paths, OTA firewall backend, debug-tool
discovery, portable socket-header gate, production ESP32-S3 ESP-IDF
build/flash/monitor, and Windows CI are available. Full POSIX socket,
FreeRTOS POSIX, and Bash-driven BearSSL integration tests remain explicitly
Linux-only. The native Windows OTA callback, trial confirmation, and automatic
rollback have been validated on Pico 2 W over a trusted `Private` LAN. OTA
hardware requires local fixture credentials; hardware debug additionally
requires a connected SWD probe. The Fiesta desktop SerialConfigurator remains
a Linux application and is outside the native Windows firmware-workflow scope.
