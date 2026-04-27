# VS Code project template for JaszczurHAL

Ready-to-use VS Code configuration and build scripts for Arduino projects
using **JaszczurHAL** on RP2040 / RP2350 boards (Raspberry Pi Pico family).

Copy the contents of this directory into your new project's root to get a
working build/upload/debug/monitor environment out of the box. Every file is
meant to be edited - this is a starting point, not a framework.

---

## Contents

```
vscode-template/linux/
  .vscode/
    settings.json              ← project settings (EDIT THIS)
    tasks.json                 ← build, upload, monitor, clean tasks
    launch.json                ← Cortex-Debug configurations (RP2040 + RP2350)
    extensions.json            ← recommended VS Code extensions
    keybindings.reference.json ← keyboard shortcuts (copy to user keybindings)
  scripts/
    build.sh           ← single entry point for Build / Debug / Upload
    refresh-intellisense.sh    ← compile + generate c_cpp_properties.json
    select-board.sh            ← interactive FQBN selector with menu options
    upload-uf2.sh              ← compile + copy UF2 to BOOTSEL drive
    serial-persistent.py       ← persistent serial monitor (see notes below)
  hal_project_config.h         ← template - HAL module disable flags
```

> **Note on serial monitor scripts**
> `serial-persistent.py` is the primary serial-monitor path going forward.
> It auto-reconnects on unplug, waits for the device on startup, and
> re-reads the preferred port from `settings.json` while running, so
> **Change port** (`Ctrl+Shift+9`) takes effect without restarting the
> monitor. A separate one-shot `serial-monitor.py` is deliberately omitted
> from the Linux template - the persistent variant is stricter superset
> and should be the default.

---

## Setup for a new project

### 1. Copy files

```bash
# From the JaszczurHAL repo root:
cp -r vscode-templates/linux/.vscode   /path/to/your/project/
cp -r vscode-templates/linux/scripts   /path/to/your/project/
cp    vscode-templates/linux/hal_project_config.h /path/to/your/project/
chmod +x /path/to/your/project/scripts/*.sh
```

### 2. Edit `.vscode/settings.json`

Open `.vscode/settings.json` and set the required fields:

| Key | What to set |
|---|---|
| `arduino.fqbn` | Your board's FQBN (e.g. `rp2040:rp2040:rpipico`, `rp2040:rp2040:rpipicow`) |
| `arduino.uploadPort` | Serial port (e.g. `/dev/ttyACM0`) |
| `arduino.sketchbookPath` | Absolute path to the directory containing `libraries/` |

