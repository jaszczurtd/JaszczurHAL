# VS Code Project Template for JaszczurHAL - Windows

This folder contains a complete VS Code project configuration template for Arduino RP2040 projects using JaszczurHAL on Windows.

## Quick Start

1. **Copy this folder** to your project directory
2. **Configure environment variables** in VS Code settings (see Configuration)
3. **Open the project** in VS Code
4. **Select build target** using tasks (Ctrl+Shift+P -> Tasks: Run Task)

## Project Structure

```
windows/
├── .vscode/                    # VS Code configuration
│   ├── settings.json          # Arduino CLI and build settings
│   ├── arduino.json           # Arduino sketch configuration
│   ├── tasks.json             # Build, upload, and monitor tasks
│   ├── launch.json            # Debug configurations for RP2040/RP2350
│   ├── c_cpp_properties.json  # C/C++ IntelliSense and include paths
│   ├── extensions.json        # Recommended extensions
│   └── keybindings.reference.json  # Optional keybinding reference
├── scripts/                   # Helper scripts
│   ├── arduino_vscode.py      # Main build orchestration script
│   ├── select-board.sh        # Interactive board selection
│   ├── serial-persistent.py   # Reliable serial monitor (USB VID/PID aware)
│   ├── serial-persistent.sh   # Shell wrapper for serial monitor
│   ├── refresh-intellisense.sh # Regenerate compile_commands.json
│   └── upload-uf2.sh          # UF2 bootloader upload helper
└── hal_project_config.h       # JaszczurHAL module configuration template
```

## Configuration

### 1. Environment Variables (Windows)

Set these in **Settings** -> **Workspace Settings** (`.vscode/settings.json`):

- **`ARDUINO_CLI_PATH`**: Full path to `arduino-cli.exe`
  - Example: `C:\arduino-cli\arduino-cli.exe`
  - Or set via user environment variables

- **`ARDUINO_UPLOAD_PORT`**: Serial port for uploads
  - Example: `COM3` (check Device Manager)
  - Can be changed per task with input prompts

### 2. Arduino CLI Installation

If `arduino-cli` is not installed:

```powershell
# Download from https://github.com/arduino/arduino-cli/releases
# Add to PATH or set ARDUINO_CLI_PATH in settings.json

arduino-cli board list
arduino-cli core install rp2040:rp2040
```

### 3. Board Selection

Interactive board selection:
```powershell
cd scripts
python select-board.sh  # or ./select-board.sh (requires bash/git)
```

This updates `.vscode/settings.json` with:
- **FQBN** (Fully Qualified Board Name)
- **Board options** (flash size, clock frequency, USB stack)

Supported boards:
- Raspberry Pi Pico
- Raspberry Pi Pico W
- Raspberry Pi Pico 2
- Raspberry Pi Pico 2 W
- Waveshare RP2040-Zero
- Waveshare RP2040-Plus

## VS Code Tasks

All tasks are configured in `tasks.json` with convenient keyboard bindings.

| Task | Shortcut | Action |
|------|----------|--------|
| **Arduino: Build** | `Ctrl+Shift+1` | Compile sketch |
| **Arduino: Build (Debug)** | via F5 | Compile with debug symbols |
| **Arduino: Upload** | `Ctrl+Shift+2` | Compile + Upload via USB CDC |
| **Arduino: Upload (UF2)** | `Ctrl+Shift+4` | Compile + Copy to BOOTSEL drive |
| **Arduino: Monitor (persistent)** | `Ctrl+Shift+3` | Smart serial monitor (reconnects on replug) |
| **Arduino: Monitor (Debug Probe)** | `Ctrl+Shift+5` | Monitor for Raspberry Pi Debug Probe |
| **Arduino: Monitor (any port)** | - | Generic serial monitor |

### Smart Serial Monitor

The persistent monitor (`serial-persistent.py`) automatically:
- **Waits for device** to appear (VID:PID filtering)
- **Connects automatically** when available
- **Reconnects after unplug** without user action
- **Supports multiple modes**: Pico USB CDC, Debug Probe, or any port

