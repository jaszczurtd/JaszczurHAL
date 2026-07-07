# JaszczurHAL VS Code Entry Changelog

## 0.1.0 - Unreleased

- Start the shared VS Code entry layer under `libraries/JaszczurHAL/vscode/`.
- Define the initial `jh-vscode` CLI contract and `--project` semantics.
- Add a minimal project manifest schema.
- Add a neutral RP2040 Arduino-Pico firmware sketch for USB identity cleanup.
- Add the Fiesta parity checklist placeholder before migrating Fiesta modules.
- Implement Linux `build`, `build-debug`, `upload`, `upload-uf2`,
  `monitor`, `monitor-probe`, `monitor-any`, `refresh-intellisense`, `clean`,
  and `clear-identity`.
- Add identity-guarded serial upload, simple one-drive BOOTSEL UF2 upload,
  persistent monitor port handoff, and automatic monitor reconnect after upload.
- Expand the identity-enabled upload failure message with the required
  verified serial, BOOTSEL, first-flash, and explicit-port conditions.
- Highlight key upload/build status lines in yellow in terminal output:
  verified serial port selection, own-monitor release, Arduino FQBN, generated
  sketch path, and Arduino build directory.
- Print an ELF memory map overview after successful build/upload, grouped by
  FLASH/XIP, SRAM, and PSRAM with VMA/LMA ranges, section notes, and totals;
  set `JH_VSCODE_MEMORY_OVERVIEW=0` to disable it.
- Add Linux CMake target orchestration for projects that generate their Arduino
  compatibility sketch from project-owned `CMakeLists.txt` files.
- Support `cmake.sourceDir` in project manifests so module workspaces can use a
  shared project-owned CMake entry outside the module directory.
- Complete the `router-reset/reseter` pilot on real hardware: verified build,
  debug build, identity-guarded serial upload, wrong-device refusal against
  Fiesta Clocks, persistent monitor reconnect, upload while monitor is active,
  single-drive BOOTSEL UF2 upload, and clear USB identity.
- Clean the `router-reset/reseter` pilot to the new contract: project-owned
  CMake sketch generation, `Project:*` VS Code tasks, root workspace delegation
  to `--project reseter`, and no dead Arduino legacy file.