Optional fields are documented inline in the file. See
[Per-device USB identity](#per-device-usb-identity) below for the
`build.usbManufacturer` / `build.usbProduct` pair.

### 3. Configure HAL modules (optional)

Edit `hal_project_config.h` - uncomment `#define HAL_DISABLE_*` lines for
modules your project does not use.  If the file is present and the build
scripts are used, it is picked up automatically.

### 4. Generate IntelliSense

Run the task **Refresh IntelliSense** (`Ctrl+Shift+6`) to compile
the project and auto-generate `c_cpp_properties.json`.

### 5. Install keyboard shortcuts (optional)

Open VS Code keyboard settings (`Ctrl+K Ctrl+S` -> `{}` icon) and paste the
contents of `.vscode/keybindings.reference.json` into your `keybindings.json`.

---

## System dependencies

### Required

| Dependency | Install command (Debian/Ubuntu) | Purpose |
|---|---|---|
| **arduino-cli** | [see docs](https://arduino.github.io/arduino-cli/installation/) | Compile, upload, library/core management |
| **Python 3** | `sudo apt install python3` | Scripts (IntelliSense, board selector, serial monitor) |
| **pyserial** | `pip3 install pyserial` | Serial monitor scripts |

### Arduino cores and libraries (via arduino-cli)

```bash
# RP2040/RP2350 core (Earle Philhower)
arduino-cli core update-index \
  --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core install rp2040:rp2040 \
  --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

JaszczurHAL and its dependencies must be visible to arduino-cli.  The
recommended approach is to place them under `<sketchbookPath>/libraries/`:

```
<sketchbookPath>/
  libraries/
    JaszczurHAL/
    Adafruit_GFX_Library/
    Adafruit_ILI9341/           (or Adafruit_SSD1306, depending on display)
    Adafruit_BusIO/
    Adafruit_NeoPixel/
    mcp_can/                    (MCP_CAN_lib)
    canDefinitions/             (if your project uses CAN)
    ... (other project-specific libs)
```

Libraries you don't use can be omitted if you disable the corresponding HAL
module via `hal_project_config.h`.

### Optional (for hardware debugging)

| Dependency | Install command | Purpose |
|---|---|---|
| **Cortex-Debug** extension | VS Code marketplace: `marus25.cortex-debug` | GDB + OpenOCD debug sessions |
| **Raspberry Pi Debug Probe** or **Picoprobe** | hardware | SWD debugging interface |
| OpenOCD | bundled with arduino-pico core | Debug server (no separate install needed) |

---

## Available tasks

| Shortcut | Task | Description |
|---|---|---|
| `Ctrl+Shift+1` | Build | Compile the sketch |
| `Ctrl+Shift+2` | Upload | Compile + upload via serial port + auto-start persistent monitor |
| `Ctrl+Shift+3` | Monitor (persistent) | Auto-reconnecting serial monitor (Pico); live port re-read |
| `Ctrl+Shift+4` | Upload (UF2) | Compile + copy UF2 to BOOTSEL drive |
| `Ctrl+Shift+5` | Monitor (Debug Probe) | Persistent monitor for Debug Probe |
| `Ctrl+Shift+6` | Refresh IntelliSense | Compile + regenerate `c_cpp_properties.json` |
| `Ctrl+Shift+7` | Clean | Delete `.build/` directory |
| `Ctrl+Shift+8` | List ports | Show connected Arduino boards |
| `Ctrl+Shift+9` | Change port | Set upload port in settings.json (live-applied to running monitor) |
| `Ctrl+Shift+0` | Install library | Install a library via arduino-cli |
| `Ctrl+Shift+Alt+1` | Select board (GUI) | Quick FQBN picker dropdown |
| `Ctrl+Shift+Alt+2` | Select board (interactive) | Full menu with advanced options |
| `F5` | Debug | Build (debug) + launch Cortex-Debug |

---

## Adapting `tasks.json` to your project

The shipped `.vscode/tasks.json` is intended as a working starting point.
Typical adaptations:

- **Rename tasks**: the `label` field is what VS Code shows in the task
  picker. Keybindings in `keybindings.reference.json` refer to tasks by
  label - if you rename a task, update the `args` field in the
  corresponding keybinding entry.
- **Change the build entry point**: every Build / Debug / Upload task
  delegates to `scripts/build.sh`. If your project has a different
  build flow (e.g. CMake, PlatformIO, a Makefile), point the `command`
  field at that instead. The entry script is expected to accept a single
  positional argument: `build`, `debug`, or `upload`.
- **Remove tasks you don't use**: e.g. if you never use Debug Probe, drop
  "Monitor (Debug Probe)" and "Ctrl+Shift+5" from your
  keybindings.
- **Add project-specific tasks**: e.g. a `Quality: cppcheck`, `Test: host`,
  `Package: .uf2 release` task. Follow the same structure - keep
  `presentation.panel` as `"shared"` for stateless tasks and
  `"dedicated"` for long-running ones like the monitor.
- **Inputs**: `inputs[]` at the top of the file back the `${input:*}`
  references. Extend with your own prompt types when adding interactive
  tasks.

The central `build.sh` reads all its configuration from
`.vscode/settings.json` (FQBN, upload port, optional USB identity, optional
`-Werror`, sketchbook path), so most project-level tuning happens there,
not in `tasks.json`.

---

## Per-device USB identity

The `arduino-pico` core (Earle Philhower) lets the firmware advertise
custom USB **manufacturer** and **product** strings. These show up in:

- `dmesg` / `journalctl` on Linux (`usb X: Product: ...`)
- `lsusb -v` under `iManufacturer` / `iProduct`
- VS Code Serial Monitor device picker
- macOS `system_profiler SPUSBDataType`

This is the cleanest way to disambiguate multiple boards with identical
VID:PID plugged into the same host - each device comes up with its own
human-readable name instead of an anonymous "Board CDC".

The template exposes this as two optional fields in `.vscode/settings.json`:

```jsonc
{
    "build.usbManufacturer": "Acme Robotics",
    "build.usbProduct":      "Robot-Arm Controller v2"
}
```

When set, `scripts/build.sh` passes them through to `arduino-cli`
as:

```text
--build-property "build.usb_manufacturer=\"Acme Robotics\""
--build-property "build.usb_product=\"Robot-Arm Controller v2\""
```

You can also hard-code them per project by editing `build.sh`
directly - the settings-based approach just keeps them alongside the
other per-project values. Leave both empty to fall back to the core's
defaults ("Raspberry Pi" / "Pico").

**Tip - per-module identities in a multi-module project**: if a single
repository ships firmware for several boards, give each one a different
`build.usbProduct` (e.g. `"FleetBot Motor"`, `"FleetBot Sensor"`). Host
tooling can then pin a friendly name per device by iProduct string - no
need to track `/dev/ttyACM*` numbers across reboots.

---

## Debugging with Raspberry Pi Debug Probe

Cortex-Debug (`marus25.cortex-debug`) runs OpenOCD against the Raspberry
Pi Debug Probe over CMSIS-DAP, so you get GDB, breakpoints, variable
watches, and live-watch samples straight from VS Code. The shipped
`.vscode/launch.json` exposes four configurations:

| Configuration | Purpose |
|---|---|
| Debug: RP2040 (Pico/Pico W/Zero/Plus) | Flash + break at `main` (RP2040) |
| Debug: RP2350 (Pico 2/Pico 2 W) | Flash + break at `main` (RP2350) |
| Debug: Attach RP2040 (no upload) | Attach without re-flashing |
| Debug: Attach RP2350 (no upload) | Attach without re-flashing |

### Hardware setup

1. **Probe firmware**: use Raspberry Pi Debug Probe firmware v2 or later
   (USB VID/PID `2e8a:000c`). Older Picoprobe-style firmware (`2e8a:0004`)
   also works but speaks a different protocol; the shipped config uses
   CMSIS-DAP (`interface/cmsis-dap.cfg`) which the current probe firmware
   implements natively.
2. **SWD wiring** to the target:
   - Probe **SC** -> target **SWCLK**
   - Probe **SD** -> target **SWDIO**
   - Probe **GND** -> target **GND**
   - (Optional) Probe **TX/RX** -> target UART for log capture via Probe.
3. **Power**: either power the target over USB separately, or take 3.3 V
   from the probe's debug header if your target supports it.

### Software setup

1. Install the `marus25.cortex-debug` extension (VS Code will prompt you
   because it is listed in `.vscode/extensions.json`).
2. OpenOCD and `arm-none-eabi-gdb` are bundled with the `rp2040:rp2040`
   Arduino core - no separate install. They live under
   `~/.arduino15/packages/rp2040/tools/`.
3. Fill in the `cortex-debug.*` paths in `.vscode/settings.json`.
   The template ships placeholders with `<version>` / `<core-version>`
   segments; substitute them with the versions `arduino-cli` actually
   installed:

   ```bash
   ls ~/.arduino15/packages/rp2040/tools/pqt-openocd/     # e.g. 4.1.0-1aec55e
   ls ~/.arduino15/packages/rp2040/tools/pqt-gcc/         # same version string
   ls ~/.arduino15/packages/rp2040/hardware/rp2040/       # e.g. 5.5.1 (core version)
   ```

   The resulting settings.json entries (example values):

   ```jsonc
   "cortex-debug.openocdPath":
     "${userHome}/.arduino15/packages/rp2040/tools/pqt-openocd/4.1.0-1aec55e/bin/openocd",
   "cortex-debug.gdbPath":
     "${userHome}/.arduino15/packages/rp2040/tools/pqt-gcc/4.1.0-1aec55e/bin/arm-none-eabi-gdb",
   "cortex-debug.svdFileRP2040":
     "${userHome}/.arduino15/packages/rp2040/hardware/rp2040/5.5.1/pico-sdk/src/rp2040/hardware_regs/RP2040.svd"
   ```

### Usage

- **F5** starts the default configuration. For first-time use, pick it
  once from the Run & Debug panel's dropdown and it becomes the default.
- The `launch` configs run `preLaunchTask: "Build (Debug)"` so the ELF
  at `${workspaceFolder}/.build/${workspaceFolderBasename}.ino.elf` is
  always up-to-date. `attach` configs skip the build step.
- The `openOCDLaunchCommands` entry sets the SWD clock to 5 MHz for both
  RP2040 and RP2350. Lower it (e.g. `2000`) if you see probe errors on
  a long/noisy cable.

### Troubleshooting

| Symptom | Likely cause |
|---|---|
| `Error: open failed` from OpenOCD | Probe not enumerated (check `lsusb`), or wiring reversed |
| `Error: unable to find CMSIS-DAP device` | Probe firmware too old, or another process holds the device |
| `No SWD target found` | Target not powered, SWCLK/SWDIO swapped, or ground missing |
| `cortex-debug.openocdPath` is empty | Version placeholder still in settings.json; substitute it |
| Live-watch stuck | Sampling rate too high for the target; reduce `samplesPerSecond` |

---

## Notes

- **`c_cpp_properties.json`** is generated by the Refresh IntelliSense script.
  Do not edit it manually - re-run the script after changing boards or libraries.
- All paths in tasks and scripts are dynamic (`${workspaceFolder}`,
  `${config:arduino.*}`, `SCRIPT_DIR`).  No hardcoded project paths.
- The `--build-property "compiler.cpp.extra_flags=-I '${workspaceFolder}'"`
  applied by `build.sh` adds the sketch directory to the include
  path for library compilation, enabling the
  `__has_include("hal_project_config.h")` hook in `hal_config.h`.
- The persistent monitor is started as a background process by the
  Upload task so that log output is visible immediately after the board
  re-enumerates. Remove the `start_persistent_monitor` call in
  `build.sh` if you prefer to start monitors manually.
