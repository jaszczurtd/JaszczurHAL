# VS Code Project Templates for JaszczurHAL

Professional VS Code configurations for building Arduino projects on **RP2040/RP2350** (Raspberry Pi Pico family) using **JaszczurHAL**.

## Overview

This directory contains platform-specific project templates optimized for different operating systems:

- **[Windows](windows/)** — Complete VS Code setup with Python helpers and Arduino CLI integration
- **[Linux](linux/)** — Bash-based build scripts and configuration for Linux development

Both templates include:
- ✅ Full build, upload, and debugging workflows
- ✅ Serial monitor with auto-reconnection
- ✅ IntelliSense configuration for JaszczurHAL modules
- ✅ Support for multiple RP2040/RP2350 boards
- ✅ Optional UF2 bootloader upload
- ✅ Cortex-Debug integration for live debugging

## Quick Start

### For Windows

```powershell
# Copy the Windows template to your project
Copy-Item -Recurse windows/.vscode          -Destination MyProject/
Copy-Item -Recurse windows/scripts          -Destination MyProject/
Copy-Item       windows/hal_project_config.h -Destination MyProject/
```

Then see [windows/README.md](windows/README.md) for configuration and usage.

### For Linux/macOS

```bash
# Copy the Linux template to your project
cp -r linux/.vscode                MyProject/
cp -r linux/scripts                MyProject/
cp    linux/hal_project_config.h   MyProject/
```

Then see [linux/README.md](linux/README.md) for configuration and usage.

## Template Contents

Each platform template includes:

### `.vscode/` Directory
| File | Purpose |
|------|---------|
| `settings.json` | Arduino CLI paths, board settings, verbose output control |
| `arduino.json` | Sketch file, FQBN, port, build directory |
| `tasks.json` | Build, upload, debug, monitor, and UF2 upload tasks |
| `launch.json` | Cortex-Debug configurations for RP2040 and RP2350 |
| `c_cpp_properties.json` | C/C++ IntelliSense, includes, and preprocessor defines |
| `extensions.json` | Recommended VS Code extensions |
| `keybindings.reference.json` | Reference keyboard shortcuts (optional) |

### `scripts/` Directory
| Script | Purpose | Platform |
|--------|---------|----------|
| `arduino_vscode.py` | Main build orchestrator | Windows |
| `build.sh` | Main build orchestrator | Linux |
| `select-board.sh` | Interactive board/options selector | Both |
| `serial-persistent.py` | Smart serial monitor (auto-reconnect) | Both |
| `refresh-intellisense.sh` | Regenerate `compile_commands.json` | Both |
| `upload-uf2.sh` | UF2 bootloader upload | Both |

### Root Configuration File
- **`hal_project_config.h`** — Template for enabling/disabling JaszczurHAL modules
  - Copy to your sketch directory and customize
  - Supported flags: `HAL_DISABLE_GPIO`, `HAL_DISABLE_I2C`, `HAL_DISABLE_GPS`, etc.

## Key Features

### 1. Build Workflows

**Windows** (via `arduino_vscode.py`):
- Compile with Arduino CLI
- Auto-generate compile_commands.json for IntelliSense
- Support for debug symbols

**Linux** (via `build.sh`):
- Similar build process using bash
- Compatible with standard toolchains

### 2. Serial Monitoring

`serial-persistent.py` provides intelligent monitoring:
- Auto-waits for device to appear
- Auto-reconnects after unplug/replug
- USB VID:PID filtering (recognize Pico vs Debug Probe)
- Modes: `pico`, `probe`, `any`, or explicit port

### 3. Debugging

Via **Cortex-Debug** extension:
- Live debugging with GDB
- Breakpoints, watches, and variable inspection
- Requires Debug Probe connected to SWD pins
- Pre-configured for both RP2040 and RP2350

### 4. Board Selection

Interactive selector (`select-board.sh`):
- Choose from 6+ predefined RP2040/RP2350 boards
- Select clock frequency (125 MHz, 150 MHz, 200 MHz)
- Choose optimization level (Small, Optimize, Optimize2, Optimize3, Debug)
- Updates `.vscode/settings.json` automatically

### 5. IntelliSense

`c_cpp_properties.json` includes:
- Arduino SDK and pico-sdk paths
- JaszczurHAL module definitions
- RP2040-specific preprocessor symbols
- Built-in library configurations

## Environment Variables

Both platforms use environment variables for flexibility:

