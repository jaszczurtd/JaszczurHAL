# VS Code project template for JaszczurHAL

Ready-to-use VS Code configuration and build scripts for Arduino projects
using **JaszczurHAL** on RP2040 / RP2350 boards (Raspberry Pi Pico family).

Copy the contents of this directory into your new project's root to get a
working build/upload/debug/monitor environment out of the box.

---

## Contents

```
vscode-template/
  .vscode/
    settings.json              ← project settings (EDIT THIS)
    tasks.json                 ← build, upload, monitor, clean tasks
    launch.json                ← Cortex-Debug configurations (RP2040 + RP2350)
    extensions.json            ← recommended VS Code extensions
    keybindings.reference.json ← keyboard shortcuts (copy to user keybindings)
  scripts/
    refresh-intellisense.sh    ← compile + generate c_cpp_properties.json
    select-board.sh            ← interactive FQBN selector with menu options
    upload-uf2.sh              ← compile + copy UF2 to BOOTSEL drive
    serial-persistent.py       ← serial monitor (auto-reconnect; pico/probe/any)
  hal_project_config.h         ← template — HAL module disable flags
```

---

## Setup for a new project

### 1. Copy files

```bash
# From the JaszczurHAL repo root:
cp -r vscode-template/.vscode   /path/to/your/project/
cp -r vscode-template/scripts   /path/to/your/project/
cp    vscode-template/hal_project_config.h /path/to/your/project/
```

### 2. Edit `.vscode/settings.json`

Open `.vscode/settings.json` and set:

| Key | What to set |
|---|---|
| `arduino.fqbn` | Your board's FQBN (e.g. `rp2040:rp2040:rpipico`, `rp2040:rp2040:rpipicow`) |
| `arduino.uploadPort` | Serial port (e.g. `/dev/ttyACM0`) |
| `arduino.sketchbookPath` | Absolute path to the directory containing `libraries/` |

Other settings (cliPath, cortex-debug paths) use sensible defaults that work
with a standard `arduino-cli core install rp2040:rp2040` installation.

### 3. Configure HAL modules (optional)

Edit `hal_project_config.h` — uncomment `#define HAL_DISABLE_*` lines for
modules your project does not use.  If the file is present and the build
scripts are used, it is picked up automatically.

### 4. Generate IntelliSense

Run the task **Arduino: Refresh IntelliSense** (`Ctrl+Shift+6`) to compile
the project and auto-generate `c_cpp_properties.json`.

### 5. Install keyboard shortcuts (optional)

Open VS Code keyboard settings (`Ctrl+K Ctrl+S` → `{}` icon) and paste the
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
| `Ctrl+Shift+2` | Upload | Compile + upload via serial port |
| `Ctrl+Shift+3` | Monitor (persistent) | Auto-reconnecting serial monitor (Pico) |
| `Ctrl+Shift+4` | Upload (UF2) | Compile + copy UF2 to BOOTSEL drive |
| `Ctrl+Shift+5` | Monitor (Debug Probe) | Serial monitor for Debug Probe |
| `Ctrl+Shift+6` | Refresh IntelliSense | Compile + regenerate `c_cpp_properties.json` |
| `Ctrl+Shift+7` | Clean | Delete `.build/` directory |
| `Ctrl+Shift+8` | List ports | Show connected Arduino boards |
| `Ctrl+Shift+9` | Change port | Set upload port in settings.json |
| `Ctrl+Shift+0` | Install library | Install a library via arduino-cli |
| `Ctrl+Shift+Alt+1` | Select board (GUI) | Quick FQBN picker dropdown |
| `Ctrl+Shift+Alt+2` | Select board (interactive) | Full menu with advanced options |
| `F5` | Debug | Build (debug) + launch Cortex-Debug |

---

## Notes

- **`c_cpp_properties.json`** is generated by the Refresh IntelliSense script.
  Do not edit it manually — re-run the script after changing boards or libraries.
- All paths in tasks and scripts are dynamic (`${workspaceFolder}`,
  `${config:arduino.*}`, `SCRIPT_DIR`).  No hardcoded project paths.
- The `--build-property "compiler.cpp.extra_flags=-I '${workspaceFolder}'"` in
  build tasks adds the sketch directory to the include path for library
  compilation, enabling the `__has_include("hal_project_config.h")` hook in
  `hal_config.h`.
