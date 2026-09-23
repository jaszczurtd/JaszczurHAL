<a id="firmware-project-workflow"></a>

# Working with firmware projects

*Also available in [Polish](../pl/FwProjectWorkflow.md).*

This chapter explains how to create, configure, build, and upload firmware projects using JaszczurHAL. The same workflow applies to user projects and checked-in examples. It covers the version-controlled manifest, target and board selection, source files, generated outputs, and separate CMake caches for each configuration.

For CLI actions and device checks before upload, see [JaszczurHAL VS Code Entry](../../vscode/README.md). For descriptor fields and generated metadata, see [Target and board profiles](boards_profiles_howto.md). Network updates are covered in [Native OTA Workflow](OTAWorkflow.md).

## Project layout

A firmware project normally contains:

```text
my-device/
  app.c or app.cpp
  hal_project_config.h
  .vscode/
    jaszczurhal.project.json
    settings.json
    tasks.json
    launch.json
    keybindings.reference.json
    extensions.json
```

`extensions.json` recommends the extensions the project files depend on:
`ms-vscode.cpptools` for IntelliSense, `ms-vscode.cmake-tools` as the
`C_Cpp.default.configurationProvider` set in `settings.json`,
`marus25.cortex-debug` for the `launch.json` debug configuration and
`ms-vscode.vscode-serial-monitor` alongside the `jh-vscode` monitor actions.
VS Code offers to install missing entries when the folder is opened.

Generated `launch.json` files specify the OpenOCD interface and target scripts. On Windows, run `jh-vscode debug-tools --project <path> --json`, then set the reported OpenOCD executable and Arm toolchain directory in the Cortex-Debug user settings. The extension locates GDB in that directory. These machine-specific paths are not stored in version-controlled project files.

On Debian/Ubuntu-like Linux hosts, `runmefirst.sh` installs `gdb-multiarch` and
generated settings select it through `cortex-debug.gdbPath.linux`. The
STM32G474 profile uses `board/st_nucleo_g4.cfg` with connect-under-reset so the
on-board ST-Link can recover a running target before GDB attaches.

For RP and STM32, set `toolchain: "cmake"` and point `cmake.sourceDir` to `libraries/JaszczurHAL/cmake/jh_firmware_project`. `JH_PROJECT_DIR` identifies the application directory. For ESP32 and ESP32-S3, select `toolchain: "esp-idf"`; the target registry supplies the build runner and artifact-manifest path. The shared tool selects the appropriate build system, so no project-local CMake configuration is needed.

Generate a working standalone project with:

```bash
libraries/JaszczurHAL/vscode/tools/create-vscode-example.py \
  --output my-device --target rp2040 --board pico
```

Generated `tasks.json` provides GUI and terminal board selection, OTA discovery and upload, and `Project: Sync board picker`. The synchronization task runs on `folderOpen`, reads the current board registry, and updates the version-controlled GUI choices only when needed. It also creates or repairs RP2040, RP2350 ARM, and STM32G474/ST-Link debugger profiles in `launch.json`, using the manifest's ELF file and preserving user-added configurations. VS Code requires a trusted workspace and may request one-time approval for automatic tasks.

The terminal task `Project: Select board` reads the registry each time it runs. Generated tasks use `jaszczurhal.vscodeEntry` on Unix and the `jaszczurhal.vscodeEntryWindows` override on Windows. These settings select the adjacent `jh-vscode` and `jh-vscode.cmd` launchers, which run the same Python code.

To check or regenerate version-controlled generated files, including shared configuration snippets and example projects, run:

```bash
python3 scripts/sync_generated.py --check
python3 scripts/sync_generated.py --write
```

Recommended extensions can be checked without changing the VS Code profile:

```bash
python3 vscode/tools/manage_vscode_extensions.py
```

Passing `--install` requests confirmation before installing missing entries.
Automation can use `--install --yes` after obtaining consent.

## Core terms

