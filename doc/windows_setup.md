# Native Windows Setup

JaszczurHAL provides a native Windows bootstrap for the firmware development
environment. It prepares pinned tools without requiring WSL, Git Bash, winget,
Chocolatey, or a global toolchain installation.

The supported host floor is Windows 10 1809 (build 17763), AMD64. Git for
Windows and VS Code must already be installed so the repository can be checked
out and opened. The bootstrap manages Python, CMake, Ninja, GNU Arm Embedded,
GNU RISC-V, OpenOCD, picotool, pyserial, and the pinned source dependencies.

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

# Check a headless firmware builder without requiring VS Code extensions.
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

GitHub Actions builds a generated consumer project from a path containing
spaces for RP2040, RP2350 ARM, RP2350 RISC-V, and STM32G474 on native Windows.
The gate checks Ninja configuration, the target static library where
applicable, representative firmware, declared artifacts, the patched compile
database, MSVC warning settings, and the visible disabled classification of
Windows-incompatible POSIX/FreeRTOS/BearSSL host tests. The MSVC job builds and
runs a focused HAL CRC smoke test with `/W4 /permissive- /WX`. The smoke target
is independent from the GNU-oriented full mock backend, so firmware support
does not imply that every historical host mock is portable to MSVC; generated
project-file inspection is not the only warning-policy check.

## Current support boundary

The native launcher, shared build runtime, generated VS Code task override,
line-ending policy, component manager, host bootstrap, four-family firmware
matrix, and Windows CI are available. Native COM/BOOTSEL device adapters and
hardware upload gates are tracked by the remaining Windows-support stages.
Keep the Linux gate available while those device gates are being completed.
