# JaszczurHAL VS Code Entry Changelog

## 0.1.0 - Unreleased

- Make `build-debug` use an isolated CMake cache with
  `CMAKE_BUILD_TYPE=Debug`, while preserving the stable firmware artifact
  paths used by upload and IntelliSense workflows.
- Store parallel example-dispatcher logs in the host temporary directory so
  the full example matrix works on native Windows as well as POSIX hosts.
- Select `jh-vscode.cmd` for dispatcher builds on Windows instead of passing
  the extensionless POSIX launcher to CreateProcess.
- Pass the target platform explicitly when converting offset OTA application
  images to UF2 so picotool validates RP2040 and RP2350 memory maps correctly.
- Enrich generic Windows `usbser` ports with the parent PnP product and allow a
  product plus configured VID/PID to verify identity when the USB manufacturer
  descriptor is unavailable through Windows device metadata.
- Prepare STM32G474 FreeRTOS dependencies through the shared Python component
  manager so firmware configuration does not execute a Bash helper on Windows.
- Start the shared VS Code entry layer under `libraries/JaszczurHAL/vscode/`.
- Move the Python CLI and persistent-monitor logic into a shared runtime with a
  lazy platform protocol, Linux adapter, compatibility entrypoints, and
  import-safe behavior on hosts without `fcntl`.
- Keep the monitor core importable on hosts without pyserial and report the
  missing dependency through exit code 7 at run time.
- Report host operations missing from the active platform adapter through exit
  code 8 instead of an unhandled error.
- Define the CLI and monitor exit codes in one shared module.
- Add thin Unix and Windows launchers over one public Python entrypoint. The
  Windows launcher verifies pyserial, supports a managed interpreter override,
  preserves arguments and exit codes, and handles paths with spaces and Unicode.
- Add repository line-ending policy for Unix scripts, Windows launchers,
  source text, and binary datasheets/artifacts.
- Generate every standard VS Code task from one shared helper with a Windows
  command override, add a drift gate for the shared template and all checked-in
  examples, and test standalone generator idempotence.
- Add an opt-in VS Code extension installer with interactive or explicit
  non-interactive consent and post-install verification.
- Add multi-target board selection: target registry descriptors, `target` /
  `board` / `targetProfiles` manifest fields, `select-board`, and gitignored
  `.vscode/jaszczurhal.local.json` persistence.
- Add idempotent `sync-board-picker` handling and a trusted-workspace
  `folderOpen` task so existing and generated projects refresh GUI board
  options after the JaszczurHAL registry changes.
- Add Pico SDK dispatcher targets for RP2040, RP2350 ARM and RP2350 RISC-V,
  with shared build/upload/monitor/clean task labels and shortcuts.
- Enable `29_freertos_smoke` on all native RP profiles through the same
  dispatcher and VS Code tasks.
- Route repository-owned examples, hardware fixtures, CMake probes, and the
  picotool build below the single root `.build/` directory. Managed clean and
  stale-cache reset operations accept only project-local output or this root.
- Use Ninja as the default firmware generator, pass the active Python
  interpreter and compile-database setting to CMake, and resolve Windows tools
  from the bootstrap state. Native Windows keeps CMake work below a short
  managed root while artifacts and patched compile commands stay at stable
  project paths.
- Refresh the stable firmware artifact set from the selected target's isolated
  CMake tree after every build, including target switches that Ninja reports as
  having no work.
- Remove stable uploadable firmware before a build attempt so configuration or
  compilation failure cannot leave an image from the previously selected
  target available for upload.
- Add native RP CDC auto-upload: 1200-bps DTR touch, bounded BOOTSEL wait,
  single-drive safety checks and UF2 copy under the existing `upload` action.
- Allow native RP `upload` to use the single visible BOOTSEL device when the
  configured CDC path is stale because the board is already in BOOTSEL;
  explicit `--port` selections remain strict.
- Allow the same stale saved path to follow the single verified CDC identity
  when a replacement board is already running; refuse zero or multiple matches
  and keep explicit `--port` selections strict.
- Let the persistent Pico monitor follow the single verified project CDC when
  an implicit saved path disappears or a board is replaced; explicit ports stay
  pinned and ambiguous identity matches remain unselected.
- Add native RP OTA discovery and authenticated upload, direct-host automation,
  manifest password/environment configuration, signed `.ota` staging and the
  `Ctrl+Shift+8` upload / `Ctrl+Shift+Alt+3` discovery task references.
- Use TCP/8266 as the default OTA callback listener so generated projects match
  the persistent LAN-scoped firewall rule offered by `runmefirst.sh`; explicit
  `ota.listenPort: 0` still selects an ephemeral listener.
- Route generated and migrated firmware projects through the shared
  `cmake/jh_firmware_project` dispatcher; CMake caches are isolated per
  target/board.