- **Project directory**: path passed to `--project` and normally stored as
  `JH_PROJECT_DIR`.
- **Manifest**: tracked `.vscode/jaszczurhal.project.json`.
- **Local state**: gitignored `.vscode/jaszczurhal.local.json`, containing a
  developer's selected target, board, and serial port.
- **Target**: stable build ID: `rp2040`, `rp2350-arm`, `rp2350-riscv`,
  `stm32g474`, `esp32`, `esp32s3`, or `mock`.
- **Board**: stable physical profile ID such as `pico`, `picow`, `pico2`,
  `pico2w`, `pico-rm2`, `rp2040-zero`, `rp2040-plus-4mb`,
  `nucleo-g474re`, `esp32-devkitc-v4`, or `waveshare-esp32-s3-zero`.
- **Board registry**: generated tooling view of `boards/targets/*.json`,
  `boards/profiles/*.json`, and `boards/capabilities.json`.
- **`JH_TARGET` / `JH_BOARD`**: CMake cache values selected by the dispatcher
  before SDK/toolchain import.
- **`HAL_TARGET_*`**: compile-time HAL backend selector generated or inferred
  from the resolved build target.

<a id="target-and-configuration-resolution"></a>

## Selecting the target and configuration

The target and board are selected in the following order, from highest priority:

1. invocation overrides such as `--target rp2040 --board picow`;
2. `.vscode/jaszczurhal.local.json`;
3. tracked manifest `target` and `board`;
4. registry default `rp2040/pico`.

Settings are then merged in this order. Later values override earlier ones:

1. target and board registry defaults;
2. base manifest;
3. active `targetProfiles.<target>` overlay;
4. resolved `JH_TARGET` and `JH_BOARD`;
5. action-specific options such as `--port`, `--host`, `--verbose`, and
   `--allow-unverified-port`.

`.vscode/settings.json` contains editor paths and display preferences. Store project identity, build directories, source layout, target profiles, artifacts, and OTA settings in the manifest. Standalone projects created by `create-vscode-example.py` also copy the initial shared build settings to `cmake.configureSettings`, allowing CMake Tools to configure the project without calling `jh-vscode`.

Before diagnosing a build or upload problem, inspect the complete configuration after all settings have been merged:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

The output includes `featureResolution`, with `registryDigest`, `requestedFeatures`, `resolvedFeatures`, `resolvedFeaturesDigest`, and `provenance`. The last field identifies the source of each requested setting. The result accounts for the active target profile, variant, and all manifest overlays.

<a id="target-matrix"></a>

## Supported targets

| Target | ISA | Default board | Firmware format | Upload |
|---|---|---|---|---|
| `rp2040` | Cortex-M0+ | `pico` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `rp2350-arm` | Cortex-M33 | `pico2` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `rp2350-riscv` | Hazard3 RISC-V | `pico2` | ELF/BIN/HEX/UF2/MAP | verified CDC to BOOTSEL, or direct BOOTSEL |
| `stm32g474` | Cortex-M4F | `nucleo-g474re` | ELF/BIN/HEX/MAP | OpenOCD |
| `esp32` | dual-core Xtensa LX6 | `esp32-devkitc-v4` | ELF/MAP plus bootloader, partition-table, and application BIN images | ESP-IDF flash through the verified USB-UART bridge |
| `esp32s3` | dual-core Xtensa LX7 | `waveshare-esp32-s3-zero` | ELF/MAP plus bootloader, partition-table, and application BIN images | ESP-IDF flash through verified USB Serial/JTAG |
| `mock` | host | `host-mock` | host executable/library | none |

The board registry checks whether the board is compatible with the target. It also supplies the provider platform, physical flash and PSRAM parameters, GPIO domain, board components and capabilities, programmer identity, and upload defaults. An unknown target/board pair fails before the compiler starts.