### Debug Setup Requirements

For live debugging via Debug Probe:
1. Install **Cortex-Debug** extension (`cortex-debug.cortex-debug`)
2. Connect Raspberry Pi Debug Probe to SWD pins
3. Press `F5` to start debugging

## Python Scripts

All scripts require **Python 3.7+** with `pyserial` installed:

```powershell
pip install pyserial
```

### arduino_vscode.py
- Main orchestrator for build and upload
- Handles compilation with Arduino CLI
- Manages environment-specific logic

### serial-persistent.py
- Long-running monitor with automatic reconnection
- Supports filtering by VID:PID (Raspberry Pi Pico: 0x2e8a:0x000a)
- Usage:
  ```powershell
  python serial-persistent.py -m pico    # Pico USB CDC
  python serial-persistent.py -m probe   # Debug Probe
  python serial-persistent.py -m any     # Any serial port
  python serial-persistent.py -p COM3    # Explicit port
  ```

### select-board.sh
- Interactive FQBN and board options selector
- Updates `.vscode/settings.json` automatically
- Bash script (works on Windows with Git Bash or WSL)

## HAL Configuration

Copy `hal_project_config.h` to your sketch directory to customize JaszczurHAL modules:

```cpp
// Example: disable GPS support
#define HAL_DISABLE_GPS

// Example: disable JSON utilities
#define HAL_DISABLE_CJSON
```

See `JaszczurHAL_API.md` for all available `HAL_DISABLE_*` flags.

## IntelliSense and Code Completion

C/C++ IntelliSense is configured in `c_cpp_properties.json`:

- **Include paths**: Points to Arduino SDK, pico-sdk, and JaszczurHAL
- **Defines**: RP2040-specific preprocessor symbols
- **Standards**: C17 and C++17

If IntelliSense is stale, refresh it:
```powershell
python scripts/refresh-intellisense.sh
```

This regenerates `compile_commands_patched.json`.

## Troubleshooting

### Build Fails - "arduino-cli not found"
- Set `ARDUINO_CLI_PATH` environment variable
- Or install arduino-cli via `winget`: `winget install ArduinoAG.CLI`

### Upload Fails - "Port not found"
- Check Device Manager for the correct COM port
- Update `ARDUINO_UPLOAD_PORT` in settings
- For RP2040 in BOOTSEL mode: hold BOOTSEL button while plugging in

### Monitor Not Connecting
- Verify `pyserial` is installed: `pip list | grep serial`
- Check USB cable and port (Device Manager)
- Try "Monitor (any port)" task to diagnose

### Debugging Not Working
- Install Cortex-Debug extension
- Connect Debug Probe to SWD pins (GND, CLK, DIO)
- Press F5 and select RP2040/RP2350 configuration

## Customization

- **Add libraries**: Update `hal_project_config.h` and include paths in `c_cpp_properties.json`
- **Modify tasks**: Edit `tasks.json` for custom build steps
- **Change keybindings**: See `keybindings.reference.json` for available commands

## Additional Resources

- [JaszczurHAL API Documentation](../../JaszczurHAL_API.md)
- [Arduino CLI Documentation](https://arduino.github.io/arduino-cli/)
- [Raspberry Pi Pico Documentation](https://www.raspberrypi.com/documentation/microcontrollers/)
- [Cortex-Debug Extension](https://github.com/Marus/cortex-debug)

## Notes

- All paths using `${workspaceFolder}` are relative to the project root
- Environment variables like `${env:LOCALAPPDATA}` expand at runtime
- Settings are project-scoped (`.vscode/` is not committed to public repos)
- For system-wide Arduino CLI setup, use user environment variables

---

**Version**: 1.0  
**Last Updated**: 2026-04-10  
**Target**: Raspberry Pi Pico / RP2040+ with Arduino Core  
**Language**: C/C++