- Track manifest-managed CMake cache keys per target/board and unset removed
  entries on the next configure, preventing stale options such as a temporary
  FreeRTOS define from surviving a manifest change.
- Make `tools/create-vscode-example.py` generate dispatcher-backed projects with
  `--target` / `--board`, target-neutral tasks, GUI/interactive board selection,
  and no generated firmware `CMakeLists.txt`.
- Add STM32/OpenOCD upload handling, RP2040-only `upload-uf2` gating, and
  friendly axis-2/axis-3 build diagnostics for missing target backends and
  linker memory overflows.
- Define the initial `jh-vscode` CLI contract and `--project` semantics.
- Add a minimal project manifest schema.
- Add neutral RP firmware for USB identity cleanup.
- Add the Fiesta parity checklist placeholder before migrating Fiesta modules.
- Implement Linux `build`, `build-debug`, `upload`, `upload-uf2`,
  `monitor`, `monitor-probe`, `monitor-any`, `refresh-intellisense`, `clean`,
  and `clear-identity`.
- Add identity-guarded serial upload, simple one-drive BOOTSEL UF2 upload,
  persistent monitor port handoff, and automatic monitor reconnect after upload.
- Add native Windows COM enumeration through pyserial with shared structured
  identity scoring across VID/PID, serial number, manufacturer, product,
  interface, location, HWID, and platform aliases. Normalize `COM10+`, keep the
  chosen COM port in gitignored local state, and distinguish stale, incomplete,
  unmatched, unique, and ambiguous selections.
- Preserve adapter-defined numeric COM ordering, retain structured sysfs
  identity when Linux pyserial has no matching record, and include validated
  monitor-marker PIDs in Windows busy-port diagnostics.
- Add versioned per-port monitor ownership with PID plus process-start identity,
  cooperative release requests, stale/crash cleanup, bounded verified fallback,
  and Windows `msvcrt` build locks. Foreign processes and reused PIDs are never
  terminated.
- Add native Windows BOOTSEL discovery through drive, volume-information, and
  volume-GUID WinAPI calls. Snapshot volume GUIDs before the 1200-bps touch,
  accept only the RP boot labels on FAT media, and support explicit drive/GUID
  selection through user-local configuration or `--bootsel-volume`.
- Include structured BOOTSEL records in `list-ports --json`, preserving the
  existing path list while exposing the mount, volume GUID, label, and
  filesystem used by the upload safety checks.
- Keep standalone project generation successful when a Unicode output path is
  reported through a Windows console with a narrow legacy encoding.
- Write generated project and example files with deterministic LF endings on
  Windows, matching the repository `.gitattributes` policy.
- Validate complete UF2 block groups, including the RP2350 absolute-ignore
  record and merged OTA images with one global sequence across family IDs,
  before upload. Stream the Windows copy without metadata, detect short writes
  and source changes, flush and close the destination, and report read-only,
  removed-volume, and write failures.
- Implement read-only `list-ports`, including project identity matching and
  BOOTSEL candidate reporting, so device checks do not require a risky upload.
- Implement `change-port`: interactive or `--port` selection persisted as the
  user-local `uploadPort` in `.vscode/jaszczurhal.local.json`.
- Allow dispatcher-backed projects to provide `JH_PROJECT_RECIPE` for
  target-specific applications such as doomConsole while retaining the common
  target selection and `jh-vscode` orchestration layer.
- Expand the identity-enabled upload failure message with the required
  verified serial, BOOTSEL, first-flash, and explicit-port conditions.
- Highlight key upload/build status lines in yellow in terminal output:
  verified serial port selection, own-monitor release, selected target/board,
  generated firmware path, and build directory.
- Print an ELF memory map overview after successful build/upload, grouped by
  FLASH/XIP, SRAM, and PSRAM with VMA/LMA ranges, section notes, and totals;
  set `JH_VSCODE_MEMORY_OVERVIEW=0` to disable it.
- Add `tools/create-vscode-example.py`, a standalone CMake-first VS Code
  project generator with blink firmware, USB identity, tasks, launch config,
  and IntelliSense refresh wiring.
- Add Linux CMake target orchestration for firmware projects; current generated
  and migrated projects use the shared dispatcher, while early pilot
  project-owned recipes were folded into that common path.
- Support `cmake.sourceDir` in project manifests so module workspaces can use a
  shared project-owned CMake entry outside the module directory.
- Complete the `router-reset/reseter` pilot on real hardware: verified build,
  debug build, identity-guarded serial upload, wrong-device refusal against
  Fiesta Clocks, persistent monitor reconnect, upload while monitor is active,
  single-drive BOOTSEL UF2 upload, and clear USB identity.
- Clean the `router-reset/reseter` pilot to the new contract: project-owned
  CMake firmware generation, `Project:*` VS Code tasks, root workspace delegation
  to `--project reseter`, with obsolete project build files removed.