## Minimal manifest

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "pico",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${buildDir}/cmake",
  "cmake": {
    "sourceDir": "${project}/../libraries/JaszczurHAL/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "tracker"
    }
  },
  "identity": {
    "enabled": true,
    "usbManufacturer": "Jaszczur",
    "usbProduct": "My Device",
    "byIdHint": "My_Device"
  }
}
```

Ninja is the default build generator; select another one explicitly with `cmake.generator`. The tool always enables the compilation database and passes its Python interpreter to CMake. On native Windows, the CMake working tree is placed under the short path recorded by `runmefirst.ps1`. The manifest's `buildDir` remains the stable location for final artifacts and `compile_commands_patched.json`.

Store common settings in the base manifest and target-specific differences in small overlays:

```json
{
  "targetProfiles": {
    "stm32g474": {
      "board": "nucleo-g474re",
      "cmake": {
        "cache": {
          "JH_EXTRA_DEFINES": "APP_STM32_BUILD=1"
        }
      }
    }
  }
}
```

The resolved registry values always pin the final `JH_TARGET` and `JH_BOARD`.

An ESP32-S3 project uses the smaller provider-specific manifest shape:

```json
{
  "project": "my-device",
  "module": "tracker",
  "toolchain": "esp-idf",
  "target": "esp32s3",
  "board": "waveshare-esp32-s3-zero",
  "buildDir": "${project}/.build/esp32s3"
}
```

The target and board registry supplies the build runner, artifact manifest, upload method, required FreeRTOS feature, and programmer identity `303a:1001`. Do not duplicate these settings in the project manifest.

## Adding project source files

The shared CMake project automatically discovers `*.c`, `*.cpp`, `*.h`, and
`*.hpp` directly under `JH_PROJECT_DIR`.

```text
tracker/
  app.cpp
  hal_project_config.h
  gps_filter.c
  gps_filter.h
```

For sources in subdirectories, provide the complete list:

```json
{
  "cmake": {
    "cache": {
      "JH_PROJECT_SOURCES": "app.cpp;hal_project_config.h;filters/gps.c;filters/gps.h"
    }
  }
}
```

`JH_PROJECT_SOURCES` is a semicolon-separated list of paths relative to `JH_PROJECT_DIR`. Providing it replaces automatic discovery in the project root.

Additional shared files can be appended with `JH_EXTRA_SOURCES`:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_SOURCES": "../common/product_identity.cpp"
    }
  }
}
```

The shared CMake configuration normalizes paths and removes duplicates.

The ESP-IDF runner discovers C, C++, and assembly files in the project root and recursively under `src/`. When invoking the runner directly, repeated `--source <relative-path>` arguments can replace automatic discovery. Every source path must stay within the project.

<a id="feature-and-runtime-configuration"></a>

## Selecting features and the runtime

Put project feature flags in `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_WIFI
#define HAL_ENABLE_MQTT
#define HAL_ENABLE_APP_TASK1
```