| Variable | Purpose | Example |
|----------|---------|---------|
| `ARDUINO_CLI_PATH` | Arduino CLI executable | `C:\arduino-cli\arduino-cli.exe` |
| `ARDUINO_UPLOAD_PORT` | Serial port for uploads | `COM3` (Windows) or `/dev/ttyACM0` (Linux) |
| `LOCALAPPDATA` | Windows user app data | Auto-set by Windows |

## Keyboard Shortcuts

Default task shortcuts (both platforms):

| Shortcut | Task |
|----------|------|
| `Ctrl+Shift+1` | Build |
| `Ctrl+Shift+2` | Upload (Serial) |
| `Ctrl+Shift+3` | Monitor (persistent) |
| `Ctrl+Shift+4` | Upload (UF2 / BOOTSEL) |
| `Ctrl+Shift+5` | Monitor (Debug Probe) |
| `F5` | Debug (if Cortex-Debug installed) |

## Supported Boards

All templates support:
- ✅ Raspberry Pi Pico
- ✅ Raspberry Pi Pico W
- ✅ Raspberry Pi Pico 2
- ✅ Raspberry Pi Pico 2 W
- ✅ Waveshare RP2040-Zero
- ✅ Waveshare RP2040-Plus
- ✅ Custom RP2040/RP2350-based boards (via FQBN configuration)

## Getting Started

1. **Choose your platform**: [Windows](windows/) or [Linux](linux/)
2. **Read the platform-specific README**
3. **Copy files** to your project root
4. **Configure environment variables** (paths, COM ports)
5. **Run a task**: Press `Ctrl+Shift+1` to build

## Customization

### Add Custom Libraries

Edit `c_cpp_properties.json` and `hal_project_config.h`:

```json
"includePath": [
    "${workspaceFolder}",
    "${workspaceFolder}/../../../libraries/MyCustomLib/src",
    // ... other paths
]
```

### Modify Tasks

Edit `.vscode/tasks.json` to add custom build steps, preprocessor flags, or optimization settings.

### Change Keyboard Shortcuts

Edit `.vscode/keybindings.json` (create if it doesn't exist):

```json
[
    {
        "key": "ctrl+alt+b",
        "command": "workbench.action.tasks.runTask",
        "args": "Arduino: Build"
    }
]
```

## Troubleshooting

### Cannot find arduino-cli
- **Windows**: Add `%ARDUINO_CLI_PATH%` to System Environment Variables
- **Linux**: Install via package manager: `sudo apt install arduino-cli`

### Monitor not connecting
- Check Device Manager (Windows) or `lsusb` (Linux) for the device
- Verify `ARDUINO_UPLOAD_PORT` is correct
- Try "Monitor (any port)" task to test connectivity

### IntelliSense not showing completions
- Run refresh-intellisense: `python scripts/refresh-intellisense.sh`
- Restart VS Code C/C++ extension
- Check Include path paths in `c_cpp_properties.json`

### Debugging fails
- Install **Cortex-Debug** extension
- Verify Debug Probe connected to SWD pins
- Check `.vscode/settings.json` for correct `cortex-debug.*` paths

## Platform-Specific Notes

### Windows
- Paths use backslashes and drive letters
- Environment variables: `${env:VARIABLE_NAME}`
- Serial ports: `COM1`, `COM2`, etc.
- Uses Python `.py` scripts (Python 3.7+ required)

### Linux
- Paths use forward slashes
- Environment variables: `$VARIABLE_NAME`
- Serial ports: `/dev/ttyACM0`, `/dev/ttyUSB0`, etc.
- Uses bash shell scripts

## Additional Resources

- 📖 [JaszczurHAL API Documentation](../JaszczurHAL_API.md)
- 🔗 [Arduino CLI Documentation](https://arduino.github.io/arduino-cli/)
- 🔗 [Raspberry Pi Pico Documentation](https://www.raspberrypi.com/documentation/microcontrollers/)
- 🔗 [Cortex-Debug Extension](https://github.com/Marus/cortex-debug)
- 🔗 [Arduino Core for RP2040](https://github.com/earlephilhower/arduino-pico)

## License

These templates are part of **JaszczurHAL** and follow the same license.

## Support

For issues or questions:
1. Check platform-specific README (Windows or Linux)
2. Review JaszczurHAL_API.md for module documentation
3. Consult Arduino CLI and Cortex-Debug documentation
4. Check serial monitor output for build/upload errors

---

**Version**: 1.0  
**Last Updated**: 2026-04-10  
**Supported Platforms**: Windows (10+), Linux (Any distribution), macOS (experimental)
