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
- Add Linux CMake target orchestration for projects that generate their Arduino
  compatibility sketch from project-owned `CMakeLists.txt` files.