`JH_EXTRA_DEFINES` is useful for target profiles, build variants, and CI:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS;APP_DIAGNOSTICS=1"
    }
  }
}
```

Enable a feature with `HAL_ENABLE_X` or `HAL_ENABLE_X=1`. After selecting the active target profile and example variant, the shared build configuration and `jh-vscode` reject `HAL_ENABLE_X=0` and other explicit values with `[JH-CFG-VALUE]`. To disable a feature, omit its symbol.

This rule does not apply to ordinary parameters such as `APP_DIAGNOSTICS=0`. In definition lists, each `HAL_ENABLE_*` entry must be a separate, simple token, and entries must be separated by semicolons. Whitespace is not a separator. CMake generator expressions are not supported.

The `esp32s3` descriptor requires `HAL_ENABLE_FREERTOS` and supports the peripheral and network/service features described as Phases 2 and 3. These include APP_TASK1, UART, I2C controller/target, SPI, PWM_FREQ, RGB_LED, PCNT, STACK_GUARD, BLE, WiFi, TCP/UDP, BSD sockets, TLS, HTTP client/server/files, WebSocket server, MQTT, time, OTA, and WireGuard.

The baseline always includes simple PWM and core system, synchronization, GPIO, ADC, serial, and timer sources. Requesting a feature or dependency outside the descriptor's allowlist fails with `[JH-CFG-UNSUPPORTED]`.

The initial `esp32` descriptor deliberately supports fewer features. It includes the required FreeRTOS runtime and `HAL_ENABLE_BLUETOOTH_GAMEPAD`, which selects Bluedroid, BR/EDR, and ESP HID Host. Features available only on ESP32-S3, including the public BLE API, are rejected during preflight.

With a Fiesta-convention `firmware_entry.h`, setting `FIESTA_ENABLE_CORE1=1` also requires `HAL_ENABLE_APP_TASK1` in `hal_project_config.h` or another standard feature-configuration input. This keeps the generated entry adapter, requested and resolved feature sets, and link signature consistent.

The feature registry resolves all transitive dependencies consistently across targets and tools. The generated C header defines the implied macros, CMake selects sources and dependencies, and the board generator calculates `featureHash` and the link signature from the same result. `jh-vscode` uses that result for preflight and OTA eligibility while passing the original feature requests to CMake. Define `HAL_CONFIG_VERBOSE` to print a report of all active registered features during compilation.

Rules that depend on configuration parameters, the build provider, board capabilities, or the selected target remain in `hal_config.h`. These include enabling I2C for the selected EEPROM type, choosing the default GPS transport, validating backend/provider compatibility, checking board capabilities, and applying target-specific limits.

`hal_project_config.h` is read before target auto-detection and before derived target and board macros exist. Keep it macro-only: it may directly define `HAL_TARGET_*`, `HAL_BOARD_PROFILE_*`, `HAL_ENABLE_*`, and tuning parameters. Do not include JaszczurHAL headers or condition its contents on `HAL_TARGET_IS_*` / `HAL_BOARD_IS_*`.

Feature definitions used for source selection must be unconditional `#define HAL_ENABLE_X` or `#define HAL_ENABLE_X 1`. The only supported exception is a same-symbol `#ifndef HAL_ENABLE_X` guard. No other `#if`/`#ifdef`, including raw or derived target/board conditions, is supported: at this stage, the tool reads the file text rather than preprocessor output.

Select the physical platform and board through `target` and `board`. The project defines application wiring, USB identity, secrets, partition policy, and enabled features.

## Build directories and generated files

External firmware projects store outputs in `${project}/.build`. Checked-in examples and hardware test configurations use stable locations relative to the JaszczurHAL root:

```text
.build/examples/<example>/
.build/hardware/<fixture>/
```

On Unix, each project CMake cache is isolated by target and board below the
manifest `cmakeBuildDir`:

```text
<cmakeBuildDir>/<target>/<board>/
```

This keeps toolchains, build-provider platforms, generated board headers, and linker layouts from sharing one cache. Native Windows instead uses the short path prepared during setup:

```text
<BuildRoot>/<project-name>-<path-hash>/cmake/<target>/<board>/
```

The original `compile_commands.json` resides in the relevant CMake tree. The tool writes the adjusted `compile_commands_patched.json` to the stable `buildDir` and refreshes the selected target's firmware artifacts after every build. This also happens when switching back to a previously configured target and Ninja has nothing to rebuild.

`jh-vscode` records manifest-managed cache keys in `.jh-vscode-cache-keys.json`. Removing a key from the manifest removes it from the cache on the next configure. Changing the CMake source directory recreates a stale cache if that cache is inside the managed artifact directory.

For ESP-IDF projects, `buildDir` is the IDF build tree itself. It must be inside the project's or JaszczurHAL repository's `.build` directory, or one of their subdirectories. The runner manages generated project and SDK settings there without creating a second board registry.

Generated outputs include:

- resolved board CMake/header/JSON and link-signature translation units;
- raw `compile_commands.json` in the CMake tree and stable
  `compile_commands_patched.json` in `buildDir`;
- `.vscode/c_cpp_properties.json`;
- ELF/BIN/HEX/UF2/MAP or ELF/BIN/HEX/MAP target artifacts;
- for ESP-IDF, `jh_esp_idf_artifacts.json`, the application ELF/MAP/BIN,
  bootloader and partition-table images, `sdkconfig`, build log, generated
  board/link metadata, toolchain provenance, and the raw compile database;
- OTA container and merged recovery UF2 when OTA is enabled.

Tracked configuration remains in the manifest and `hal_project_config.h`.

## Build and upload actions

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode upload --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode monitor --project "$PWD"
../libraries/JaszczurHAL/vscode/entry/jh-vscode clean --project "$PWD"
```

`Project: Upload` selects the upload method from the registry. On RP with running firmware, it verifies the device over USB CDC, switches to BOOTSEL, and uploads the UF2. For a blank board, use `Project: Upload (UF2 / BOOTSEL)`. STM32G474 invokes the OpenOCD upload target.

On ESP32-S3, the tool performs a validated build, checks every path in the multi-image manifest, and passes the verified serial port to ESP-IDF flashing. The board profile specifies VID/PID `303a:1001`. A missing device, stale path, mismatched identity, or multiple matches stops the operation. `--allow-unverified-port` explicitly bypasses the port check and requires `--port`.

During upload, the tool releases the port held by the project's persistent serial monitor. The monitor can reconnect when the device enumerates again. An ambiguous BOOTSEL volume or serial identity stops the operation.

On ESP32-S3, `Project: Serial Monitor` selects the single device matching the registry's programmer identity unless a port is specified explicitly. `Project: Refresh IntelliSense` uses the Xtensa compilation commands generated by ESP-IDF, without substituting an Arm mode. `build-debug` and managed Cortex-Debug profiles are not provided for ESP32-S3.

## OTA manifest configuration

For RP projects built with CMake, the manifest identifies the generated OTA container and build metadata alongside the shared connection settings:

```json
{
  "cmake": {
    "cache": {
      "JH_EXTRA_DEFINES": "HAL_ENABLE_OTA",
      "JH_OTA_GENERATION": 7,
      "JH_OTA_VERSION": "1.4.0"
    }
  },
  "artifacts": {
    "ota": "${buildDir}/firmware.ota"
  },
  "ota": {
    "hostname": "tracker-office",
    "port": 8266,
    "listenPort": 8266,
    "passwordEnv": "TRACKER_OTA_PASSWORD"
  }
}
```

ESP-IDF projects do not use the RP-specific `cmake` and `artifacts.ota` entries. Their build manifest identifies the raw application BIN. The `ota` object above still configures host-side addresses, ports, and authentication.

`ota.broadcast` specifies the UDP discovery destination, while `ota.host` selects a fixed device address. `ota.listenPort` is the host port for the reverse TCP connection and for UDP discovery replies. Its default, `8266`, matches the persistent LAN-scoped firewall rules created by `runmefirst.sh`; `0` requests ephemeral ports. `ota.passwordEnv` keeps the secret in an environment variable rather than the version-controlled manifest.

The device hostname, UDP port, and password must match the firmware configuration. [Native OTA Workflow](OTAWorkflow.md) covers platform-specific artifacts, initial programming, tasks, authentication, the host firewall, trial-boot confirmation, rollback, and recovery. For RP updates, the uploader signs the JaszczurHAL container. For ESP-IDF, it validates the build manifest and sends the specified raw application image without converting it to the RP container format.

## Examples and variants

Example manifests may declare `example.targets` and `example.variants`.
Variants can override module name, sources, feature definitions, supported
targets, and CMake cache entries.

```bash
scripts/examples_dispatcher.py list
scripts/examples_dispatcher.py build --target rp2040 --example 01_core_runtime
```

The validation suite uses generated example manifests as build inputs. See [JaszczurHAL Examples](../../examples/README.md).
