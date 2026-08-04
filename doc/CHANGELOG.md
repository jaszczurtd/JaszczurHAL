# Changelog

All notable changes to this project will be documented in this file.

## [1.9.0] - 2026-xx-xx

### Windows OTA, debug, and extended host gates

- Extended the automatic board-picker synchronization to create or repair the
  complete managed Cortex-Debug profile set in existing consumer projects,
  including STM32G474/ST-Link, without removing custom debugger configurations;
  `${jhRoot}` artifacts in repository examples remain workspace-relative.
- Updated the public support boundary after the completed hardware gates:
  native Windows COM/BOOTSEL is no longer marked as pending, while the Fiesta
  desktop SerialConfigurator remains explicitly Linux-only.
- Split shared OTA firewall validation from its host backends and added an
  idempotent Windows Defender Firewall rule limited to an active `Private`
  profile, interface alias, RFC1918 subnet, and callback TCP port, with
  read-only check/dry-run modes, consent, and an explicit elevation boundary.
- Moved the RP OTA hardware verifier from a direct `termios` dependency to the
  shared pyserial adapter used on Windows and Unix hosts, and aligned its
  identity checks with the generated board-profile runtime names.
- Let the Pico SDK select picotool's UF2 family for relocated applications,
  avoiding an unsupported extra argument while retaining the SDK-provided
  RP2040 and RP2350 address-map metadata.
- Kept native RP OTA transfers responsive while writing staging flash by
  erasing each 4 KiB sector immediately before its first page instead of
  blocking the network stack while erasing the complete slot.
- Added verified OpenOCD, GNU Arm GDB, scripts-root, and target-configuration
  discovery through `jh-vscode debug-tools`; generated launch files now name
  their CMSIS-DAP and RP target scripts explicitly.
- Made the native Windows bootstrap preserve and configure the standard VS Code
  JSONC user profile with OS-specific Cortex-Debug OpenOCD and GNU Arm paths;
  verify-only checks drift and firmware-only leaves editor settings unchanged.
- Removed the need for legacy project-private Cortex-Debug path, scripts-root,
  and SVD variables from generated launch profiles.
- Added a generated STM32G474/NUCLEO-G474RE Cortex-Debug profile backed by the
  on-board ST-Link and OpenOCD's board-level reset configuration, with shared
  launch generation and drift coverage across all checked-in examples.
- Added validated 5 MHz RP2040 and 2 MHz RP2350 CMSIS-DAP speeds to generated
  profiles; this avoids OpenOCD's 100 kHz fallback and the resulting RP2350
  GDB handshake timeout on Windows.
- Validated the managed Windows OpenOCD/GDB path with an official Raspberry Pi
  Debug Probe running firmware 2.3.1 and a Pico 2 W target, including Debug ELF
  loading, hardware breakpoints at `main` and `app_start`, resume, and
  application restart.
- Validated the STM32 profile natively on Windows with a physical
  NUCLEO-G474RE: managed OpenOCD detected the STM32G474, GNU Arm GDB programmed
  the Debug ELF, stopped at `main` and `app_start`, and restarted the target.
- Validated the generated STM32 profile on Linux Mint 22.2 with the same
  NUCLEO-G474RE and on-board ST-Link. Linux setup now includes
  `gdb-multiarch`, OpenOCD script discovery validates the selected target, and
  the STM32 launch profile connects under reset before loading the Debug ELF.
- Isolated the Linux `gdb-multiarch` resolver fixture from managed Windows host
  records so the cross-platform build-environment test exercises the intended
  fallback consistently in Linux and Windows CI.
- Kept the fixed-size native RP OTA boot applier within its flash partition in
  application Debug builds by retaining its production assertion policy.
- Added an MSVC/GNU portable BSD socket-header gate and documented the full
  POSIX adapter, Bash BearSSL integration, and FreeRTOS POSIX scheduler as
  Linux-only host tests. Windows static analysis remains undeclared until an
  authenticated version-pinned analyzer is part of the managed tool set.

### Cross-platform VS Code launchers

- Added `jh-vscode.cmd` and a public Python entrypoint so Windows and Unix
  launch the same shared runtime with preserved arguments and exit codes.
- Added generated Windows task overrides, a shared task/keybinding registry,
  standalone-generator idempotence coverage, and a drift gate for both the
  shared snippets and every checked-in example project.
- Added explicit line-ending and binary-file attributes, including forced LF
  for hash-pinned upstream sources, plus a consent-gated, verified VS Code
  extension installer.

### Native Windows bootstrap

- Added a cross-platform Python component manager for exact Git refs,
  submodules, authenticated ZIP/`tar.gz` archives, atomic replacement, content
  manifests, version stamps, custom directories, and verify-only operation.
- Converted the Unix `ensure_*` scripts and central updater into compatibility
  launchers for the shared manager.
- Added a plan-first Windows PowerShell bootstrap with pinned Python 3.12 and
  hash-verified pyserial, short managed roots, compatible system-tool reuse,
  pinned CMake/Ninja/GNU Arm/GNU RISC-V/OpenOCD/picotool fallbacks, and an Arm
  RP2040-multilib `<cstdlib>` compiler self-check. Incomplete system OpenOCD
  installations fall back to the managed archive.
- Added the public Windows host inventory, CMake 3.20 contract, registry build
  fallback, explicit host/extension consent switches, and verify-only tests for
  idempotency and damaged content/stamps.
- Added a firmware-only inventory scope for headless builders and CI while
  retaining visible optional editor diagnostics, and made CI repair and verify
  the required Windows long-path settings explicitly.
- Preserved Linux picotool self-repair when USB or signing support becomes
  available, with the same command-capability checks used on Windows.
- Routed managed Windows HTTPS downloads through the operating system's
  `curl.exe` and Schannel trust store while retaining HTTPS-only redirects,
  TLS validation, and pinned SHA-256 verification, so enterprise-inspected
  hosts can complete the bootstrap.

### Cross-platform firmware builds and Windows CI

- Made Ninja the firmware default with a manifest override, passed the active
  Python interpreter and compile-database option explicitly, and replaced the
  registry's fixed picotool path with runtime host resolution.
- Added a bootstrap-recorded short Windows CMake root while retaining stable
  project artifact and patched compile-database locations, including paths with
  spaces and semicolon-separated cache lists.
- Added ELF/BIN/HEX/UF2/MAP artifact gates, compiler-aware GNU/MSVC host warning
  policy, visible Windows-disabled CTest entries for POSIX/FreeRTOS/BearSSL
  cases, and native Windows CI builds for all four target families.
- Refresh stable artifacts after every selected-target build, including Ninja
  no-op target switches, and make STM32 flash segments read/execute without an
  RWX linker warning.
- Invalidated uploadable stable firmware before each build attempt so a failed
  target switch cannot expose an older image, installed and verified Ninja in
  the Unix bootstrap and gate 1, and made the MSVC CI probe compile and run
  real code.
- Kept the STM32G474 host-compiler sanity configuration available through the
  explicit `JH_STM32_HOST_SANITY` option, so the clang-tidy gate and the
  static-analysis and STM32 CI jobs keep their host database while a firmware
  configuration without a cross toolchain still stops.
- Kept forced managed-tool resolution during the CI verify-only pass and
  included the hidden stable artifact directory in Windows firmware uploads.
- Made standalone project generation use absolute repository references when
  the destination and JaszczurHAL checkout are on different Windows volumes.
- Made the MSVC host gate enter the installed developer environment explicitly
  and build with Ninja instead of relying on CMake's Visual Studio discovery.
- Scoped the strict MSVC host smoke gate to a standalone HAL CRC target instead
  of requiring the complete GNU-oriented mock backend to compile on Windows.
- Added structured BOOTSEL records to `list-ports --json`, including the mount,
  volume GUID, label, and filesystem used by the Windows upload safety checks,
  while preserving the existing path list for compatibility.
- Enriched Windows `usbser` COM records with the parent PnP product and added a
  strict product plus configured VID/PID fallback when Windows exposes only the
  driver manufacturer, preserving identity-guarded consumer uploads.
- Routed STM32G474 FreeRTOS dependency preparation through the shared Python
  component manager so native Windows configuration does not require Bash.
- Normalized Windows drive paths embedded in CMake cache strings and lists so
  migrated consumer sources do not become invalid CMake escape sequences.
- Made `build-debug` select `CMAKE_BUILD_TYPE=Debug` in an isolated CMake cache
  while retaining the stable artifact paths consumed by upload tasks.
- Initialized the MFRC522 cascade response state exposed by the strict Debug
  warning policy.

### Compiler portability layer

- Added `hal/hal_compiler.h` as the single source for the compiler extensions
  the HAL relies on: `HAL_NORETURN`, `HAL_FORCE_INLINE`, `HAL_TRAP()`,
  `HAL_UNREACHABLE()`, the `HAL_PACKED` structure trio and `hal_clz32()`, each
  resolved for GNU, Clang and MSVC.
- Replaced the raw `noreturn`, `always_inline`, `__builtin_trap()` and
  `__builtin_clz()` uses in JaszczurHAL code with those macros, and moved the
  former `JH_HAL_NORETURN` ladder out of `hal_config.h`. Linker-level
  attributes, inline assembly and vendored sources keep their explicit form on
  purpose.
- Extended the host compiler smoke test to compile and run the new macros, so
  both the Linux gate and the MSVC CI job validate them. One translation unit
  forces the portable fallback through the overridable identity macros and
  compares its leading-zero count against the builtin path, keeping the branch
  no real compiler selects under test.

### Configuration header boundaries

- Split source-compatibility helpers, assertions and runtime configuration
  into standalone `hal_compat.h`, `hal_assert.h` and `hal_runtime_config.h`
  public headers while retaining `hal_config.h` as a compatible facade.
- Moved the target-aware `hal_assert_fail()` implementation from
  `hal_config.cpp` into its matching `hal_assert.cpp` translation unit.
- Moved the maintained `HAL_ENABLE_*` catalog out of the compile-time header
  and into the configuration documentation, and added strict C, C++ and MSVC
  coverage for the new standalone public headers.

### Managed media, JSON, storage and test dependencies

- Replaced the tracked cJSON, LodePNG and picojpeg source copies with pinned,
  git-ignored checkouts under `third_party/`, each controlled by its own
  version file and `ensure_*` helper.
- Switched BearSSL and LodePNG to project-owned forks and replaced picojpeg with
  the clean `jaszczurtd/TJpg_Decoder` Tiny JPEG Decompressor core.
- Preserved the C LodePNG ABI through a thin wrapper and isolated the GCC 15
  RISC-V optimizer false positive with a source-specific build option.
- Added clean-checkout enforcement to the cJSON, LodePNG and JPEG dependency
  helpers; no managed component requires a tracked patch file.
- Replaced the formatted FatFs source copy with an exact-commit checkout of the
  project-owned `jaszczurtd/ff16` mirror of ChaN's unchanged R0.16 archive,
  avoiding the unreliable runtime download from `elm-chan.org` while retaining
  tracked feature and configuration wrappers.
- Replaced the tracked Unity source copy with a clean exact-commit checkout from
  the project fork while preserving `HAL_ENABLE_UNITY` and project configuration
  through thin integration wrappers.
- Updated the component inventory, generated SBOM, dependency documentation
  and CI preparation steps for the managed components.

### Host OTA setup

- Added an idempotent `runmefirst.sh` firewall preflight for the default
  TCP/8266 OTA callback. It detects the default RFC1918 LAN, asks before any
  change, uses active UFW/firewalld/iptables tooling, installs only missing
  iptables persistence support, and verifies the resulting persistent rule.
- Changed the host OTA callback default from an ephemeral port to TCP/8266;
  projects can still request an ephemeral listener explicitly with
  `ota.listenPort: 0`.

### Declarative board profiles

- Added versioned target, board, and capability descriptors for RP2040,
  RP2350 ARM/RISC-V, NUCLEO-G474RE, host mock, RP2040-Zero, and the exact
  RP2040-Plus 4 MB variant.
- Added configure-time descriptor validation and deterministic generated
  CMake/C headers, resolved diagnostics, physical board facts, and controlled
  component selection.
- Made native RP and STM32 static libraries board-aware and added a
  target/board/feature link-time contract symbol that rejects incompatible
  archives.
- Added unit, golden, semantic-negative, determinism, flash, WS2812, and
  positive/negative contract-link tests.
- Switched jh-vscode, project generation, example dispatch, upload metadata,
  config dumps, and artifact-layout tests to the declarative `boards/` registry;
  removed the duplicate `vscode/targets/` descriptors and raw Pico board
  selectors from resolved project cache.
- Removed the legacy unknown-RP-board fallback to the Pico profile: builds
  without generated board config now require an explicit
  `HAL_BOARD_PROFILE_*` selector and fail with a hard compile error
  otherwise.
- Added the `cmake/jh_board_components.cmake` component registry mirrored
  against the generator registry; every official flow validates resolved
  components (unknown ID, provider mismatch, exclusive-slot conflict) and
  exposes `JH_BOARD_COMPONENT_<ID>` flags.
- Added a `pico`/`pico-rm2` descriptor drift test and a generator/CMake
  component-registry synchronization test.

### Official Pico SDK native build

- Restored CYW43 factory-MAC selection from the radio OTP. Radios without a
  programmed OTP address now use the Pico SDK-compatible locally administered
  fallback derived from the unique UID suffix, avoiding collisions between
  boards that share the same UID prefix. The public WiFi API and lwIP netif now
  read the controller-owned MAC instead of a stale pre-initialization cache,
  preserving Raspberry Pi addresses such as `28:CD:C1:xx:xx:xx`.
- Implemented non-blocking RP CYW43 station join for
  `hal_wifi_begin_station_ex(..., true)`; polling the WiFi state now advances
  association and DHCP instead of the join returning `HAL_EUNSUPPORTED`.
- Expanded the native RP quality gate from representative smoke/storage
  projects to every declared example: 56 RP2040, 56 RP2350 ARM and 45 RP2350
  RISC-V projects. The manifest contract now rejects unclassified targets,
  boards and variants.
- Added build-gated bare-metal/FreeRTOS hardware fixtures for concurrent CDC
  producers on both RP cores and for SDLogger mount/write/close/reset/remount,
  exact appended-content verification and EEPROM counter persistence.
- Made hardware fixture manifests reproducible after clone by tracking only
  `tests/hardware/*/.vscode/jaszczurhal.project.json` while keeping local VS
  Code state ignored.
- Fixed native example warnings exposed by the complete matrix: bounded
  thermocouple diagnostics, the Pico SDK 2.2 PIO program metadata for
  NeoPixel, unused BSD socket example helpers, and a GCC 15 RP2350 RISC-V
  interprocedural false positive in the upstream LodePNG copy helper.
- Added an explicit native OTA hardware fixture for Pico W, Pico 2 W and
  Pico+PIM730 in bare-metal and FreeRTOS variants. All six hardware
  combinations passed discovery, wrong-password rejection, acknowledged
  transfer, trial confirmation, an unconfirmed second image, automatic
  rollback, and USB/network recovery across reboots.
- Fixed the native OTA boot applier handoff on RP2040 and RP2350 ARM by
  transferring VTOR, MSP and control to the selected program reset vector
  after clearing inherited interrupt state.
- Replaced board-specific CYW43 PIO dividers with a target gSPI frequency.
  The RP transport now derives its 16.8 divider from the live `clk_sys`,
  selects the upstream high/low-speed sampling program, and reports the
  effective clock to the hardware OTA fixture.
- Completed the native USB hardware matrix on Pico W and Pico 2 W: 1 MiB CDC
  echo with delayed-read backpressure, close/reopen, 1200-bps upload, monitor
  handoff/reconnect, Linux runtime suspend/resume and post-resume echo.
- Made native UF2 upload select only the BOOTSEL device created by the current
  1200-bps touch, so an explicitly selected board cannot be confused with a
  second board that was already waiting in BOOTSEL.
- Enabled the shared FatFs `39_sdlogger` example for native RP2040, RP2350 ARM
  and RP2350 RISC-V builds, made native RP2040 its default VS Code target and
  added the complete native matrix to CI.
- Added native RP OTA for RP2040 and RP2350: versioned target-bound images,
  payload SHA-256, password-derived HMAC-SHA256, redundant boot state, equal
  program/staging slots and a resumable sector swap with trial confirmation,
  rollback and BOOTSEL recovery.
- Added VS Code OTA discovery and authenticated upload, `ota` manifest
  configuration, build-time `.ota` packaging, merged boot/application UF2
  output, `Project: Upload (OTA)` / discovery tasks and documented keyboard
  shortcuts. Example `57_ota` covers Pico W and Pico 2 W.
- Preserved the top-level `ota` object while loading project manifests and
  added an optional fixed host TCP `listenPort` for callback firewall rules.
  OTA discovery now sends a direct query when `ota.host` is configured instead
  of ignoring it and relying on broadcast.
- Padded every touched non-final flash sector in merged OTA UF2 images, matching
  the Pico SDK workaround for RP2040-E14 so a sparse boot-applier/application
  boundary cannot prevent the application from being programmed by BOOTSEL.
- Added exhaustive host failure-boundary tests for manifest/state persistence
  and interrupted program/staging swaps, plus native OTA build coverage for
  RP2040 and RP2350 ARM.
- Added native Pico SDK EEPROM/KV and LittleFS storage for RP2040 and RP2350.
  EEPROM uses a RAM mirror and one coordinated erase/program commit; LittleFS
  routes every flash program and erase through the shared RP transaction
  coordinator.
- Added linker-enforced native RP storage reservations: 4 KiB EEPROM at the
  physical flash tail and an opt-in 64 KiB LittleFS partition immediately
  before it. The official Pico SDK still sees the physical flash size while
  the firmware link region excludes both partitions.
- Replaced the vendored littlefs core with a pinned v2.11.3 managed checkout
  under `third_party/` and reused it on native RP and STM32G474 targets.
- Added a repeatable physical storage fixture. EEPROM persistence, explicit
  LittleFS format/remount and mount-after-reset pass on Pico/RP2040 and Pico 2
  with both RP2350 ARM and RISC-V firmware; RISC-V also mounts the filesystem
  previously written by the ARM image without formatting.
- Consolidated repository-owned build artifacts below `.build/`: host and
  target builds, examples, hardware fixtures, CMake compiler probes,
  IntelliSense databases, and picotool no longer create root `build_*`,
  per-example `.build`, source-checkout build, or root `.o` output.
- Centralized BearSSL, lwIP, littlefs, FreeRTOS-Kernel, Pico SDK, picotool and
  the RP2350 RISC-V toolchain under `third_party/`: tracked version files are
  the source of truth, ignored installations are synchronized or replaced by
  `update_components.sh`, and `runmefirst.sh` uses that single entry point.
- Added an artifact-layout regression contract and constrained static/native
  build helper `--output` paths to the managed `.build/` tree.
- Added a shared native RP flash transaction coordinator for bare-metal and
  FreeRTOS SMP firmware. It serializes callers, coordinates core 1 and local
  IRQs, pauses TinyUSB, rejects active DMA and XIP callbacks/contexts, preserves
  bounded timeouts, and restores runtime state after success or failure.
- Added host sequencing/error-precedence tests plus a physical Pico/Pico 2
  fixture covering both cores, USB quiesce/resume, active DMA rejection,
  recursive/XIP rejection, erase/program verification and recovery after an
  interrupted mutation. The fixture passes on RP2040 and RP2350 ARM/RISC-V
  with both bare-metal and FreeRTOS SMP runtimes.
- Propagated native FreeRTOS kernel usage requirements into final firmware
  targets so Pico SDK interface sources such as `pico_flash` select their
  scheduler-aware SMP implementation.
- Added the pinned FreeRTOS V11.3.0 kernel and its release-matched community
  ports for native RP2040, RP2350 ARM_NTZ and RP2350 RISC-V, with HAL-owned
  scheduler startup and core-affined application tasks.
- Added a FreeRTOS-aware core-0 TinyUSB worker, native heap/runtime reporting,
  `29_freertos_smoke` support in all RP VS Code profiles and a repeatable
  scheduler/SMP/mutex/USB hardware probe.
- Corrected RP2350 RISC-V tick rescheduling by preserving the
  `xTaskIncrementTick()` result across the community port's ISR critical
  section, restoring delayed-task wakeups and preemption.
- Added the status-first `hal_usb` API, a HAL-owned native TinyUSB CDC device
  lifecycle, descriptors, background pump, bounded writes and 1200-bps BOOTSEL
  reset for RP2040, RP2350 ARM and RP2350 RISC-V.
- Made `hal_serial` a client of the HAL-owned `hal_usb` TinyUSB lifecycle and
  added a deterministic USB mock.
- Added native VS Code auto-upload through a verified serial port: release the
  project monitor, perform the 1200-bps DTR touch, wait for one BOOTSEL drive,
  copy UF2 and reconnect the monitor.
- Added a reproducible RP USB CDC hardware probe. Physical Pico and Pico 2
  validation covers RP2040, RP2350 ARM and RP2350 RISC-V enumeration, exact
  1 MiB echo with delayed-read backpressure, reconnect, Linux runtime
  suspend/resume, 1200-bps upload and monitor handoff.
- Integrated native `rp2040`, `rp2350-arm` and `rp2350-riscv` targets with the
  shared VS Code dispatcher, board selector and example manifests while keeping
  the build/upload/monitor/clean tasks unchanged.
- Added `rp_native_lib/` and `scripts/build_rp_native_lib.sh` for direct,
  self-contained builds against the pinned official Pico SDK on RP2040, RP2350
  ARM and RP2350 Hazard3 RISC-V.
- Added a reusable native firmware CMake helper and build-only link probe that
  verifies `libJaszczurHAL.a` plus ELF, BIN and UF2 generation for every native
  platform.
- Added the HAL-owned native `main()` and explicit core policy: `app_start()`
  and `app_task0()` run on core 0, while `HAL_ENABLE_APP_TASK1` alone launches
  core 1, registers it for multicore lockout and dispatches `app_task1()`.
- Added HAL-only `01_blink` and core-1 entry builds for RP2040, RP2350 ARM
  and RP2350 Hazard3 RISC-V to the local gate and GitHub CI.
- Mapped official Pico SDK board selectors to HAL profiles and added native
  `pico_w`/`pico2_w` built-in LED control through the pinned JaszczurHAL CYW43
  driver without enabling lwIP or the full network backend.
- Shared one RP source inventory across all three RP targets and added artifact
  coverage to the local quality gate and GitHub CI.
- Made RP system metadata ISA/profile-aware, used the Pico SDK portable
  exception-context query and limited Cortex-M HardFault frame capture to ARM.

### Board profiles and runtime capabilities

- Added target-independent profiles for Pico, Pico W, Pico 2, Pico 2 W
  and Pico+PIM730, including compatibility mapping from legacy CYW43 profiles
  and board-owned built-in LED selection.
- Added the public `hal_board` API for declared, available and failed USB,
  CYW43 and external-radio capabilities. The RP CYW43 provider now publishes
  runtime init, failure and deinit transitions.
- Added public network preflight for RP CYW43 profiles: absent hardware returns
  `HAL_EUNSUPPORTED`, inactive hardware returns `HAL_EUNINIT`, and failed
  probe/init returns `HAL_EHW` without further backend or pin access.
- Allowed CYW43 network modules to compile for plain Pico profiles and added
  unit, static-build and CI coverage for Pico, Pico W and Pico+PIM730 runtime
  behavior.
- Fixed native Pico+PIM730 CYW43 initialization after divider 4 at 125 MHz
  exposed a one-bit gSPI sampling shift. Added fixed-point clock calculation,
  timing-program selection, compile-time profile regressions and
  native/compatibility build gates.
- Updated dispatcher profiles and CI to use `JH_RP_BOARD_DEFINES` and exact
  board selectors, including an owned-CYW43 Pico 2 W build.

### Explicit RP2040/RP2350 target model

- Split the former RP2040/RP2350 umbrella target into exact
  `HAL_TARGET_RP2040`, `HAL_TARGET_RP2350_ARM` and
  `HAL_TARGET_RP2350_RISCV` selectors, with `HAL_TARGET_IS_RP` for shared
  backend paths and explicit ARM/RISC-V ISA discriminators.
- Made the Pico SDK platform, chip/ISA, and board selection orthogonal.
  RP2350 RISC-V uses the Hazard3 Pico SDK platform.
- Added compile coverage for explicit and auto-detected targets, invalid
  target/profile combinations, and exact target names. The RP static
  library gate now includes RP2350 ARM/Pico 2 alongside existing RP2040
  profiles.

### Shared implementation layout and independent TLS transports

- Removed the catch-all `impl/shared/compat` source category. Network services
  now live under `impl/shared/network/services`, the public BSD/POSIX adapter
  lives under `impl/shared/network/adapters/bsd`, and shared serial formatting
  lives under `impl/shared/debug`.
- Decoupled the native `hal_tls` client from POSIX descriptors. TLS now
  resolves and connects through `hal_net`/`hal_tcp`, so `HAL_ENABLE_TLS`
  propagates TCP/WiFi without forcing `HAL_ENABLE_BSD_SOCKETS`.
- Preserved BSD sockets as a supported public adapter and retained the
  independent BearSSL-over-BSD transport for clients using descriptor-based
  TLS. Added compile coverage for TLS without BSD, BSD without TLS and runtime
  coverage for the combined TLS-over-BSD path.

### Shared WireGuard and STM32G474 NTP

- Moved the bundled WireGuard protocol, crypto, lwIP netif and lifecycle code
  out of the RP2040-specific framework tree into
  `impl/shared/frameworks/wireguard`. The private client now uses HAL byte-array
  IPv4 values and backend-provided lwIP context, resolver, entropy and time
  hooks; public `hal_wireguard` signatures remain unchanged.
- Added STM32G474 CYW43/lwIP WireGuard support with hardware RNG entropy,
  split/full route installation, default-route restoration and bounded
  peer/timer/PCB teardown. Unsupported offload backends remain rejected by the
  existing capability checks.
- Added asynchronous STM32G474 NTP over public HAL UDP with source/originate
  validation, fractional time-of-day and a newlib `gettimeofday` bridge.
- Included lwIP options before architecture defaults and guarded port macros to
  avoid target `lwipopts.h` redefinition warnings.

### GPIO interrupt core ownership

- Added status-returning GPIO interrupt attach/detach APIs with explicit core
  ownership. RP2040 registration now fails with `HAL_ESTATE` when called from
  the wrong core, rejects cross-core reconfiguration/detach, and atomically
  reserves an unowned pin against concurrent claims.
- Added cross-core diagnostic owner queries, mock core-affinity simulation and
  host coverage for RP2040-style ownership plus STM32G474 single-core/EXTI-line
  behavior. Legacy attach/detach APIs remain compatibility wrappers.
- Documented the separate RP2040 peripheral-IRQ contract: hardware UART RX is
  implicitly bound to the core that calls `hal_uart_begin()`, and GPS inherits
  that affinity when `HAL_GPS_TRANSPORT_UART` is selected. The RP2040
  SoftwareSerial GPS transport uses PIO/DMA and has no CPU IRQ owner.

### Zephyr-informed display driver ports

- Added shared SSD16xx (`SSD1608`, `SSD1673`, `SSD1675A`, `SSD1680`,
  `SSD1681`) and UC81xx (`UC8175`, `UC8176`, `UC8151D`, `UC8179`)
  monochrome EPD drivers over a reusable HAL SPI/GPIO transport with bounded
  BUSY waits, controller reset, full/partial waveform profiles and explicit
  refresh sequencing.
- Integrated both EPD families into `hal_display` as raw `MONO10` backends.
  `frame_incomplete` batches RAM writes for a following full refresh,
  `hal_display_flush_ex()` and `hal_display_epd_refresh_ex()` trigger panel
  updates, and capabilities now expose EPD, MSB-first packing and
  family-specific alignment requirements.
- Added driver/facade host coverage and `examples/55_epd_display` for an
  SSD1681 200x200 raw frame on RP2040 and STM32G474.
- Added GC9A01 round-TFT support to the shared ST77xx-style SPI/GPIO display
  backend. `HAL_ENABLE_GC9A01` now propagates `HAL_ENABLE_TFT`,
  `HAL_ENABLE_DISPLAY` and `HAL_ENABLE_SPI`, and `HAL_DISPLAY_GC9A01` selects
  it through the existing `hal_display` facade.
- Added shared SSD1331 and SSD1351/SSD1357 RGB OLED drivers over HAL SPI/GPIO,
  with Zephyr-derived init, contrast, remap/address-window and RGB565 write
  behavior. The low-level RGB OLED driver currently accepts only native
  orientation for raw RGB565 writes.
- Added a shared ST7567 monochrome LCD driver over HAL I2C or SPI/GPIO, with
  Zephyr-derived power/orientation/contrast setup and page-aligned framebuffer
  writes.
- Added public display capabilities, pixel-format selection and status-returning
  raw area writes. SSD1331/SSD135x now participate in the RGB565 GFX/stream
  facade; ST7567 is exposed as a page-aligned MONO01/MONO10 raw backend.
- Added host coverage in `test_st77xx_driver`, `test_rgb_oled_driver`,
  `test_st7567_driver` and dedicated real-facade dispatch tests.

### Quality gate reliability

- Added profile-specific, deduplicated clang-tidy compile databases so shared
  facade sources are analyzed once even when CMake compiles them under several
  feature configurations. This removes clang-analyzer 18 state leakage and its
  false uninitialized-`va_list` report.
- Reworked ST7567 initialization to keep the required initialized-before-
  contrast sequence explicit without an assignment inside an `if` condition.

### SSD1306-family OLED driver

- Extended the shared OLED backend from SSD1306-only I2C support to an
  SSD1306-family driver covering `SSD1306`, `SSD1309`, `SSD1315`, `SH1106` and
  `CH1115`.
- Added the status-returning `hal_display_init_ssd1306_family_ex()` config
  entry point for controller selection, I2C/SPI transport, segment/page/display
  offsets, hardware orientation and variant current-reference options.
- Added OLED suspend/resume support through `hal_display_suspend_ex()` and
  `hal_display_resume_ex()`, plus driver-level contrast/orientation/power tests
  covering I2C and SPI command/data paths.

### Generic `hal_crc` checksums

- Extracted the Dallas/Maxim CRC-8/CRC-16 routines out of `hal_onewire` into a
  new backend-agnostic `hal_crc` module (`hal_crc8_maxim`, `hal_crc16_maxim`,
  `hal_crc16_maxim_check`), so both the OneWire driver and downstream projects
  can share them without depending on the 1-Wire bus API.
- Added `hal_crc16_ccitt` (CRC-16/CCITT-FALSE) and `hal_crc32` (CRC-32/ISO-HDLC)
  alongside the migrated variants; every routine is a table-free bitwise loop
  and is named after its concrete catalog variant.
- Removed the redundant `hal_onewire_crc8/crc16/check_crc16` public API and the
  duplicate `JHOneWire::crc8/crc16/check_crc16` driver methods; DS18B20 ROM and
  scratchpad validation now call `hal_crc8_maxim` directly.
- `hal_crc` is opt-in via `HAL_ENABLE_CRC` and is auto-enabled by
  `HAL_ENABLE_ONEWIRE`/`HAL_ENABLE_DS18B20`. `tests/test_hal_crc.cpp` pins all
  variants against catalog check values and preserves the former OneWire
  regression vectors.

### UART status-first API

- Completed the `hal_uart` migration to the current status-first rules.
  `hal_uart_begin` and `hal_uart_flush` are now `hal_status_t` in place across
  the RP2040, STM32G474 and mock backends; the redundant `hal_uart_begin_ex` /
  `hal_uart_flush_ex` adapters were removed. The `bool`/value operations keep
  their compatibility signatures with adjacent `_ex` status variants, and
  `hal_uart_create` keeps its handle-returning shape (NULL is its failure
  signal). Existing callers that ignore the return value are unaffected.

### Driver status-first API migrations

- Completed the current-rule status migrations for STMPE610, the simple-I/O
  chips (MCP23017, PCA9654E, PCF8574, 74HC595, MCP3221 and MCP4725), DHT,
  DS18B20 and SD logger.
- STMPE610 now reports register, FIFO data and write failures through
  `hal_status_t` APIs while preserving legacy value/`bool` wrappers where
  applicable.
- The simple-I/O drivers now keep their existing legacy `bool`/value shapes as
  thin compatibility wrappers over adjacent `_ex` status implementations.
- DHT and DS18B20 gained status-returning init/read/sample workflow APIs with
  explicit invalid-argument, uninitialised, busy, timeout/protocol and missing
  sample/device statuses.
- SD logger now reports SD mount, file open/write/flush/close, EEPROM update,
  uninitialised use and buffer-overflow failures through `hal_status_t`, while
  the legacy boolean init calls remain source-compatible wrappers.
- Expanded host mock coverage and docs for the migrated error paths.

### System status-first API

- Completed the `hal_system` migration to the current status-first rules.
  Fallible historical `void` operations for watchdog setup, bootloader entry,
  raw device UID reads and big-endian conversion now return `hal_status_t`
  directly.
- Added status implementations for chip-temperature reads, captured-fault
  retrieval and stack-guard setup. Their historical value/`bool` APIs remain
  thin compatibility wrappers, while missing fault snapshots and unsupported
  STM32 system services now report `HAL_ENOENT` and `HAL_EUNSUPPORTED`
  explicitly.
- Added inline `hal_millis_interval_*` helpers for loop-driven non-blocking
  scheduling over `hal_millis()` without hardware timers: elapsed-check and
  callback-dispatch variants (`hal_millis_interval_elapsed(_now)` and
  `hal_millis_interval_call(_now)`), including wrap-safe `uint32_t` overflow
  semantics and host test coverage.
- Expanded mock and STM32 host tests with success, invalid-argument, missing
  data, unsupported-backend and compatibility-wrapper coverage.

### SoftwareSerial status-first API

- Migrated the complete `hal_swserial` surface to the current status-first
  rules. `create`, pin setters, one-byte reads, writes and `println` now have
  adjacent status implementations while their historical handle/bool/value
  functions remain thin compatibility wrappers. The fallible historical
  `begin` and `flush` functions now return `hal_status_t` directly.
- The owning RP2040 and shared backends now distinguish invalid pins, frame
  formats and baud rates, pool/native-resource exhaustion, post-start pin
  changes, unstarted I/O and empty nonblocking reads. GPS consumes create,
  begin and read statuses and releases a handle when startup fails.
- Expanded host coverage with status success/failure paths, output
  initialisation, legacy-wrapper behaviour and instance-pool exhaustion.

### RP2040 SoftwareSerial PIO/DMA backend

- Replaced the RP2040 shared GPIO/interrupt bit-banger with a native Pico SDK
  backend: PIO state machines perform RX and TX bit timing and DMA moves
  completed RX frames into a circular buffer. This removes the approximately
  one-frame-long GPIO callback at 9600 baud that could delay unrelated RPM and
  CAN processing.
- Kept the legacy `hal_swserial` call shapes source-compatible and retained all
  5-8-bit, parity and stop-bit frame configurations. STM32G474 and mock builds
  continue to use the shared HAL implementation. Each RP2040 handle reserves
  two PIO state machines and one DMA channel.
- Added a source-selection regression test that requires the native PIO
  backend on RP2040 and rejects wrapper implementations, GPIO RX callbacks,
  microsecond bit delays and HAL critical sections in that backend.

### Network transport status refactor

- Re-migrated `hal_wifi`, `hal_net`, `hal_tcp` and `hal_udp` so status-returning
  operations own validation and I/O inside the mock and RP2040 backends. The
  historical bool/count/handle APIs are now thin adjacent compatibility
  wrappers; their sections were removed from `hal_network_status.cpp`.
- Added status variants for WiFi timeout configuration, TCP socket/listener
  allocation, UDP socket allocation, packet parsing and remote-port lookup.
  The network API now distinguishes invalid arguments, invalid/uninitialised
  state, missing DNS/remote data, would-block accept, pool exhaustion, output
  overflow and backend I/O failure.
- Expanded cross-module network status tests with output initialization,
  precise state errors and TCP/UDP pool exhaustion coverage.

### KV and LittleFS status refactor

- Re-migrated `hal_kv` and `hal_littlefs` so status-returning functions own
  validation and storage I/O in the shared KV implementation and each
  LittleFS backend. Historical bool/value functions are thin adjacent
  compatibility wrappers; the fallible historical `void` functions for KV
  auto-commit and LittleFS callback/unmount now return `hal_status_t` directly.
- Removed the superseded `hal_kv_status.cpp` and `hal_littlefs_status.cpp`
  adapters. KV now propagates EEPROM read, write and commit errors and
  distinguishes uninitialised, invalid-range, capacity, missing-key and
  caller-buffer errors. LittleFS distinguishes invalid arguments, unmounted
  state, missing paths, invalid STM32 partition configuration and backend I/O.
- Expanded storage tests with direct status calls, output initialization,
  uninitialised/range/capacity behavior and the updated callback/unmount API.

### RTC status API

- Reworked RTC to the revised status-first migration pattern: mock, RP2040 and
  STM32G474 backends now own their `_ex` validation and I/O results directly,
  while the legacy handle/bool functions remain thin compatibility wrappers.
  Infallible `hal_rtc_deinit()` stays `void` and has no artificial status
  companion.
- Removed the superseded `hal_rtc_status.cpp` adapter and its build entries.
  RTC status calls now distinguish invalid arguments/configuration
  (`HAL_EINVAL`), pool or mutex exhaustion (`HAL_ENOMEM`), unsupported chips or
  chip features (`HAL_EUNSUPPORTED`), epoch conversion outside 1970..2099
  (`HAL_EOVERFLOW`) and backend/bus failures (`HAL_EIO`). I2C bus init status is
  propagated instead of being collapsed.
- Expanded `test_hal_rtc` with direct status coverage for success paths,
  invalid arguments/configuration, pool exhaustion, unsupported features and
  epoch overflow.

### EEPROM status refactor (reference pattern for the revised migration)

- Reworked `hal_eeprom` as the reference pattern for the revised status
  direction: instead of adding parallel `_ex`
  wrappers over the legacy `void` API, the historically `void` entry points
  (`init`, `set_progress_callback`, `write_byte`, `write_int`, `write_bytes`,
  `read_bytes`, `commit`, `reset`) now **return `hal_status_t` directly**. This
  is source-compatible: existing callers that ignore the return value are
  unaffected. The value-returning getters keep their signature and gain
  `_ex` companions with an output parameter: `hal_eeprom_read_byte_ex`,
  `hal_eeprom_read_int_ex`, `hal_eeprom_size_ex`.
- The status logic now lives **in each backend** (mock, RP2040, STM32G474)
  rather than in a shared adapter, so it can report native failures: the
  AT24C256 path now surfaces I2C errors as `HAL_EIO` and the STM32 flash commit
  surfaces failed flash writes as `HAL_EIO` - outcomes the old `void` API
  discarded. Out-of-range access still clips exactly as before and is reported
  as `HAL_EOVERFLOW`; access before init is `HAL_EUNINIT`; a NULL buffer is
  `HAL_EINVAL`. The additive `hal_eeprom_status.cpp` adapter was removed.
- Expanded `test_hal_eeprom` with byte/int round-trip, range/overflow,
  uninitialised and NULL-argument status coverage.

### Storage status API

- Added status-returning `_ex` APIs for `hal_kv`
  (init/get/set/blob/delete/gc/stats/commit) and `hal_littlefs`
  (begin/end/format/exists/remove/byte counts) while preserving every existing
  `bool`/`int`/`void` compatibility function. (`hal_eeprom` was subsequently
  reworked in place - see the EEPROM refactor entry above.)
- Initially added `hal_kv_status.cpp` and `hal_littlefs_status.cpp` as backend-agnostic
  validation/result adapters shared by mock, RP2040 and STM32G474 builds. They
  surface distinct codes for invalid arguments (`HAL_EINVAL`), read misses
  (`HAL_ENOENT`), a caller buffer too small for a stored KV blob
  (`HAL_EOVERFLOW`), an uninitialised/unmounted backend (`HAL_EUNINIT`) and
  backend I/O failure (`HAL_EIO`). Reads and byte-count queries expose their
  result through an output parameter. Both adapters were subsequently removed
  by the KV/LittleFS refactor described above.
- Expanded `test_hal_kv` and `test_hal_littlefs` with success,
  invalid-argument, overflow, not-found and uninitialised coverage for the new
  APIs.

### Display status API

- Re-migrated the complete display module to the revised status-first pattern.
  Status validation and error mapping now live as explicitly named functions
  inside the mock and shared RP2040/STM32G474 backend sources; every historical
  `bool` entry point is a thin adjacent wrapper. The superseded
  backend-agnostic `hal_display_status.cpp` adapter was removed; the migration
  uses no preprocessor symbol aliases, injected `.inc` implementation or
  separate compatibility translation unit.
- Changed the historical `void hal_display_init()` and
  `void hal_display_soft_init()` functions in place to return `hal_status_t`;
  their redundant additive `_ex` variants were removed. Existing callers may
  continue ignoring the result. Value-returning dimension/text helpers retain
  their original shape and their status `_ex` output-parameter companions.
- Display operations now preserve distinct backend-owned results for invalid
  arguments (`HAL_EINVAL`), unconfigured state (`HAL_EUNINIT`), unsupported
  bitmap/stream paths (`HAL_EUNSUPPORTED`), missing stream (`HAL_ESTATE`), an
  already-open stream or active async DMA (`HAL_EBUSY`), formatting truncation
  (`HAL_EOVERFLOW`) and panel/bus failure (`HAL_EIO`).
- Because the historical bus-selecting initialiser already occupies
  `hal_display_init_ssd1306_i2c_ex()`, the SSD1306 status entry point is
  `hal_display_init_ssd1306_i2c_status_ex()` (mirrors `hal_wifi_ping_status_ex()`).
- Completed the mock display backend with the async DMA write trio
  (`_async_start`/`_async_busy`/`_async_wait`) so the full public surface is
  host-testable, added deterministic display I/O failure injection, and
  expanded `test_hal_display` with success, invalid-argument, uninitialised,
  busy/stream-state, overflow and backend-I/O coverage.

### SPI/DMA status API

- Re-migrated SPI to the revised backend-owned status-first pattern. Historical
  fallible `void` operations (`init`, begin/end transaction, buffer transfer
  and write) now return `hal_status_t` in place; byte/word and DMA APIs retain
  their value/`bool` compatibility wrappers over adjacent `_ex` functions.
- Removed the superseded `hal_spi_status.cpp` adapter. Mock, RP2040 and
  STM32G474 now validate and map results in their owning backend: active RP2040
  DMA reports `HAL_EBUSY`, short Pico SDK transfers report `HAL_EIO`, and
  STM32G474 polling expiration reports `HAL_ETIMEOUT`.
- Expanded `test_hal_spi` with success, invalid-argument, zero-length buffer,
  compatibility-wrapper and injected DMA failure coverage.

### RGB LED status API

- Migrated `hal_rgb_led` to backend-owned status-first behavior. Historical
  init, explicit-type init, colour and off operations now return
  `hal_status_t` in place; `hal_rgb_led_init_ex()` retains its established name
  because `_ex` already identifies the pixel-type overload.
- Mock, RP2040 and STM32G474 now distinguish invalid configuration
  (`HAL_EINVAL`), use before init (`HAL_EUNINIT`), allocation/transport-resource
  exhaustion (`HAL_ENOMEM`) and pixel transport failure (`HAL_EIO`). Failed
  writes no longer poison the repeated-colour cache, so callers can retry.
- Expanded `test_hal_rgb_led` with invalid configuration, pre-init access,
  injected init/write failures and successful retry coverage.

### PGA2311 status API

- Added direct status companions for PGA2311 init, gain/raw setters, mute and
  gain-code conversions. Historical handle/`bool` functions remain adjacent
  compatibility wrappers using `hal_status_to_bool(...)` where applicable;
  cached getters, the mute predicate and infallible cleanup retain their shape.
- The shared PGA2311 SPI driver now propagates transaction/write/completion
  errors and always restores CS, transaction and lock state. Failed gain writes
  no longer alter the cached target, so retry and later software-unmute restore
  the last successfully accepted value.
- Expanded `test_hal_pga2311` with status validation, static-pool exhaustion,
  injected SPI failure, resource cleanup and successful retry coverage.

### Pulse-counter status API

- Completed the existing backend-owned `hal_pcnt` migration. Historical
  `void hal_pcnt_reset()` now returns `hal_status_t` directly and the redundant
  `hal_pcnt_reset_ex()` entry point was removed. Existing callers that ignore
  the reset result remain source-compatible.
- Kept `hal_pcnt_init()` as a thin `bool` wrapper over backend-local
  `hal_pcnt_init_ex()`, while count-returning read helpers retain their `_ex`
  output-parameter companions. Predicates and channel-count queries remain
  value-returning state queries.
- STM32G474 init now honestly rejects pins other than its fixed TIM2_CH1/PA0
  input. Tests and API documentation cover direct reset statuses, invalid
  arguments and uninitialised channels.

### Network status API

- Added status-returning APIs for WiFi, IPv4 resolution, TCP, UDP, MQTT and
  WireGuard while preserving all legacy `bool`, integer and handle-returning
  functions.
- Added explicit output parameters for ping/scan results, transferred byte
  counts, accepted TCP sockets and WireGuard peer state. The new ping entry is
  `hal_wifi_ping_status_ex()` because the historical int-returning
  `hal_wifi_ping_ex()` remains source-compatible.
- Added `hal_network_status.cpp` as a shared validation/result adapter and a
  cross-module `test_hal_network_status` covering success and representative
  `HAL_EINVAL`, `HAL_ENOENT`, `HAL_EUNINIT` and `HAL_EIO` paths.

### Multi-target VS Code firmware workflow

- Added the shared `cmake/jh_firmware_project` dispatcher and target recipes so
  VS Code firmware projects can build through one CMake entry for RP2040/RP2350
  and STM32G474.
- Fixed the generated Fiesta entry adapter so RP2040 executes
  `initialization1()` from the generated core-1 entry. This restores the
  core-1 affinity of GPIO/IRQ initialization
  without changing initialization order on other targets; a host regression
  test now verifies the generated RP2040 adapter's call mapping.
- Added the `vscode/targets/` registry plus `jh-vscode select-board`, with
  target/board selection stored in the gitignored
  `.vscode/jaszczurhal.local.json` user-local file.
- Updated the VS Code project generator to create dispatcher-backed,
  target-selectable projects with `--target`/`--board`, target-neutral tasks,
  GUI/terminal board selection, and no project-local firmware CMake recipe.
- Migrated all checked-in `examples/01_*` through `examples/53_*` directories
  to dispatcher-backed VS Code projects with per-example manifests, board
  selection tasks, and a manifest-driven examples quality-gate runner.
- Added OpenOCD upload support for STM32G474, RP2040-only UF2/BOOTSEL gating,
  target/board-specific CMake build directories, and friendlier diagnostics for
  missing target backends or firmware images that do not fit the selected MCU.

### Security supply chain

- Added `SECURITY.md` with vulnerability reporting, triage, CVSS guidance and
  third-party maintenance policy.
- Added a human-maintained third-party inventory in `security/third_party.json`
  and a generated CycloneDX SBOM at `security/sbom.cdx.json`.
- Added `scripts/generate_sbom.py` for deterministic offline SBOM generation,
  `scripts/check_sbom.sh` for SBOM freshness checks,
  `scripts/check_vulnerabilities.sh` as an optional scanner wrapper, and
  `security/vulnerability_log.md` for CVE/CVSS assessment decisions.
- Documented the SBOM and vulnerability-tracking workflow in
  `doc/security_supply_chain.md`.
- Added a dedicated GitHub Actions `security-scan` job for PR/push, weekly and
  manual SBOM/vulnerability checks.

### Simple I/O chip drivers

- Added shared HAL-only drivers for MCP23017, PCA9654E, PCF8574, 74HC595,
  MCP3221 and MCP4725 under `src/hal/impl/shared/drivers/simple_io/`, with
  public `hal_mcp23017.h`, `hal_pca9654e.h`, `hal_pcf8574.h`, `hal_hc595.h`,
  `hal_mcp3221.h` and `hal_mcp4725.h`.
- Ported the working grblHAL plugin transaction flows by Terje Io onto
  JaszczurHAL I2C/SPI/GPIO/timing/sync primitives and exposed new
  `hal_status_t`-based `_ex` APIs while keeping simple `bool` wrappers.
- Added per-instance mutex protection for multicore/FreeRTOS-safe runtime use,
  host coverage in `test_simple_io_drivers`, and
  `examples/53_simple_io_chips` for RP2040 and STM32G474.

### ADP5360 PMIC driver

- Added `HAL_ENABLE_ADP5360`, public `hal_adp5360.h` and a shared HAL-only
  ADP5360 PMIC driver under `src/hal/impl/shared/drivers/adp5360/` for RP2040,
  STM32G474 and mock builds.
- Ported the Zephyr ADP5360 MFD, charger, fuel-gauge and regulator register
  flows from the Analog Devices/Nordic Apache-2.0 drivers onto JaszczurHAL
  I2C/GPIO/timing/sync primitives, with `hal_status_t` error mapping and
  per-device mutex protection via `jh_hal_mutex_create_once()`.
- Added host coverage in `test_adp5360_driver`, API documentation and
  `examples/54_adp5360_pmic` using `debugInit()`, `deb()` and `derr()`.

### Display raw buffer foundation

- Added `hal_display_pixel_format_t` and `hal_display_buffer_desc_t` to
  describe low-level display buffers with pixel format, pitch, width, height,
  buffer size and frame-continuation hints ahead of the planned raw area-write
  API.

### Status-returning API expansion

- Added `hal_digipot_init_ex()` and `hal_digipot_set_resistance_ex()` with
  `hal_status_t` diagnostics while keeping `hal_digipot_init()` and
  `hal_digipot_set_resistance()` as compatibility wrappers.
- Propagated status codes through the shared MCP401x/MAX5395 digipot backends
  for invalid configuration/range, pool exhaustion, I2C bus failures and
  MCP401x read-back mismatch reporting.
- Migrated DAC writes to the status-first layout: `hal_dac_write()` and
  `hal_dac_write_millivolts()` now return `hal_status_t` in place, and their
  redundant `_ex` variants were removed. `hal_dac_init_ex()` remains the
  status implementation beside the historical `bool hal_dac_init()` wrapper.
- Added status diagnostics for PCNT init/read/read-and-reset. The reset API was
  subsequently migrated in place; see "Pulse-counter status API" above.
- Added `hal_get_device_uid_hex_ex()` so callers can distinguish NULL-buffer
  (`HAL_EINVAL`) and too-small-buffer (`HAL_EOVERFLOW`) failures while keeping
  the existing `hal_get_device_uid_hex()` wrapper.
- Re-migrated I2C master APIs to the status-first backend layout. Init, clock
  and bus-clear operations now return `hal_status_t` in place and their six
  redundant `_ex` variants were removed. Historical value/`bool` APIs remain
  compatibility wrappers over adjacent status companions.
- Mock, RP2040 and STM32G474 now own validation, I/O and error mapping in their
  status paths; compatibility wrappers no longer serve as the implementation
  called by status APIs. Backends distinguish invalid arguments,
  uninitialized use, bus errors and RP2040 timeouts where available.
- Moved the legacy `i2cScanner()` utility into `hal_i2c` as status-first
  `hal_i2c_scan()` / `hal_i2c_scan_bus()`. The new scanner performs one bounded
  pass over non-reserved addresses, returns discovered addresses without a
  serial dependency, supports count-only scans and explicit overflow
  reporting, and accepts a per-probe `void(void)` callback so
  `hal_watchdog_feed` can be passed directly. Removed the `I2C_SCANNER` flag.

### HTTP server

- Added opt-in `HAL_ENABLE_HTTP_SERVER`, public `hal_http_server.h` and a
  small poll-driven HTTP/1.1 server implemented over the handle-based
  `hal_tcp` listener/socket API.
- The server supports exact route registration for GET/HEAD/POST/PUT/DELETE
  and OPTIONS, query/body exposure, buffered responses with automatic
  `Content-Length`, content-type/status helpers and close-after-response
  semantics.
- HTTP requests now expose parsed headers to handlers through
  `hal_http_header_t`, `request.headers`, `request.header_count` and
  `hal_http_request_get_header()`, and responses can add arbitrary headers via
  `hal_http_response_set_header()`.
- Added prefix route registration with `hal_http_server_route_prefix()` so
  modules can mount path subtrees such as static file roots.
- HTTP route, start, handler and response helper APIs return `hal_status_t`
  result codes instead of collapsing failures to `bool`.
- Added host coverage in `test_hal_http_server` and `examples/48_http_server`
  for a Pico W style HTML/JSON status endpoint.

### HTTP files

- Added opt-in `HAL_ENABLE_HTTP_FILES`, public `hal_http_files.h` and a
  callback-backed file serving/upload adapter over `hal_http_server`.
- The adapter supports GET/HEAD prefix-mounted file serving, extension-based
  content types, weak ETag generation, `If-None-Match`, raw PUT uploads,
  multipart/form-data POST uploads and path traversal rejection.
- Added host coverage in `test_hal_http_files` and a RAM-backed Pico W example
  in `examples/52_http_files`; real firmware can replace the RAM callbacks
  with LittleFS, FatFs/SD or flash-asset callbacks.

### WebSocket server

- Added opt-in `HAL_ENABLE_WEBSOCKET`, public `hal_websocket.h` and a small
  poll-driven WebSocket server over the handle-based `hal_tcp` listener/socket
  API.
- The server performs the HTTP Upgrade handshake, parses masked single-frame
  text/binary client messages, handles ping/pong and close frames, and exposes
  connect/message/disconnect callbacks plus per-client send and broadcast
  helpers.
- WebSocket start, callback, send, broadcast and close APIs return
  `hal_status_t`; broadcast helpers report the sent-client count through an
  optional output pointer.
- Added host coverage in `test_hal_websocket` and `examples/49_websocket` for
  a Pico W style HTTP page with a live WebSocket telemetry/echo channel.

### Net console

- Added opt-in `HAL_ENABLE_NET_CONSOLE`, public `hal_net_console.h` and a
  password-protected TCP console transport over `hal_tcp`.
- `hal_serial`/`deb`/`derr` output still goes to the normal UART/USB debug
  path and is additionally mirrored to authenticated TCP clients.
- Added bidirectional command input through a line callback and polling RX API,
  per-client/broadcast write helpers using `hal_status_t`, host coverage in
  `test_hal_net_console` and `examples/50_net_console`.

### Network commands

- Added opt-in `HAL_ENABLE_NET_COMMANDS`, public `hal_net_commands.h` and a
  shared JSON/text command dispatcher for HTTP and WebSocket control channels.
- Commands are registered by name and receive source-aware callbacks with
  plain-text args or cJSON-backed `cmd`/`args` payloads, plus fixed-buffer
  response helpers returning `hal_status_t`.
- Added HTTP route and WebSocket message helpers, structured default success
  and error responses, host coverage in `test_hal_net_commands` and
  `examples/51_net_commands`.

### HAL status codes

- Added public `hal_status_t` in `hal_status.h` as a shared status vocabulary
  for new APIs (`HAL_OK`, `HAL_EINVAL`, `HAL_EBUSY`, `HAL_ETIMEOUT`,
  `HAL_EIO`, `HAL_EUNSUPPORTED`, `HAL_ENOENT`, `HAL_EAGAIN`) without changing
  existing module return contracts.
- Added `hal_status_to_string()` for stable symbolic status names in logs and
  diagnostics.
- Added small status helper functions (`hal_status_is_ok()`,
  `hal_status_is_error()`, `hal_status_from_bool()` and
  `hal_status_to_bool()`) so legacy `bool` wrappers and new `_ex` APIs use one
  shared conversion pattern.
- Added `hal_status_t`-returning `_ex` APIs for BH1750, TSC2007 and STMPE610
  sensor/input drivers while keeping the existing `bool`, `float` and `void`
  compatibility wrappers unchanged.

### MFRC522 - shared RFID reader driver

- Moved the MFRC522 RFID reader driver into
  `src/hal/impl/shared/drivers/mfrc522/` with public `hal_mfrc522.h` and
  opt-in `HAL_ENABLE_MFRC522`, making the driver usable from RP2040,
  STM32G474 and mock builds.
- Ported the MFRC522-spi-i2c-uart-async / Miguel Balboa protocol logic to
  JaszczurHAL SPI, I2C, GPIO, timing and mutex primitives, with
  `StatusCodeToHalStatus()` mapping driver outcomes to `hal_status_t`.
- Added `examples/46_mfrc522_rfid`, module/API docs and host coverage in
  `test_mfrc522_driver`; removed the old imported driver folder.

### PN532 - shared NFC/RFID reader driver

- Added a shared PN532 driver in `src/hal/impl/shared/drivers/pn532/` with
  public `hal_pn532.h` and opt-in `HAL_ENABLE_PN532` for RP2040, STM32G474
  and mock builds.
- Ported the Adafruit_PN532 command framing, ACK handling and core card
  commands to JaszczurHAL SPI/I2C/UART transports, GPIO, timing and mutex
  primitives, returning `hal_status_t` from the new API surface.
- Added `examples/47_pn532_nfc`, API/docs entries and host coverage in
  `test_pn532_driver`; removed the old imported Adafruit_PN532 driver folder.

### RP2040 backend - native Pico SDK migration

- Migrated the RP2040 `hal_uart` backend to the native Pico SDK hardware UART
  and removed the former `Serial1`/`SerialPIO` path.
- Moved `hal_swserial` to a shared HAL GPIO/timing/sync implementation used by
  RP2040, STM32G474 and mock builds. The driver preserves the Serial-over-PIO
  frame handling model, adds per-instance locking, critical-section-protected
  bit timing, host coverage for GPIO framing, and `examples/45_swserial_loopback`.
- Migrated `hal_system` to direct Pico SDK primitives: `hal_millis()`
  now uses `to_ms_since_boot(get_absolute_time())`, `hal_micros()`/
  `hal_micros64()` use `time_us_64()`, and `hal_delay_ms()` selects pico
  `sleep_ms()` (interrupts enabled) or `busy_wait_ms()` (ISR / HAL-critical
  context). The RP2040 assert path
  (`hal_assert_fail`) now writes through the native serial backend instead of
  `Serial.print()`.
- Ported the RP2040 `hal_adc` and `hal_pwm` backends to native `hardware/adc.h`
  and `hardware/pwm.h` (no `analogRead`/`analogWrite`). ADC default resolution
  is 12 bits (consistent with STM32G474/mock), with the 12-bit hardware sample
  rescaled to the configured resolution; PWM uses hardware slices with an
  approximately 1 kHz best-effort default frequency derived from `clk_sys`.
- Made the optional RP network frameworks opt-in at build time: PubSubClient
  compiles only under `HAL_ENABLE_MQTT`, and WireGuard compiles only under
  `HAL_ENABLE_WIREGUARD`.
- Replaced the last RP2040 SoC system/fault wrapper dependencies:
  `rp2040_system_get_free_heap()` now derives free heap from the
  `__StackLimit`/`__bss_end__` linker symbols minus `mallinfo().uordblks`,
  `rp2040_system_read_chip_temp()` reads the on-die sensor over the native ADC
  (`ADC_TEMPERATURE_CHANNEL_NUM`), and `rp2040_fault` decodes the reset reason
  straight from the watchdog reason and chip-reset registers
  (`VREG_AND_CHIP_RESET` on RP2040, `POWMAN` on RP2350) - no
  `rp2040.getFreeHeap()`, `analogReadTemp()` or `rp2040.getResetReason()` in the
  SoC drivers.
- Tightened shared-layer header hygiene: the bundled `JPEGDecoder` is now a
  target-neutral, memory-only decoder (dropped the platform-specific
  ESP/SD/LittleFS file backends and kept the `decodeArray()` path), and removed
  the residual platform includes from
  `hal_bits.h` and the RP2040 `hal_sync` backend.

## [1.8.0] - 2026-07-01

### hal_dht - shared DHT11/DHT22 GPIO driver

- Added opt-in `HAL_ENABLE_DHT`, public `hal_dht.h`, and a shared
  `impl/shared/drivers/dht/hal_dht.cpp` backend for DHT11/DHT22 temperature and
  humidity sensors on RP2040, STM32G474 and mock builds.
- Ported the Bonezegei DHT timing/readout flow to HAL GPIO, timing,
  critical-section and mutex primitives, including per-handle locking and a
  singleton pool mutex created with `jh_hal_mutex_create_once`.
- Added host coverage in `test_hal_dht` for successful frames, checksum
  failures, response failures, cached sample getters and critical-section
  restoration.
- Added `examples/43_dht_temperature_humidity`, feature/API docs, module flag
  docs and credits; removed the old RP2040-local Bonezegei DHT driver folders.

### hal_dacless - shared DACless PWM-audio driver

- Moved the DACless driver into `src/hal/impl/shared/drivers/dacless/` with
  `HAL_ENABLE_DACLESS` and public `hal_dacless.h`, making it available to
  RP2040, STM32G474 and mock builds through JaszczurHAL DMA/PWM-freq, ADC,
  timing and synchronization primitives.
- Added `HAL_ENABLE_DMA_PWM_AUDIO` and `hal_dma_pwm_audio.h` as a narrow timer-paced
  PWM-audio DMA helper. The RP2040 backend preserves the original DACless
  chained PWM DMA channel A/B flow and ADC sample/control DMA refresh; the
  STM32G474 backend provides the analogous TIM update DMA into CCR with
  circular half/full callbacks and ADC1 circular DMA scan.
- Preserved the DACless configuration shape, double-buffered block flow,
  sample/block callbacks, ADC result buffer, compatibility globals and RP2040
  blend-fraction interpolation helper behavior, with attribution to the
  original Brian Varren / Brian Sullivan code in the shared driver source.
- Added per-instance HAL mutex protection with `jh_hal_mutex_create_once`,
  DMA-driven default operation plus `cfg.useDma=false` cooperative `service()`
  processing for normal task/core context, and `hal_pwm_freq_stop()` so
  `mute()` can stop polling PWM output without destroying the channel.
- Added `examples/44_dacless_audio` plus a polling build variant, API/module
  docs, README credits and host regression coverage in
  `test_hal_dma_pwm_audio` and `test_dacless_driver`.
- The shared driver no longer depends on the old RP2040-local
  `impl/rp2040/drivers/DACless` import.

### hal_udp - handle-based UDP sockets

- Added `hal_udp_socket_t` and the handle-based
  `hal_udp_socket_open/bind/sendto/recvfrom/close` API using shared
  `hal_net_endpoint_t` IPv4 endpoints.
- Reworked the mock UDP backend to model multiple independent sockets with
  separate bind ports, RX queues and TX captures.
- Reworked the RP2040 UDP backend from one global `WiFiUDP` instance to a
  static `WiFiUDP` pool sized by `HAL_UDP_SOCKET_MAX_INSTANCES`.
- Kept the legacy `hal_udp_*` API source-compatible as a default-socket wrapper,
  including explicit-host `hal_udp_begin_packet(...)` behavior.

### hal_tcp - TCP sockets and listeners

- Added opt-in `HAL_ENABLE_TCP`, public `hal_tcp.h`, and the handle-based
  `hal_tcp_socket_open/connect/send/recv/is_connected/shutdown/close` API using
  shared `hal_net_endpoint_t` IPv4 endpoints.
- Added a deterministic mock TCP client backend with scripted connect result,
  injected RX bytes, captured TX payload and closed-socket error coverage.
- Added an RP2040 TCP backend backed by a static socket pool, with
  connect/receive timeout handling and serialized access through the HAL mutex
  layer.
- Added `hal_tcp_listener_t` and
  `hal_tcp_listener_open/bind/listen/accept/close` for inbound TCP servers.
- Added mock listener queues with backlog coverage and RP2040 lwIP listener
  support; accepted sockets remain independent when the listener is closed.

### BSD sockets compatibility adapter

- Added opt-in `HAL_ENABLE_BSD_SOCKETS`, which propagates UDP/TCP/WiFi and
  provides minimal IPv4 compatibility headers:
  `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `fcntl.h`,
  `sys/select.h` and `unistd.h`.
- Added fd-table based mapping from POSIX-style `int` descriptors to
  `hal_udp_socket_t`, `hal_tcp_socket_t` and `hal_tcp_listener_t`.
- Implemented MVP calls for `socket`, `bind`, `listen`, `accept`, `connect`,
  `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `close`, `read`, `write`
  plus byte-order and IPv4 text/binary helpers.
- Added minimal nonblocking support with `fcntl(F_SETFL, O_NONBLOCK)`,
  `MSG_DONTWAIT` for send/receive calls and `select()` read/write readiness
  over HAL socket descriptors.
- Added minimal `netdb.h` support with `getaddrinfo()`, `freeaddrinfo()` and
  `gai_strerror()` for IPv4 hostname/literal resolution through the shared
  HAL resolver contract.
- Added `hal_net_resolve_ipv4()` plus mock DNS entries and an RP2040
  `WiFi.hostByName()` backend path so simple TCP/UDP clients can connect by
  hostname while keeping HAL transport endpoints numeric.
- Added basic `setsockopt()` compatibility for `SOL_SOCKET` +
  `SO_REUSEADDR`/`SO_REUSEPORT`, with unsupported options reported through
  `ENOPROTOOPT`.
- Extended socket option and endpoint compatibility with `getsockopt(SO_ERROR)`,
  `getsockname()`, `getpeername()`, `SO_RCVTIMEO` and `SO_SNDTIMEO`.
- Added connected UDP compatibility: `connect()` on `SOCK_DGRAM` records a
  default peer, then `send()`/`write()` and `recv()`/`read()` work on that
  peer without explicit `sendto()`/`recvfrom()` addresses.
- Documented and tested non-blocking TCP `connect()` as an immediate
  best-effort attempt rather than a full pending-connect state machine; added
  listener readiness coverage for `select()`.
- Updated BSD sockets TCP/UDP client examples to resolve
  `BSD_EXAMPLE_SERVER_HOST` with `getaddrinfo()`, while preserving
  `BSD_EXAMPLE_SERVER_IP` as the default host alias.
- Added host/mock coverage for sockaddr translation, errno paths, TCP/UDP data
  flow, hostname resolution, `EAI_*` paths, `setsockopt()` and a C compile
  smoke test for simple TCP/UDP client/server shapes.

### hal_serial - native RP2040 CDC and streamed debug output

- Reworked the RP2040 `hal_serial` backend to write through the native
  TinyUSB CDC transport owned by the RP2040 USB stack, while keeping the public
  `hal_serial_*`, `hal_deb`, `hal_derr` and `hal_derr_limited` API unchanged.
- `hal_serial_set_flush(false)` is now the RP2040 default. The backend still
  serializes every emitter with the shared TX mutex and kicks the CDC FIFO in
  the write loop; enabling flush adds an extra TinyUSB CDC flush/task poll before
  releasing the mutex.
- Replaced task-context `hal_deb()`, `hal_derr()` and full-message
  `hal_derr_limited()` formatting buffers with a shared streaming formatter.
  `HAL_DEBUG_BUF_SIZE` no longer caps normal debug/error log length; it remains
  a legacy sizing knob for mock capture/RX helpers. ISR-deferred records remain
  intentionally bounded by `HAL_DEBUG_ISR_TEXT_MAX`.
- Updated RP2040, STM32G474 and mock serial backends to emit prefixes,
  timestamps, source tags and formatted payload fragments under the same TX
  mutex, preserving line-level serialization without whole-line staging buffers.
- Added host coverage for the shared debug formatter, including output longer
  than `HAL_DEBUG_BUF_SIZE` and common printf-style conversions.
- Refreshed the serial/debug API documentation, build/test dependency notes and
  feature overview to describe TinyUSB CDC, optional flush semantics and streamed
  debug formatting.

### hal_can - backend-selectable CAN API

- Changed the public CAN creation API to use `hal_can_config_t`, with
  `HAL_CAN_BACKEND_MCP2515` as the first backend selector and compatibility
  helpers kept around the existing `hal_can_send()` / `hal_can_receive()`
  classic 8-byte frame surface.
- Split compile-time flags so `HAL_ENABLE_CAN` is now the generic CAN facade
  and `HAL_ENABLE_MCP2515` is the backend flag that propagates both
  `HAL_ENABLE_CAN` and `HAL_ENABLE_SPI`.
- Moved MCP2515 defaults into the MCP2515 backend area and kept
  `hal_can_util.cpp` for backend-neutral helpers such as retry creation,
  queue draining and small payload encoding.
- Refactored RP2040 and STM32G474 `hal_can.cpp` to stay as the HAL facade and
  dispatch through shared MCP2515 backend operations under
  `impl/shared/drivers/mcp2515/hal_can_mcp2515.*`.
- Added the `HAL_ENABLE_MCP251XFD` backend flag, `HAL_CAN_BACKEND_MCP251XFD`
  selector and MCP2517FD/MCP2518FD polling backend under
  `impl/shared/drivers/mcp251xfd/`, preserving the existing classic CAN API while
  enabling CAN FD through `hal_can_frame_t`.
- Added the STM32G474-only `HAL_ENABLE_STM32G474_FDCAN` backend flag,
  `HAL_CAN_BACKEND_STM32G474_FDCAN` selector and native FDCAN1 register backend
  with fixed message RAM layout, classic/FD frame TX/RX, mode, diagnostics and
  filter support based on the Zephyr STM32 FDCAN driver model.
- Updated the MCP2515 CAN example, added a STM32G474 native FDCAN example, and
  refreshed module flag documentation, CAN API docs and host tests for
  backend-selection config.

### hal_littlefs - STM32G474 internal-flash backend

- Added a real STM32G474 `hal_littlefs` backend using upstream littlefs v2 and
  a dedicated internal-flash partition before the existing EEPROM/KV pages.
- Added linker symbols and validation for `HAL_STM32_FLASH_LITTLEFS_SIZE`.
  STM32 CMake builds reserve 64 KB automatically when `HAL_ENABLE_LITTLEFS` is
  enabled through their define lists and no explicit size is provided.
- Extracted common STM32G474 flash erase/program helpers so EEPROM/KV and
  LittleFS use the same register-level flash primitive layer.
- Enabled `examples/16_littlefs` for both RP2040 and STM32G474 and documented
  the STM32 partitioning requirements.

### build - RP2040 static-library paths

- Standardized the default RP2040 static-library build directory as
  `build_rp2040/` in the helper script, full test runner and build
  documentation.
- Consolidated RP static-library CMake glue under `rp_native_lib/` and aligned
  dependency pins with the official Pico SDK toolchain.

### hal_irsmall_decoder - shared IR receiver decoder

- Added `HAL_ENABLE_IRSMALL_DECODER` with public `hal_irsmall_decoder_*` API
  and a shared HAL implementation for RP2040, STM32G474 and host tests.
- Preserved the IRsmallDecoder protocol behavior for NEC, NEC extended, RC5,
  Sony SIRC 12/15/20-bit, Sony SIRC triple-frame, Samsung 20-bit and Samsung
  32-bit receivers, including timing thresholds, repeat suppression and
  `key_held` reporting.
- Replaced the shared RC5 frame decoder with the transition-table logic from
  the existing RP2040-tested `RC5` driver while keeping the shared
  `key_held` reporting semantics.
- Public driver calls serialize each decoder instance with a HAL mutex created
  through the shared create-once helper; ISR-shared timeout/reset paths use
  short critical sections for multicore/FreeRTOS-safe access.
- Added `hal_gpio_detach_interrupt()` for GPIO interrupt users and covered it
  in the mock GPIO tests.
- Added `examples/34_irsmall_decoder`, module/API docs, origin license context
  in the shared driver folder and host regression coverage in
  `test_irsmall_decoder_driver`.
- Removed the old `src/hal/impl/rp2040/drivers/IRsmallDecoder` import after
  moving the driver behavior into the shared HAL implementation.

### hal_stmpe610 - shared STMPE610 resistive touch driver

- Added `HAL_ENABLE_STMPE610` with public `hal_stmpe610_*` API and a shared
  HAL implementation for RP2040, STM32G474 and host tests.
- Preserved the source STMPE610 behavior: chip-ID probe, hardware-SPI mode-1
  fallback, reset/setup register sequence, FIFO data decode, touch status,
  buffer-size helpers and interrupt-status clear after FIFO drain.
- Public driver calls serialize each controller instance with a HAL mutex
  created through the shared create-once helper for multicore/FreeRTOS-safe
  first use; hardware SPI transactions also lock the HAL SPI bus.
- Added I2C, hardware-SPI and soft-SPI transport coverage in
  `test_stmpe610_driver`, plus `examples/33_stmpe610_touch`, module/API docs,
  and origin license context in the shared driver folder.
- Removed the old `src/hal/impl/rp2040/drivers/Adafruit_STMPE610` import after
  moving the driver behavior into the shared HAL implementation.

### examples - debug logging cleanup

- Refactored examples to initialise the debug backend through `debugInit()` and
  print console text through the `deb`/`derr` macros from tools.
- Set `HAL_DEBUG_DEFAULT_BAUD` to 115200 in example configs so the new
  `debugInit()` calls preserve the previous console baud rate.

### hal_hd44780 - shared HD44780 character LCD driver

- Added `HAL_ENABLE_HD44780` with a shared `HD44780` class under
  `src/hal/impl/shared/drivers/hd44780/`, usable from RP2040, STM32G474 and host
  tests through HAL GPIO, system-timing and synchronization primitives.
- Preserved the established HD44780 control flow from the upstream
  LiquidCrystal driver: 4-bit/8-bit init retries, high-nibble-first transfers,
  row-offset defaults, display/cursor/blink/autoscroll commands, CGRAM writes,
  and command settle delays.
- Added `src/hal/hal_hd44780.h`, module flag docs, API docs,
  `examples/31_hd44780`, and host regression coverage in
  `test_hd44780_driver`.
- Public driver calls now serialize each display instance with a HAL mutex so
  multicore/FreeRTOS tasks cannot interleave command/data GPIO sequences.
- Moved origin attribution/license context into the shared driver location and
  removed the old `src/hal/impl/rp2040/drivers/LiquidCrystal` vendor folder
  after moving the driver behavior into the HAL implementation.

### hal_tsc2007 - shared TSC2007 resistive touch driver

- Added `HAL_ENABLE_TSC2007` with public `hal_tsc2007_*` API and a shared HAL
  I2C implementation for RP2040, STM32G474 and host tests.
- Preserved the source TSC2007 behavior: command-byte layout, 500 us
  conversion wait, 12-bit reply decode, Z1/Z2 pressure reads, duplicate X/Y
  stability filter, invalid `4095` coordinate rejection and final power-down
  command.
- Public driver calls serialize each controller instance with a HAL mutex
  created through the shared create-once helper for multicore/FreeRTOS-safe
  first use.
- Added `examples/32_tsc2007_touch`, module/API docs, origin license context in
  the shared driver folder and host regression coverage in
  `test_tsc2007_driver`.
- Removed the old `src/hal/impl/rp2040/drivers/Adafruit_TSC2007` import after
  moving the driver behavior into the shared HAL implementation.

### hal_bh1750 - shared BH1750 ambient-light driver

- Added `HAL_ENABLE_BH1750` with public `hal_bh1750_*` API and a shared HAL I2C
  implementation for RP2040, STM32G474 and host tests.
- Preserved the proven ArtronShop_BH1750 behavior: continuous H-resolution mode
  command `0x10`, 180 ms first-measurement delay, exact two-byte sample read and
  raw/1.2 lux conversion.
- Added `hal_i2c_read_bytes()` / `_bus()` for direct sensor reads without a
  preceding register-pointer write, keeping request and sample copy inside the
  I2C bus mutex.
- Added `examples/30_bh1750_light`, API/flag/example documentation, and host
  regression coverage in `test_bh1750_driver` plus `test_hal_i2c`.
- Removed the old `src/hal/impl/rp2040/drivers/ArtronShop_BH1750` import after
  moving the driver behavior into the shared HAL implementation.

### hal_timer - STM32G474 TIM6 alarm backend

- Replaced the STM32G474 timer placeholder with a TIM6-backed 1 MHz alarm
  scheduler under `JH_STM32G474_HW`. The backend now supports low-level
  `hal_timer_add_alarm_us()` / cancel, callback-driven rescheduling via a
  positive callback return value, logical alarm pools, and the shared managed
  timer layer (`hal_timer_create/start/stop/pause/resume`).
- Extended the STM32G474 startup vector and lightweight register map with the
  TIM6/DAC underrun IRQ, TIM6 basic-timer registers, update interrupt bits, and
  the minimal NVIC accessors needed by the timer backend.
- Added `test_stm32_hal_timer`, a host-side regression test that compiles the
  real STM32G474 timer backend and covers one-shot alarms, callback
  rescheduling, cancel, pool capacity/destruction, long-delay chunking, and
  managed timer stop/pause/resume behavior.

### hal_gpio - STM32G474 EXTI/NVIC interrupt backend

- Replaced the STM32G474 `hal_gpio_attach_interrupt()` no-op with a real EXTI
  backend under `JH_STM32G474_HW`: pin-to-EXTI routing via SYSCFG, trigger
  edge selection (rising/falling/both), pending-flag clear, IRQ unmasking, and
  NVIC enable for `EXTI0..4`, `EXTI9_5`, `EXTI15_10`.
- Added STM32 EXTI IRQ handlers in the GPIO backend that dispatch callbacks by
  EXTI line from IRQ context.
- Extended the STM32 startup vector table with EXTI entries and weak handler
  aliases so GPIO interrupts are wired end-to-end.
- Extended the lightweight STM32 register map with SYSCFG/EXTI registers and
  EXTI IRQ numbers used by the new GPIO interrupt path.
- `hal_gpio_set_irq_priority()` on STM32G474 now sets NVIC priorities for all
  GPIO EXTI IRQ groups (instead of being a no-op).

### hal_system (stm32g474) - full reset reason/fault path

- Replaced the STM32G474 fault-diagnostics stub with a real backend in
  `src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp`.
- Added reset-cause classification from `RCC->CSR` flags (`IWDGRSTF`,
  `WWDGRSTF`, `SFTRSTF`, `PINRSTF`, `BORRSTF`, `LPWRRSTF`, `OBLRSTF`) and
  reset-flag clear via `RCC_CSR_RMVF` after latching.
- Integrated retained crash handoff from
  `src/hal/impl/stm32g474/port/exception_info.c` into
  `hal_get_last_fault()` / `HAL_RESET_REASON_HARDFAULT`.
- Implemented STM32 stack-guard support:
  `hal_stack_guard_init()` now arms a canary at the linker-provided stack
  limit; `hal_stack_guard_check()` stores a retained overflow marker and
  triggers system reset so the next boot reports
  `HAL_RESET_REASON_STACK_OVERFLOW`.
- Added host-side STM32 regression coverage (`test_stm32_hal_system`) for
  reset-reason mapping, brownout heuristic behavior, fault-frame precedence,
  and stack-overflow marker precedence.

### Documentation

- Updated GPIO API docs with STM32 EXTI line-routing semantics, line-sharing
  constraints, and backend-specific IRQ-priority behavior.
- Refreshed STM32 status docs to reflect delivered GPIO EXTI support and
  removed stale placeholder claims.

### hal_pwm / hal_pwm_freq - STM32G474 TIM PWM backend

- Added a real STM32G474 TIM output-compare/PWM backend for `hal_pwm_write()`,
  replacing the previous host-style value store on hardware builds.
- Added STM32G474 support for optional `HAL_ENABLE_PWM_FREQ` using the same
  `hal_pwm_freq_create/write/destroy` API as RP2040. The backend configures
  TIM2/TIM3/TIM4/TIM15/TIM16/TIM17 channels, defers GPIO alternate-function
  output until the first write, and protects frequency-PWM writes with the
  module mutex.
- Extended the lightweight STM32G474 register map with generic timer accessors,
  PWM mode bits, capture/compare registers, and APB clock enables for the PWM
  timer set.

### tests / shared drivers - datasheet-grounded regression expansion

- Expanded host regression coverage so the current shared drivers are validated
  against device-PDF register and bitfield semantics instead of only
  round-trip/self-consistency paths. Added or significantly extended tests for
  MCP2515, MAX6675, MCP9600, PGA2311, PCF8563, DS3231, DS18B20,
  MCP401x/MAX5395 digipots, and ADS1x15.
- Added explicit `Datasheet anchors used by these tests` comments in the main
  driver test files so register-level expectations can be audited directly back
  to the source PDFs under `doc/datasheets/`.
- Fixed shared MCP2515 message handling exposed by the new tests: standard
  frames now report RTR using the correct SIDL/SRR path, DLC is clamped to
  8 bytes, and payload staging is safe for short or null buffers.
- Fixed shared MCP9600 ambient-resolution handling to match the datasheet
  `DEVICE_CONFIG` bit 7 definition while preserving ADC-resolution bits.
- Simplified `examples/01_blink` to use `LED_BUILTIN`, keeping the example on
  the target abstraction instead of a local per-board pin override.
- Normalized the final `runalltests.sh` summary label to the current
  `RP2040 + STM32G474` target naming.

## [1.7.0] - 2026-06-08

### hal_pga2311 - shared PGA2311 stereo-volume module

- Added a new optional `HAL_ENABLE_PGA2311` module with public API in
  `src/hal/hal_pga2311.h` and facade implementation in
  `src/hal/hal_pga2311.cpp`.
- Added a backend-agnostic shared transport driver in
  `src/hal/impl/shared/drivers/pga2311/pga2311_driver.{h,cpp}` using HAL SPI/GPIO.
- Added gain conversion helpers (dB and half-dB to raw code), raw-code writes,
  optional hardware-mute pin support, and software mute emulation fallback.
- Added `examples/28_pga2311` demonstrating portable PGA2311 usage on RP2040
  and STM32G474 (SPI init, gain stepping, mute/unmute flow).
- Wired module integration across umbrella/config/build paths:
  `hal/hal.h`, `hal_config.h`, root/test/target CMake files,
  `HAL_FLAGS.txt`, and API docs.
- Added `test_hal_pga2311` host regression coverage for config validation,
  SPI write framing, mute behavior, and gain conversion boundaries.

### hal_crypto / wireguard crypto - shared source-of-truth and regression hardening

- Removed duplicate ChaCha20/Poly1305 logic from `hal_crypto.cpp` and delegated
  HAL ChaCha20 + AEAD paths to the shared WireGuard cryptography backend under
  `src/hal/impl/shared/frameworks/wireguard/crypto/`, preserving the public HAL API.
- Added RFC8439/IETF helper entry points in the shared backend:
  `chacha20_init_ietf(...)`,
  `chacha20poly1305_encrypt_ietf_detached(...)`, and
  `chacha20poly1305_decrypt_ietf_detached(...)`.
- Switched host/mock linkage to compile shared WireGuard crypto sources through
  `hal_mock`, and updated `test_wireguard_crypto_shared` to link against
  `hal_mock` to avoid duplicate symbol composition.
- Added regression coverage for:
  - ChaCha20 counter wraparound rejection on very large input lengths in
    `test_hal_crypto`.
  - shared `chacha20_init_ietf(...)` RFC8439 block-vector conformance.
  - shared detached IETF AEAD RFC8439 vector and argument-validation paths.
- Fixed clang-tidy warning in shared `blake2s.c` by making the index expression
  explicitly size_t-typed for little-endian word loads.

### hal_rgb_led - shared portable NeoPixel driver

- Replaced the bundled `Adafruit_NeoPixel` backend with a shared
  NeoPixel core under `src/hal/impl/shared/drivers/neopixel/` (`jh_neopixel.{h,cpp}`),
  keeping the proven buffer layout, color-order mapping, latch timing and
  brightness scaling behavior from the upstream implementation.
- Added RP2040 transport glue using PIO (`rp2040_pio.h`) and switched
  `src/hal/impl/rp2040/hal_rgb_led.cpp` to the shared core.
- Added a new STM32G474 `hal_rgb_led` backend using the same shared core with
  cycle-timed GPIO bitstream output, so `HAL_ENABLE_RGB_LED` now works on both
  RP2040 and STM32G474.
- Enabled `examples/18_rgb_led` for STM32G474 in addition to RP2040.
- Removed the obsolete `drivers/Adafruit_NeoPixel` folder.
- Moved NeoPixel attribution and license notice from README dependency list to
  the shared driver code/location (`impl/shared/drivers/neopixel/`).

### hal_rtc - shared portable DS3231 driver

- Ported the bundled DS3231 real-time clock driver to a shared HAL driver under `src/hal/impl/shared/drivers/ds3231/`.
- The shared `ds3231.{h,cpp}` implementation preserves the original public class/API shape while using JaszczurHAL I2C primitives.
- RP2040 and STM32G474 `hal_rtc` wrappers now use the shared DS3231 driver, so both targets can select `HAL_ENABLE_DS3231` through the same code path.
- Added a new `hal_rtc_get_temperature()` API for DS3231 temperature reads and a dedicated `examples/27_rtc_ds3231` sample.
- Removed the vendored DS3231 driver from `src/hal/impl/rp2040/drivers/DS3231/`.

### hal_rtc - shared portable PCF8563 driver

- Ported the bundled PCF8563 I2C real-time clock driver to a shared, HAL-only driver under `src/hal/impl/shared/drivers/pcf8563/`.
- The shared `pcf8563.{h,cpp}` implementation uses only JaszczurHAL I2C primitives while preserving full functional parity with the original.
- RP2040 continues to use the shared driver through the existing `impl/rp2040/hal_rtc.cpp` wrapper.
- **STM32G474 now has full RTC support for the first time** through a new `impl/stm32g474/hal_rtc.cpp` backend using the same shared PCF8563 driver, enabling `HAL_ENABLE_RTC` and `HAL_ENABLE_PCF8563` on the STM32 platform.
- All RTC functionality is preserved: date/time read/write, clock integrity check, alarms (minute/hour/day/weekday-independent matching), countdown timer (1/60Hz to 4096Hz), and CLKOUT output (disabled, 1Hz, 32Hz, 1024Hz, 32768Hz).
- Removed the vendored PCF8563 driver from `src/hal/impl/rp2040/drivers/PCF8563/`.
- Added `examples/26_rtc_clock` demonstrating portable RTC usage on both RP2040 and STM32G474.
- Documented that RTC support is complete for STM32G474.
- Full test suite and both target static-library builds pass with no warnings.

### hal_display - shared HAL-only display stack

- Added `src/hal/impl/shared/drivers/display/jh_gfx.{h,cpp}` - a portable graphics
  engine providing geometry primitives (line, circle, triangle, rounded rect),
  bitmap rendering, and full text layout with proportional font support.
  The rendering algorithms are adapted from the Adafruit GFX Library (BSD-2-Clause)
  with all platform-specific dependencies removed.
- Added `jh_gfx_font.h` (GFXfont/GFXglyph structs), `jh_gfx_glcdfont.h`
  (built-in 5x7 font), and `shared/drivers/display/Fonts/` (proportional fonts) as
  self-contained data headers usable by any backend.
- Added a shared SSD1306 OLED driver (`shared/drivers/display/ssd1306_driver.{h,cpp}`)
  built on the HAL I2C bus; rendering is delegated to the GFX engine via an
  in-RAM framebuffer flushed by `hal_display_flush()`.
- Replaced the two per-backend `hal_display.cpp` implementations with a single
  shared `shared/drivers/display/hal_display.cpp`. Both RP2040 and STM32G474 now drive
  ILI9341 / ST7735 / ST7789 / ST7796S (over the shared SPI/GPIO panel drivers)
  and SSD1306 (over the shared I2C driver) through the same code path.
- SSD1306 is now supported on STM32G474 (previously stubbed out).
- Removed the vendored Adafruit display libraries from
  `src/hal/impl/rp2040/drivers/`: `Adafruit_GFX_Library`, `Adafruit_ILI9341`,
  `Adafruit_ST7735_and_ST7789_Library`, `Adafruit_SSD1306`, and the now-unused
  `Adafruit_BusIO`.
- `HAL_ENABLE_SSD1306` now propagates `HAL_ENABLE_I2C`.
- Geometry/text tests migrated from the vendored Adafruit_GFX_Library to the
  shared `jh_gfx.cpp` and renamed `test_jh_gfx_geometry`; no platform stubs
  required. Added `test_ssd1306_driver` alongside the existing
  `test_ili9341_driver` and `test_st77xx_driver`.
- Example `09_display_tft` now targets both RP2040 and STM32G474; added
  `25_display_oled` (SSD1306) targeting both backends.
- Removed the obsolete display-platform test stubs.

### hal_can - shared HAL-only MCP2515 driver

- Replaced the bundled MCP2515 backend with a shared HAL-only driver
  under `src/hal/impl/shared/drivers/mcp2515/`, built on JaszczurHAL SPI, GPIO,
  timing and synchronization primitives only.
- RP2040 and STM32G474 `hal_can` wrappers now both delegate to the same shared
  MCP2515 register/SPI engine, preserving the proven upstream control flow for
  reset, mode changes, bit timing, TX/RX buffer handling, masks, filters,
  one-shot TX, wake-up, abort and error counters.
- Added `test_mcp2515_driver` smoke coverage on the mock backend to verify the
  shared driver performs MCP2515 reset/config traffic through HAL SPI and
  configures the chip-select pin through HAL GPIO.
- Added `examples/24_can_mcp2515` for RP2040 and STM32G474.
- Removed the obsolete `drivers/MCP2515` folder. Upstream attribution
  and LGPL notice now live in the shared driver folder instead of README.
- Confirmed the migration with a clean full local quality-gate run: host tests,
  Valgrind, cppcheck, clang-tidy, target static-library builds, and both
  examples builds all pass end-to-end.

### hal_onewire / hal_ds18b20 - shared OneWire driver and separated DS18B20 module

- Replaced the bundled `OneWire` transport and `DallasTemperature`
  dependency with shared HAL-only code under
  `src/hal/impl/shared/drivers/onewire/`, built only on JaszczurHAL GPIO, timing and
  synchronization primitives.
- Separated DS18B20 implementation into a dedicated `src/hal/impl/shared/drivers/ds18b20/`
  submodule while keeping OneWire driver in `src/hal/impl/shared/drivers/onewire/`.
  This follows the shared implementation subfolder convention (hardware
  drivers such as digipot, mcp9600, max6675 and ads1x15; reusable engines such
  as gps).
- RP2040 and STM32G474 now both use the same shared `hal_onewire`
  implementation. `hal_ds18b20` also moved to shared code on the same
  low-level driver, so the STM32G474 default static-library profile now
  enables `HAL_ENABLE_DS18B20` (`HAL_ENABLE_ONEWIRE` is propagated).
- Preserved the working OneWire behaviour: reset/presence timing, read/write
  slots, parasite-power depower semantics, ROM select/skip, normal and
  conditional search state machine, target-family search, CRC8 and CRC16.
- Added PRIMASK-backed `hal_critical_section_enter/exit()` for real STM32G474
  ARM builds so timing-sensitive 1-Wire slots can be protected directly.
- Preserved the DS18B20 flow from the existing backend: init-time address
  probing, ROM validation, scratchpad CRC checks, resolution writes, conversion
  deadline scheduling and cached fresh-sample semantics.
- Added public `hal_onewire_crc16()` and `hal_onewire_check_crc16()` helpers,
  with host test coverage in `test_hal_onewire`.
- Enabled `examples/06_ds18b20` for STM32G474 as well as RP2040.
- Removed obsolete `drivers/OneWire`, `drivers/DallasTemperature`,
  `impl/rp2040/hal_onewire.cpp` and `impl/rp2040/hal_ds18b20.cpp`. Upstream
  OneWire attribution and MIT notice now live in the shared driver source
  instead of README/docs dependency inventories.

### hal_external_adc - shared ADS1X15/ADS1115 driver

- Replaced the bundled `ADS1X15` backend with a shared HAL-only
  driver (`src/hal/impl/shared/drivers/ads1x15/ads1x15_driver.*`) that uses only JaszczurHAL
  I2C, timing and idle primitives.
- RP2040 and STM32G474 now both use the same shared ADS1115 implementation
  through `hal_external_adc`; the STM32G474 default static-library profile now
  enables `HAL_ENABLE_EXTERNAL_ADC`.
- Preserved the working Rob Tillaart ADS1X15 behaviour: ADS1013/1014/1015 and
  ADS1113/1114/1115 variants, gain/mode/data-rate mapping, blocking and async
  conversion flow, comparator settings, threshold registers, raw-to-voltage
  conversion, ADS101x bit shifting and legacy pseudo-differential `0_2`/`1_2`
  reads.
- Added `test_ads1x15_driver` coverage for register config writes, readback,
  ADS1015 shifting, comparator threshold endianness and I2C clock forwarding.
- Added `examples/23_external_adc_ads1115` for RP2040 and STM32G474.
- Removed the obsolete `drivers/ADS1X15` folder. The MIT notice and
  upstream attribution now live in the shared driver source instead of
  README/docs dependency inventories.
- Reorganized shared implementation files into per-module subfolders:
  `shared/drivers/ads1x15/`, `shared/frameworks/gps/`, `shared/drivers/max6675/` and
  `shared/drivers/mcp9600/`, matching the existing `shared/drivers/digipot/` layout.

### hal_thermocouple - shared MCP9600/MCP9601 driver

- Replaced the bundled `Adafruit_MCP9600` / `Adafruit_MCP9601`
  backend with a shared HAL-only driver
  (`src/hal/impl/shared/drivers/mcp9600/mcp9600_driver.*`) that uses only JaszczurHAL I2C and
  synchronization primitives.
- RP2040 and STM32G474 `hal_thermocouple` wrappers now both delegate MCP9600 /
  MCP9601 operations to the same shared driver. The STM32G474 default profile
  now enables `HAL_ENABLE_MCP9600` together with the existing MAX6675 backend.
- Added `hal_i2c_write_read()` / `hal_i2c_write_read_bus()` for atomic
  write-then-repeated-start-read register transactions, with implementations
  for RP2040, STM32G474 and mock builds.
- Preserved the working Adafruit-derived MCP9600 logic: device-ID acceptance
  for `0x40` and `0x41`, reset config write `0x80`, sleep-mode NAN returns,
  signed 0.0625 C fixed-point decoding, 24-bit ADC sign extension, alert
  register layout and the existing inverted ambient-resolution bit mapping.
- Removed the obsolete `drivers/Adafruit_MCP9600` folder. The BSD
  notice and upstream attribution now live in the shared driver source instead
  of README/docs dependency inventories.
- Added `test_mcp9600_driver` coverage plus sequential mock I2C RX scripting
  for multi-register read flows.

### hal_digipot - shared MCP401x/MAX5395 drivers

- Split the digipot chip logic out of `src/hal/hal_digipot.cpp` into shared
  target-neutral drivers:
  `src/hal/impl/shared/drivers/digipot/digipot_mcp401x.cpp` and
  `src/hal/impl/shared/drivers/digipot/digipot_max5395.cpp`.
- Added the internal `hal_digipot_ops_t` contract
  (`impl/shared/drivers/digipot/hal_digipot_ops.h`). `hal_digipot.cpp` now owns only the
  public handle pool, per-instance mutex, backend selection and ops dispatch,
  matching the portable-driver shape used by `hal_thermocouple`.
- Preserved the existing working MCP401x and MAX5395 behaviour, including
  validation rules, I2C frame order, read-back verification for MCP401x,
  MAX5395 reset/charge-pump/shutdown handling, and integer-only wiper
  calculations.
- Updated CMake/shared-source globs and clang-tidy source selection so nested
  shared implementations under `impl/shared/drivers/` and
  `impl/shared/frameworks/` are built and checked by RP2040,
  STM32G474 and host/mock targets.

### hal_thermocouple / MAX6675 - shared HAL-only driver

- Replaced the `MAX6675` class backend with a shared in-tree driver
  (`src/hal/impl/shared/drivers/max6675/max6675_driver.*`) built only on JaszczurHAL GPIO and
  delay primitives. The RP2040 thermocouple wrapper now delegates MAX6675 reads
  to this shared driver instead of using the old bundled GPIO wrappers.
- Removed the obsolete `drivers/MAX6675` folder. The protocol
  attribution and BSD notice for the Adafruit MAX6675 reference now live in the
  shared driver source instead of README/docs inventory entries.
- The shared driver preserves the working MAX6675 transaction logic from the
  old backend: per-driver mutex, CS-low + 10 us settle, two MSB-first 8-bit
  `spiread()` passes, open-circuit bit check, `raw >> 3`, and 0.25 C/LSB.
- Added an STM32G474 `hal_thermocouple` backend for MAX6675, using the same
  shared bit-bang driver as RP2040.
- `HAL_ENABLE_MAX6675` now propagates only `HAL_ENABLE_THERMOCOUPLE`; it no
  longer pulls in `HAL_ENABLE_SPI`, because the MAX6675 path does not use HAL
  SPI.
- Added `test_max6675_driver` plus mock GPIO read scripting to verify MAX6675
  raw-frame decoding, open-circuit detection, pin setup and 16-bit bit-bang
  reads on the host.

### examples - unified CMake build system + documentation

- New unified example build system compiles the declared RP and STM32G474
  matrices through one dispatcher. CMake presets provide named host profiles.
- All 23 RP2040 examples and all 12 STM32G474 examples now compile cleanly
  (verified end-to-end).
- Fixed `atomic_stubs_cm4.c` preprocessor guard: changed from
  `#if defined(__arm__) || defined(__thumb__)` (matched RP2040 too, causing
  multiple-definition linker errors) to
  `#if defined(HAL_TARGET_STM32G474) || defined(STM32G474xx) || defined(STM32G4)`.
- Fixed `examples/09_display_tft`: renamed `app.c` -> `app.cpp` because it calls
  the C++ function `draw7SegString` (C++ linkage mismatch caused undefined
  reference on RP2040).
- Added `examples/README.md` documenting the build system, requirements,
  per-platform compilation commands, application structure, and the
  `app_start`/`app_task0`/`app_task1` entry-point contract.

### stm32g474 / hal_spi - hardware SPI transfer layer

- Added structured SPI transaction/transfer primitives to `hal_spi`
  (`SPISettings`-equivalent settings, byte/word/buffer transfer, write-only
  helper, deinit).
- STM32G474 now drives SPI1/SPI2 in hardware with register-level polling
  transfers, AF5 pin setup, software NSS, SPI modes 0-3, MSB/LSB order and
  clock prescaler selection.
- Builds expose a local `<SPI.h>` (`SPIClass`, `SPISettings`, `SPI`, `SPI1`)
  backed by `hal_spi_*` for source-compatible SPI driver integration.
- Mock SPI gained scripted RX/TX capture and transaction-setting inspection,
  with expanded `test_hal_spi` coverage.

### tools / hal_sdlogger - platform-independent tools

- `scanNetworks()` now uses the HAL WiFi scan API;
  scan results are exposed by `hal_wifi_scan_networks()` /
  `hal_wifi_get_scan_result()` with mock coverage.
- Legacy SD/crash logger helpers were moved out of `tools` into the new
  opt-in `hal_sdlogger` module (`HAL_ENABLE_SDLOGGER`). The shared FatFs
  implementation now lives under `impl/shared/frameworks/filesystem/`, with a
  deterministic mock backend and `test_hal_sdlogger` coverage.
- Added `examples/39_sdlogger` for RP2040 and STM32G474, demonstrating SPI SD
  card setup, EEPROM-backed log/crash counters and FatFs 8.3 log filenames.
- `tools` declarations no longer expose platform-specific public types such as
  `String`, `File`, or `SPISettings`; the remaining utilities use portable C
  types and HAL APIs.
- `PROGMEM` / `F()` compatibility fallbacks are centralized in `hal_config.h`
  for portability shims and bundled upstream drivers; `tools_c.h` no
  longer defines its own copies.

### hal_gps - portable NMEA engine + STM32G474 support + richer fix data

- The GPS parser is now a dependency-free, in-tree NMEA engine
  (`impl/shared/frameworks/gps/gps_nmea_parser.cpp`) wrapped by a shared facade
  (`impl/shared/frameworks/gps/hal_gps_core.cpp`). The tokenizer / checksum / RMC / GGA logic
  is ported from TinyGPS++ (no longer linked and without a platform timing
  dependency); GSA / GSV / GST decoding follows the minmea-kind GNSS parser.
  Position age is stamped via `hal_millis()` in the facade.
- **STM32G474 now has a GPS backend** (the porting goal): the same engine runs
  there, fed from a hardware UART (`hal_uart`, USART1 by default). RP2040 keeps
  its behaviour; its transport is no longer hard-wired to SoftwareSerial - it
  can use UART or SoftwareSerial (compile-time `HAL_GPS_TRANSPORT_*`, default
  SoftwareSerial), and the 8N1<->7N1 auto-detect is preserved.
- `HAL_ENABLE_GPS` no longer auto-enables SoftwareSerial. It now requires a
  transport - `HAL_ENABLE_SWSERIAL` **or** `HAL_ENABLE_UART` - enforced by a
  compile-time `#error` (the `07_gps` example config gained `HAL_ENABLE_SWSERIAL`).
- Extended API (additive, existing getters unchanged): `hal_gps_altitude_m`,
  `hal_gps_course_deg`, `hal_gps_satellites_used`, `hal_gps_satellites_in_view`
  (summed across GP/GL/GA/GB), `hal_gps_hdop` / `hal_gps_vdop` / `hal_gps_pdop`,
  `hal_gps_fix_quality`, `hal_gps_fix_mode`, `hal_gps_horizontal_accuracy_m`
  (GST, sqrt of the error-ellipse axes) - parity with the GNSS fix fields
  decodable from standard NMEA (per-satellite/DR data is left out as
  module-specific).
- Mock backend gained matching getters and `hal_mock_gps_set_*` injectors; the
  portable parser has its own host test (`test_gps_nmea_parser`) that feeds real
  sentences (computed checksums) and asserts the decoded fields and mappings.
- NMEA numeric helpers `from_hex`, `parse_decimal`, `parse_degrees` were moved
  from `impl/shared/frameworks/gps/gps_nmea_parser.cpp` to shared utilities (`utils/tools.cpp`
  + `utils/tools_api.h`) and covered by `test_tools` unit tests.

### hal_digipot - I2C digital potentiometers (multiplatform, opt-in)

- New opt-in module `hal_digipot` (`HAL_ENABLE_DIGIPOT`):
  `hal_digipot_init()`, `hal_digipot_deinit()`, `hal_digipot_set_resistance()`,
  `hal_digipot_step_count()`, `hal_digipot_e2e_resistance()`,
  `hal_digipot_mode()`. Handle-based, multi-instance (up to
  `HAL_DIGIPOT_MAX_INSTANCES`, default 4), per-instance mutex.
- Two backends, selected per-handle: `HAL_ENABLE_MCP401X`
  (Microchip MCP4017/4018/4019, I2C 0x2F, 128 taps) and `HAL_ENABLE_MAX5395`
  (Maxim MAX5395, I2C 0x28/0x29/0x2B, 256 taps); both propagate
  `HAL_ENABLE_DIGIPOT` + `HAL_ENABLE_I2C`. Modes: voltage divider and W-L / W-H
  rheostat.
- Proof-of-concept for the portable-driver model: `hal_digipot.cpp` is a
  backend-agnostic facade over shared chip drivers built on `hal_i2c`, so the
  same chip logic runs on RP2040 and STM32G474 (and the host mock). Verified to
  build on the mock, the STM32 host-sanity target and the real ARM `stm32_lib`.
- Mock `hal_i2c` gained a write-frame capture log
  (`hal_mock_i2c_reset_write_log()`, `hal_mock_i2c_get_write_frame_count()`,
  `hal_mock_i2c_get_write_frame()`) so the Unity suite asserts the exact bytes
  the driver transmits against hand-computed wiper values.

### stm32g474 - real ADC1 backend

- The STM32G474 `hal_adc` backend is now a real ADC1 reader (was a host
  stub): single-ended, polled, one regular conversion per `hal_adc_read()`,
  under `JH_STM32G474_HW`. The first read lazily brings ADC1 up (ADC12 clock,
  internal regulator + startup wait, single-ended calibration, enable) and
  routes the requested pin to analog mode on demand; no public init entry
  point was added, so the simple `set_resolution` / `read` API is unchanged.
- ADC kernel clock is HCLK/1 (CKMODE=01), so the HSI16 bring-up clock gives a
  16 MHz ADC clock (in spec). Resolution (6/8/10/12-bit) maps onto CFGR.RES;
  sample time is 247.5 cycles for higher-impedance sources. EOC is polled with
  a bounded busy-loop (same style as the I2C backend).
- Pin -> channel map (JaszczurHAL pin id `port*16+pin`) covers the ADC1
  single-ended inputs per RM0440: PA0..PA3 -> IN1..IN4, PB0 -> IN15, PB1 -> IN12,
  PB11 -> IN14, PB12 -> IN11, PB14 -> IN5, PC0..PC3 -> IN6..IN9. Unreachable pins
  return 0. Host-stub behaviour is preserved for the off-target build.
- Added ADC1 + ADC12-common register definitions to `port/stm32g474_regs.h`.
- Note: the register sequence follows RM0440 but is pending on-silicon
  validation on a real Nucleo-G474RE (same status as the I2C backend).

### stm32g474 - real I2C1 master backend

- The STM32G474 `hal_i2c` backend is now a real I2C v2 master (was a host
  stub): I2C1 on SCL=PB8 / SDA=PB9 (AF4), 100 kHz, register-level transfers
  with AUTOEND, NACK and timeout handling, under `JH_STM32G474_HW`. The
  Wire-style buffered API (begin/write/end, request_from/read, write_byte,
  read_byte, is_busy) maps onto real master write/read transfers. Host-stub
  behaviour is preserved for the off-target build.
- Added I2C1 register definitions and a documented TIMINGR for the 16 MHz
  bring-up clock to `port/stm32g474_regs.h`.
- New example `examples/g474_i2c_scan/` - a bus scanner with a
  Linux-Mint/Debian build-flash-verify guide (wiring, pull-ups, expected
  output, troubleshooting) for validating the backend on a real Nucleo-G474RE.
- Note: the register sequence follows RM0440 but is pending on-silicon
  validation (that is what the scanner example is for).

### examples/portable_blink - fix include path

- `blink_app.c` now uses the `hal/`-prefixed includes (`<hal/hal_gpio.h>`,
  ...) so the shared source resolves in RP and G474 builds; fixes the examples
  CI job. README updated:
  the portable demo lives in `examples/portable_blink/` (replacing the removed
  `stm32_lib/blink_g474/`).

### hal_pcnt - edge / pulse counter (multiplatform, opt-in)

- New opt-in module `hal_pcnt` (`HAL_ENABLE_PCNT`): `hal_pcnt_is_supported()`,
  `hal_pcnt_channel_count()`, `hal_pcnt_init(channel, pin, edge)`,
  `hal_pcnt_read()`, `hal_pcnt_reset()`, `hal_pcnt_read_and_reset()`.
  Edge select: rising / falling / both; free-running 32-bit count.
- STM32G474 backend: hardware counter on TIM2 in external-clock mode 1,
  channel 0 = TIM2_CH1 (PA0/AF1), zero CPU per edge (register-level under
  `JH_STM32G474_HW`).
- RP2040 backend: software counter driven by a GPIO edge interrupt
  (`hal_gpio_attach_interrupt`) - same contract, ISR-rate limited. A nice
  contrast: identical API, hardware timer on G474 vs ISR counter on RP2040.
- Mock backend with `hal_mock_pcnt_inject/_get_edge/_get_pin` and a Unity
  suite (`test_hal_pcnt`); documented in `doc/HAL_FLAGS.txt`.
- Second of the planned core-peripheral additions (DAC -> **PCNT** -> GPT
  capture/compare/encoder -> SPI-slave -> RNG).

### hal_dac - true DAC output (multiplatform, opt-in)

- New opt-in module `hal_dac` (`HAL_ENABLE_DAC`): `hal_dac_is_supported()`,
  `hal_dac_resolution_bits()`, `hal_dac_max_value()`, `hal_dac_init()`,
  `hal_dac_write()`, `hal_dac_write_millivolts()`. Uniform channel numbering
  (0,1,...); VREF via `HAL_DAC_VREF_MV` (default 3300).
- STM32G474 backend: real DAC1, 12-bit, channel 0 -> PA4, channel 1 -> PA5
  (register-level under `JH_STM32G474_HW`, host-stub otherwise).
- RP2040 backend: the RP2040 has no DAC, so `hal_dac_is_supported()` returns
  false and writes are no-ops (the honest multiplatform behaviour; use
  `hal_pwm` + RC filter instead). Portable code should branch on
  `hal_dac_is_supported()`.
- Mock backend with `hal_mock_dac_get()` / `hal_mock_dac_is_initialized()`
  helpers and a Unity suite (`test_hal_dac`); documented in `doc/HAL_FLAGS.txt`.
- First of the planned core-peripheral additions (DAC -> PCNT -> GPT
  capture/compare/encoder -> SPI-slave -> RNG) hardening the STM32G474 backend.

### hal_target - explicit multiplatform backend selection

- New `src/hal/hal_target.h`: a single compile-time switch that
  selects the hardware backend. Define exactly one of `HAL_TARGET_RP2040`,
  `HAL_TARGET_STM32G474`, `HAL_TARGET_MOCK` in `hal_project_config.h` (or via
  `-D`).
- If none is defined the target is auto-detected from the toolchain, so
  existing RP2040 consumers need no change. Selecting two targets, or a bare-metal ARM build with no
  match, is a compile-time `#error`.
- Exposes `HAL_TARGET_IS_RP2040 / _IS_STM32G474 / _IS_MOCK` and
  `HAL_TARGET_NAME`; derives `JH_STM32G474_HW` (G474 + ARM) for register code
  vs host-stub builds.
- Replaced the fuzzy per-file platform guards across all backend files with explicit
  `#if HAL_TARGET_IS_*` guards. Unused backends compile to nothing.
- Wired the switch into `hal_config.h` and the build configs
  (`CMakeLists.txt` -> MOCK, `rp_native_lib` -> RP, `stm32_lib` -> STM32G474).
  Documented in `doc/HAL_FLAGS.txt`.

### stm32g474 - first real (non-stub) backend bring-up

- Added a self-contained bare-metal port under
  `src/hal/impl/stm32g474/port/` (CMSIS-light register map, C startup + vector
  table, `SystemInit`, 1 kHz SysTick) plus `stm32_lib/STM32G474RETx_FLASH.ld`.
- Real time base (`hal_millis/micros/delay` via SysTick, replacing the
  `g_millis += ms` stub), real GPIO (with a `port*16+pin` numbering map),
  real `hal_serial`/debug over USART2 (ST-Link VCP), `__WFI` idle, and device
  UID from `UID_BASE` - all gated by the derived `JH_STM32G474_HW`.
- Cortex-M4 fault capture (`port/exception_info.*`): stacked frame (R0-R3/R12/LR/PC/xPSR) +
  CFSR/HFSR/MMFAR/BFAR, retained in `.noinit` across reset and dumped over the
  debug console.
- New example `stm32_lib/blink_g474/` (portable `hal_*` blink + boot/fault
  report) with `build.sh`, CMake, and a Linux-Mint/Debian build-and-flash
  guide for the Nucleo-G474RE.

### hal_math - generic decimal rounding helper

- Added `roundToN(float v, int n)` to `hal_math.h`
  (with backward-compatible alias `hal_roundToN(v, n)`).
- Behavior: `n < 0` is treated as `0`, `n > 6` is clamped to `6`,
  and half values are rounded away from zero.
- Added unit tests in `test_hal_system` for signed values and
  precision clamping.

### hal_simcom_a76xx - cellular location (LBS) API

- New public type `hal_simcom_a76xx_cell_location_t` and helper
  `hal_simcom_a76xx_get_cell_location()`.
- The helper issues `AT+CLBS=1,1`, parses `+CLBS: 0,<lat>,<lon>,<accuracy>`,
  and returns coarse cell-based coordinates (non-GNSS).
- Parser accepts both modem variants: `+CLBS: 0,<lat>,<lon>,<accuracy>`
  and `+CLBS: 0,<lat>,<lon>` (with `accuracy_m = -1`).
- The same API now returns HAL-estimated speed in `speed_kmh` (computed
  from consecutive fixes, with basic outlier filtering and smoothing;
  `-1` when unavailable).
- Added unit tests in `test_hal_simcom_a76xx` covering success,
  non-zero modem status parsing, and invalid arguments.

### hal_simcom_a76xx - GNSS API

- New public type `hal_simcom_a76xx_gnss_location_t` plus helpers:
  `hal_simcom_a76xx_gnss_location_init()`,
  `hal_simcom_a76xx_gnss_power_on()`,
  `hal_simcom_a76xx_gnss_is_powered()`, and
  `hal_simcom_a76xx_get_gnss_location()`.
- The GNSS helper tries common SimCom response variants:
  `AT+CGNSSINFO`, `AT+CGNSINF`, and `AT+CGPSINFO`, normalising
  coordinates, speed, course, altitude, DOP values, satellites and UTC
  into one structure.
- For the A7670E `+CGNSSINFO` shape observed in the field
  (`<fix>,<sat_count>,...,<lat>,N,<lon>,E,...`), the parsed satellite
  count is exposed as both `satellites_used` and `satellites_view`
  instead of leaving `satellites_used = -1`.
- GNSS power-on tries common firmware command variants:
  `AT+CGNSSPWR=1`, `AT+CGNSSPWR=1,1`, `AT+CGNSPWR=1`,
  `AT+CGPS=1,1`, and `AT+CGPS=1`.
- Empty fix responses such as `+CGNSSINFO: ,,,,,,,,` return
  `HAL_SIMCOM_A76XX_NOT_READY`, allowing applications to distinguish
  "no fix yet" from command/parse failures.
- Added unit tests for GNSS initialisation, power-on, no-fix handling,
  `+CGNSSINFO`, `+CGNSINF`, `+CGPSINFO`, fallback order and invalid args.

### hal_simcom_a76xx - `+CLBS` parser tolerates fragmented URC

- **Bug fix.** Some A7670 firmware builds split the `+CLBS:` URC across
  multiple UART writes, and the CRLF boundary can land in the middle of
  a numeric field - e.g. the line is delivered as
  `+CLBS: 0,50.2743\r\n72,19.124077,550\r\n` instead of
  `+CLBS: 0,50.274372,19.124077,550\r\n`. The previous parser bailed
  on the embedded CRLF and the helper returned `HAL_SIMCOM_A76XX_PARSE`
  (callers saw `cell_valid=0, cell_error=5`, and `speed_kmh` stayed
  `-1` because no successive fix was ever produced).
- The parser now stitches contiguous fragments by inspecting the
  character that follows the embedded CRLF: if it continues a numeric
  field (digit / `.` / `,` / sign / whitespace) the CRLF is dropped;
  otherwise the CRLF is treated as the real line terminator. Truncated
  payloads (no terminator seen at all) still return PARSE so callers
  do not consume half-received coordinates.
- Added regression test
  `test_get_cell_location_payload_split_mid_number`.

### hal_modem_at - new `hal_modem_at_listen_more()`

- Same contract as `hal_modem_at_listen_until()` but does **not** call
  `reset_rx()` before draining. Lets callers preserve the partial
  response left in the scratch buffer by a preceding
  `hal_modem_at_send()` / `_send_with_data()` and append additional
  bytes that arrive afterwards.
- Needed for SimCom commands like `AT+CLBS=1,1` where the trailing
  `\r\nOK\r\n` arrives **before** the matched `+CLBS:` URC payload.
  In that timing the tail-grace window inside `hal_modem_at_send()`
  exits as soon as `+CLBS:` appears, leaving the rest of the line in
  flight; the previous `listen_until()` fallback would `reset_rx()`
  and discard the already-received marker + leading digits.
- `hal_simcom_a76xx_get_cell_location()` now uses `_listen_more()` on
  the parse-retry path so the stitching parser sees the full URC line.

### hal_modem_at - `expected` no longer races early "OK"

- **Bug fix.** When a caller passed an `expected` substring to
  `hal_modem_at_send`/`_send_with_data` (e.g. `"+CMQTTSUB: 0,0"`,
  `"+CMQTTCONNECT: <ci>,0"`, `"+CMQTTPUB: <ci>,0"`,
  `"+CMQTTSTART: 0"`), the response terminator could trigger on the
  bare `\r\nOK\r\n` line that SimCom CMQTT* commands emit BEFORE the
  asynchronous result-code URC. The driver then returned success and
  fired the next AT command while the modem was still processing the
  previous one (subscribe in flight, broker round-trip pending). The
  next CMQTT* command silently failed with `NO_PROMPT` because the
  modem was unresponsive during that window.
- New semantics in `hal_modem_at`: when `expected` is non-NULL, only
  `expected` (and `ERROR` / `+CME ERROR` / `+CMS ERROR`) terminate the
  wait. The bare `OK` line is no longer treated as success in that
  mode. When `expected` is NULL, behaviour is unchanged.
- Regression test added (`test_send_expected_waits_past_early_ok`).
- Companion fix: when `expected` matched, `hal_modem_at_send` now
  performs a short tail-drain (≤200 ms) until `\r\nOK\r\n` /
  `\r\nERROR\r\n` arrives. This prevents the trailing `OK` of payload
  responses like `+CCLK: "..."\r\n\r\nOK\r\n` (where `expected="+CCLK:"`
  matches before `OK`) from leaking into the next command's RX buffer.
  Regression test: `test_send_expected_drains_trailing_ok`.
- No public API change; no driver change required (the existing
  `expected="+CMQTT...: <ci>,0"` strings now actually wait for the URC).

### hal_modem_at + hal_simcom_a76xx - watchdog-friendly long waits

- `hal_modem_at` now exposes an application "tick" callback installed
  via `hal_modem_at_set_tick_callback(h, cb, user)`. Every internal
  poll loop (`hal_modem_at_send`, `_send_with_data`, `_listen_until`)
  invokes it at the start of each ~2 ms slice.
- New public helper `hal_modem_at_sleep_ms(h, ms)` - a watchdog-friendly
  drop-in for `hal_delay_ms()`. Sleeps the requested duration in slices
  of at most 20 ms, calling the tick callback before every slice.
  Degrades to plain `hal_delay_ms` when no tick is installed or `h` is
  NULL.
- `hal_simcom_a76xx` now uses `hal_modem_at_sleep_ms()` internally for
  every blocking wait (PWRKEY pulse, hard-reset, AT retry, SIM-ready
  poll, network-registered poll, MQTT tear-down). Long bring-up
  sequences (`wait_boot` + `wait_sim_ready` + `wait_network_registered`
  can stack up to ~75 s) no longer starve the application watchdog
  when the tick callback is installed.
- Tests: 5 new unit tests inside `test_hal_modem_at` (cadence during
  send timeout, unregister, sleep without tick is a plain delay,
  sleep with tick fires the callback, NULL-handle safety). All 40
  CTest binaries remain green.

### hal_modem_at + hal_simcom_a76xx - cellular modem stack

- New facade module `hal_modem_at` (`HAL_ENABLE_CELLULAR_MODEM`): generic
  transport-level AT-command engine sitting on top of `hal_uart`. Single
  shared implementation works on hardware and mock backends.
  - Public API: `hal_modem_at_create` / `_destroy`, `hal_modem_at_send`,
    `hal_modem_at_send_with_data` (3-phase: command -> `>` prompt ->
    payload -> OK/ERROR), `hal_modem_at_listen_until` (passive boot/URC
    waiter with quiet-window logic), `hal_modem_at_last_response`,
    `hal_modem_at_urc_register` / `_urc_poll`, `hal_modem_at_set_log_filter`
    (redacts secrets in debug output), `hal_modem_at_set_line_observer`
    (raw per-line tap used for multi-line URC payload reassembly).
  - Every handle is multi-thread safe via a per-instance `hal_mutex`.
- New driver `hal_simcom_a76xx` (`HAL_ENABLE_A7670`, auto-propagates
  `HAL_ENABLE_CELLULAR_MODEM` + `HAL_ENABLE_UART`): vendor-specific
  bring-up and full MQTT client for SimCom A76xx-family modems
  (A7670E/SA/G, A7672E/S, A7608, ...).
  - Lifecycle: `_create` / `_destroy` / `_get_at`.
  - Power: `_power_toggle` (PWRKEY pulse), `_hard_reset` (double-pulse
    sequence). Both no-op when `pwr_pin == -1` (test fixtures).
  - Bring-up: `_wait_boot` (passive listener for `*ATREADY` / `+CPIN:
    READY` / `SMS DONE` / `PB DONE` with grace + quiet-window logic),
    `_init` (AT/ATE0/CLTS/CEREG handshake), `_wait_sim_ready`,
    `_wait_network_registered` (home or roaming), `_attach_pdp` (CGDCONT
    + CGACT).
  - Time: `_get_network_time_iso8601` (parses `AT+CCLK?` and emits
    `2024-03-21T14:30:00+02:00`-style strings with TZ quarter-hour
    offsets).
  - MQTT publish: `_mqtt_connect` (with optional SSL profile applied via
    `CSSLCFG` + `CMQTTSSLCFG`, full tear-down of any previous session
    before `CMQTTSTART`), `_mqtt_disconnect`, `_mqtt_publish` (3-phase
    `CMQTTTOPIC` / `CMQTTPAYLOAD` / `CMQTTPUB`).
  - **MQTT subscribe (new on the cellular stack)**: `_mqtt_subscribe`
    (`CMQTTSUBTOPIC` + `CMQTTSUB`), `_mqtt_unsubscribe`,
    `_mqtt_set_message_callback`, `_mqtt_poll`. Incoming messages
    arrive as a four-URC sequence (`+CMQTTRXSTART:` / `+CMQTTRXTOPIC:` /
    `+CMQTTRXPAYLOAD:` / `+CMQTTRXEND:`) interleaved with bare topic
    and payload lines; the driver reassembles them internally and
    delivers a single `hal_simcom_a76xx_mqtt_message_cb_t` invocation
    per message from inside `_mqtt_poll`.
  - Connection state: `_mqtt_is_connected` is maintained automatically
    (set on `CMQTTCONNECT: <ci>,0`, cleared on `_mqtt_disconnect` and on
    the `+CMQTTCONNLOST:` URC).
  - Two CMQTT client slots (0..1) tracked independently.
- Mock UART fixture: new TX-side hook
  `hal_mock_uart_set_write_callback(h, cb, user)` for scripted-reply
  unit tests. Cleared on destroy.
- Tests: 25 unit tests for `hal_modem_at`, 38 unit tests for
  `hal_simcom_a76xx` (covering create/destroy, power, boot, init,
  SIM/network/PDP bring-up, CCLK parser incl. negative TZ, MQTT
  connect/publish/subscribe/unsubscribe, RX URC reassembly with
  callback dispatch, CONNLOST URC). Full suite: 40/40 green.

### hal_system - crash / fault diagnostics

- New public API in `hal/hal_system.h` for post-mortem diagnostics across
  reboots:
  - `hal_fault_subsystem_init()` - early-boot init; latches the silicon
    reset-reason flags, snapshots any retained HardFault info into RAM,
    then clears the volatile flag bits.
  - `hal_get_reset_reason()` / `hal_reset_reason_str()` - backend-agnostic
    classification (`POWER_ON`, `RUN_PIN`, `SOFT`, `WATCHDOG`, `DEBUG`,
    `GLITCH`, `BROWNOUT`, `HARDFAULT`, `STACK_OVERFLOW`, `UNKNOWN`).
  - `hal_get_last_fault()` / `hal_clear_last_fault()` - retrieve captured
    `hal_fault_info_t { valid, pc, lr, psr }` from the previous boot's
    HardFault.
  - `hal_last_boot_was_brownout()` - heuristic for chips (RP2040) whose
    silicon does not distinguish BOR from POR; uses a retained alive
    marker refreshed by `hal_alive_mark()`.
  - `hal_alive_mark()` - call periodically from the main loop to keep the
    brown-out heuristic honest.
  - `hal_stack_guard_init()` / `hal_stack_guard_check()` - install and
    verify a stack-bottom canary at `__StackLimit`; on corruption the
    backend records a synthetic `STACK_OVERFLOW` fault and reboots.
- All three backends (`impl/rp2040`, `impl/.mock`, `impl/stm32g474`)
  implement the API surface. STM32G474 backend currently provides no-op
  stubs (returning `UNKNOWN` / `false`); a first-class STM32G474 fault
  driver is planned.

### hal_system / drivers - STM32G474 SoC driver extraction

- Mirrored the RP2040 layout for the STM32G474 backend. All SoC-specific
  bindings (today: host-stub state; planned: RCC/IWDG/__WFI/ADC1/UID_BASE
  reads) moved out of `src/hal/impl/stm32g474/hal_system.cpp` into
  dedicated SoC drivers at
  `src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.{h,cpp}` and
  `src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.{h,cpp}`.
- `hal_system.cpp` (STM32G474) is now pure dispatch (~120 lines), matching
  the RP backend in structure. `hal_reset_reason_str` stays in the
  HAL layer as a pure enum-to-string mapping.
- `stm32_lib/CMakeLists.txt` gained a recursive glob for
  `impl/stm32g474/drivers/*/*.cpp` so SoC driver sources are picked up
  automatically. Host-stub build (`build_stm32_host`) verified green.

### hal_system / drivers - RP2040 SoC driver extraction

- All RP2040-specific fault-diagnostics logic moved out of
  `src/hal/impl/rp2040/hal_system.cpp` into a dedicated SoC driver at
  `src/hal/impl/rp2040/drivers/rp2040/rp2040_fault.{h,cpp}`. The HAL
  layer now contains only thin wrappers calling `rp2040_fault_*`.
- All remaining RP2040 / pico-sdk bindings (`watchdog_*`,
  `tight_loop_contents()`, `rp2040.getFreeHeap()`, `analogReadTemp()`,
  `reset_usb_boot()`, `pico_get_unique_board_id()`), the Cortex-M `IPSR`
  read backing `hal_in_isr()`, and the UID hex formatter moved out of
  `src/hal/impl/rp2040/hal_system.cpp` into a new SoC driver at
  `src/hal/impl/rp2040/drivers/rp2040/rp2040_system.{h,cpp}`. The HAL
  file is now a pure dispatch surface containing timing calls plus thin wrappers calling
  `rp2040_system_*` / `rp2040_fault_*`.
- The "watchdog timeout caused reboot" latch (C++ static-init
  `__attribute__((constructor))`) now lives inside the driver, keeping
  its rationale (scratch[4] magic vs. `watchdog_reboot()`-driven
  uploads) co-located with the pico-sdk usage.
- Driver owns: the naked-ASM HardFault trampoline, the Cortex-M0+
  exception-frame capture, the retained scratch layout in
  `watchdog_hw->scratch[0..3]` (with the `'JHD'` signature; `[4..7]`
  remain reserved by pico-sdk), the stack canary placement at
  `__StackLimit`, and the pico-sdk reset-reason mapping.
- The `__StackLimit` address is laundered through inline asm
  (`__asm__("" : "+r"(p));`) to avoid the GCC `-Warray-bounds` false
  positive caused by the linker-symbol declaration;
  no `-Wno-array-bounds` waiver was added.

### Mock backend - fault-diagnostics test hooks

- New mock helpers in `src/hal/impl/.mock/hal_mock.h`:
  `hal_mock_set_reset_reason`, `hal_mock_set_last_fault`,
  `hal_mock_set_brownout_suspected`, `hal_mock_alive_was_marked`,
  `hal_mock_alive_reset_flag`, `hal_mock_fault_subsystem_was_inited`,
  `hal_mock_stack_guard_is_armed`, `hal_mock_stack_guard_check_was_triggered`,
  `hal_mock_fault_diagnostics_reset`.

### STM32G474 / CI

- Fixed `src/hal/impl/stm32g474/hal_serial.cpp` compilation regression
  (removed misplaced ISR-path snippets from `hal_deb()`,
  restored the correct ISR fast-path in `hal_derr()` and removed the
  invalid `source` reference there).
- CI now includes an explicit STM32 compile gate:
  `cmake -S stm32_lib -B build_stm32_host` + build.
  This makes `src/hal/impl/stm32g474/*` compile regressions fail PR checks.

### hal_can

- `hal_can_destroy()` now releases its internal mutex on both hardware and
  mock backends (previously the mutex was left allocated after destroy).

### hal_serial / hal_system - ISR-safe debug logging

- New public API `hal_serial_set_flush(bool enabled)` controls the optional
  backend flush step after `hal_serial_print()` / `hal_serial_println()`.
  RP2040 defaults to `enabled=false`; setting it to `true` adds an extra USB CDC
  flush/task poll before releasing the TX mutex. STM32G474 and mock backends
  accept the setting as a portable no-op.
- ISR-safe debug logging: `hal_deb()`, `hal_derr()` and
  `hal_derr_limited()` may now be called from interrupt context. The
  callers detect ISR context via the new `hal_in_isr()` (ARM Cortex-M
  `IPSR` read on ARM, mock-injected flag on host) and on the
  ISR fast-path enqueue the formatted message into a per-backend
  single-producer / single-consumer (SPSC) lock-free ring instead of
  touching the UART. No mutex, no lazy init, no timestamp hook, no
  rate-limiter table lookup, no I/O is performed from the ISR.
- New public API `void hal_debug_loop(void)` drains the ISR ring from
  task context using the regular mutex-protected serial path. Each
  drained line is annotated with `[ISR ts=<micros>]` (original event
  time, not "now") and respects the current `hal_deb_set_prefix()` for
  debug records and the standard `ERROR! ` marker for error records.
  Ring overrun is reported once per drain via a single
  `ERROR! [ISR] dropped N debug message(s)` summary line and the
  internal drop counter is reset.
- `hal_debug_loop()` is safe to call from the very first iteration of
  the main loop, even when no `hal_debug_init()` / `hal_deb()` /
  `hal_derr()` was called yet: the emit path performs the same lazy
  init as `hal_deb()`, and the in-ISR / muted short-circuits use only
  zero-initialised statics. Calling it from ISR context is itself a
  no-op (prevents drain re-entry via the underlying UART mutex).
- Mute (`hal_debug_set_muted(true)`) propagates correctly through the
  ISR path: the producer silently drops without writing to the ring
  and without bumping the drop counter; the consumer discards pending
  records and clears the drop counter on drain.
- New compile-time knobs `HAL_DEBUG_ISR_SLOT_COUNT` (default `16u`,
  must be `>= 2`) and `HAL_DEBUG_ISR_TEXT_MAX` (default `160u`) size
  the per-backend ring.
- Mock-only introspection helpers added in `hal_mock.h` for tests:
  `hal_mock_set_in_isr()`, `hal_mock_debug_isr_used_slots()`,
  `hal_mock_debug_isr_capacity()`, `hal_mock_debug_isr_dropped()`,
  `hal_mock_debug_isr_reset()`,
  `hal_mock_debug_isr_set_test_capacity()`,
  `hal_mock_debug_isr_restore_default_ring()`.
- `test_hal_serial` extended with 30 new Unity test cases covering ISR
  detection, ISR-path routing for all three log APIs, drain FIFO
  ordering, prefix / `ERROR!` / `[ISR ts=]` formatting, ring overflow
  and drop-summary accounting, ring wrap-around, reset, mute-in-ISR
  semantics, timestamp-hook isolation, payload truncation at
  `HAL_DEBUG_ISR_TEXT_MAX`, and lazy-init bypass on the ISR path.

### hal_wireguard (Pico W) - lwIP background-context race + leak fixes

- `WireGuard::beginAdvanced()`, `WireGuard::end()`, `WireGuard::peerUp()`
  and `WireGuard::kickHandshake()` now hold the cyw43/lwIP recursive
  lock (`cyw43_arch_lwip_begin/end`) around every lwIP mutation. On
  Pico W the cyw43/lwIP stack runs in
  `async_context_threadsafe_background` mode; lwIP timers (incl. the
  self-rescheduling `wireguardif_tmr`) and UDP RX callbacks fire from
  a low-priority IRQ/alarm and could observe half-built state during
  WG reconnects, which previously manifested as watchdog reboots.
  The app-level mutex in `hal_wireguard.cpp` does not cover that
  context; this lock does. Implemented as an RAII guard so every
  early-return path in `beginAdvanced()` releases the lock
  automatically. `resolve_ipv4()` is intentionally left outside the
  lock (may perform a blocking DNS query that needs the background
  context running).
- Added a compile-time RP2040 guard that requires `pico/cyw43_arch.h` instead
  of silently degrading the lock to a no-op, which would reintroduce the
  reconnect race.
- `WireGuard::end()` now calls `wireguardif_shutdown(wg_netif)` after
  `netif_remove()`. `netif_remove()` does not clean up the WireGuard
  device context; without the shutdown call every `begin()/end()`
  cycle leaked the `wireguard_device`, left the UDP pcb registered in
  lwIP's demux list (with a dangling `udp_recv` arg) and kept the
  self-rescheduling `wireguardif_tmr` timeout alive forever. Over
  repeated reconnects this exhausted lwIP MEMP pools and led to
  asynchronous hardfaults.
- `wireguardif.c::wireguardif_peer_output`: explicit validation of
  `netif` / `q` / `peer` / `netif->state` / `payload`, and removal of
  the erroneous `pbuf_free(q)` calls on the error paths. The caller
  owns `q` and frees it exactly once; the previous code caused a
  double-free when the UDP pcb or underlying netif was missing.
- `wireguardif.c::wireguardif_network_rx`: explicit validation of
  `arg` / `pcb` / `p` / `addr` and of the pbuf payload; a single
  `pbuf_free(p)` on the error path.
- `wireguardif.c::wireguardif_shutdown`: `free(device)` ->
  `mem_free(device)`. The device is allocated via `mem_calloc()` in
  `wireguardif_init()`, so when `MEM_LIBC_MALLOC == 0` releasing it
  with libc `free()` corrupted the lwIP heap.

### hal_mqtt

- `hal_mqtt_set_socket_timeout()` now also propagates the timeout to
  the underlying `WiFiClient` (`s_wifi_client.setTimeout(timeout_s *
  1000)`). Previously only the MQTT-client layer honoured the value,
  so blocking TCP reads on the WiFi socket could still stall for far
  longer than the requested timeout.

## [1.6.0] - 2026-05-25

- Flag model: opt-in `HAL_ENABLE_*` (BREAKING CHANGE)

### Breaking
- **Removed every `HAL_DISABLE_<MODULE>` flag** (e.g. `HAL_DISABLE_WIFI`,
  `HAL_DISABLE_RTC`, `HAL_DISABLE_THERMOCOUPLE`, `HAL_DISABLE_DISPLAY`,
  `HAL_DISABLE_UNITY`, ...). They are replaced by `HAL_ENABLE_<MODULE>`
  flags that follow an **opt-in** model: by default *no* optional module
  is compiled. Each module must be explicitly enabled in the project's
  `hal_project_config.h` (or via `-D`).
- The previously-existing opt-in flags (`HAL_ENABLE_CJSON`,
  `HAL_ENABLE_LITTLEFS`, `HAL_ENABLE_UDP`, `HAL_ENABLE_WIREGUARD`,
  `HAL_ENABLE_MQTT`, `HAL_ENABLE_OTA`, `HAL_ENABLE_CRYPTO`) keep their
  meaning; they now sit next to the rest of the unified `HAL_ENABLE_*`
  family.
- The only `HAL_DISABLE_*` flag still recognised is **`HAL_DISABLE_ASSERTS`**,
  kept for compatibility with the standard `NDEBUG` / `assert.h` convention
  (asserts are ON by default).

### Migration
- Replace every `HAL_DISABLE_X` in your `hal_project_config.h` / build
  scripts with the corresponding `HAL_ENABLE_<Y>` flags for the modules
  you actually need (the previously implicit "enabled by default" set is
  now explicit). For example:

  ```diff
  - // (nothing - everything was on)
  + #define HAL_ENABLE_WIFI
  + #define HAL_ENABLE_KV          // -> propagates HAL_ENABLE_EEPROM
  + #define HAL_ENABLE_PCF8563     // -> propagates HAL_ENABLE_RTC + HAL_ENABLE_I2C
  + #define HAL_ENABLE_GPS         // -> propagates HAL_ENABLE_SWSERIAL
  ```

  ```diff
  - #define HAL_DISABLE_DISPLAY
  - #define HAL_DISABLE_CAN
  - #define HAL_DISABLE_GPS
  + // (do nothing - those modules are off by default in the opt-in model)
  ```

### Added
- Automatic upward **dependency propagation** in `hal_config.h`: enabling
  a leaf module also enables every module it depends on (e.g.
  `HAL_ENABLE_MQTT` -> `HAL_ENABLE_WIFI`,
  `HAL_ENABLE_PCF8563` -> `HAL_ENABLE_RTC` + `HAL_ENABLE_I2C`,
  `HAL_ENABLE_DS18B20` -> `HAL_ENABLE_ONEWIRE`,
  `HAL_ENABLE_ILI9341` -> `HAL_ENABLE_TFT` -> `HAL_ENABLE_DISPLAY`).
- Compile-time **consistency checks** for facade modules that need a
  backend: `HAL_ENABLE_RTC` without `PCF8563`/`DS3231`,
  `HAL_ENABLE_THERMOCOUPLE` without `MCP9600`/`MAX6675`,
  `HAL_ENABLE_DISPLAY` without `TFT`/`SSD1306`, and `HAL_ENABLE_TFT`
  without any concrete TFT driver all emit a clear `#error`.
- New diagnostic flag **`HAL_CONFIG_VERBOSE`**: when defined, the
  preprocessor emits a `#pragma message` for every `HAL_ENABLE_*` flag
  that is active after propagation.
- Refreshed `doc/HAL_FLAGS.txt` and the corresponding section of
  `JaszczurHAL_API.md` to describe the new opt-in surface, dependency
  graph, and consistency checks.

### Changed
- `vscode-templates/{linux,windows}/hal_project_config.h` rewritten as
  an opt-in checklist with a comment block per module group and a
  commented `HAL_DISABLE_ASSERTS` suggestion.
- `stm32_lib/CMakeLists.txt`: removed the long list of `HAL_DISABLE_*`
  defines (now redundant - modules are off by default in the opt-in
  model); the STM32G474 skeleton now enables only `HAL_ENABLE_I2C` and
  `HAL_ENABLE_UART` that its current backend actually implements.
- `rp_native_lib/CMakeLists.txt` and `stm32_lib/CMakeLists.txt`: the Unity
  inclusion check flipped from `if(NOT HAL_DISABLE_UNITY)` to
  `if(HAL_ENABLE_UNITY)`.
- Root `CMakeLists.txt`: the host-test `hal_mock` target now enables the
  full `HAL_ENABLE_*` matrix so every test in `tests/` keeps building
  against the mock backend.

## [Unreleased] - 2026-05-24 (RTC module feature expansion)

### Added
- Extended `hal_rtc` public API with generic RTC controls:
  interrupt enable mask (`HAL_RTC_IRQ_*`), read-clear event flags
  (`HAL_RTC_FLAG_*`), CLKOUT modes, timer source/count, and alarm
  field configuration (`hal_rtc_alarm_t`).
- RP backend implementation for PCF8563 control/status features:
  alarm, timer, CLKOUT, IRQ-enable, and flag read-clear paths.
- Added DS3231 RTC backend integration (vendored `DS3231` driver),
  selectable via `hal_rtc_config_t.chip = HAL_RTC_CHIP_DS3231`.
- Mock backend support for the extended RTC API, including event-flag
  injection helper `hal_mock_rtc_set_flags(...)`.

### Changed
- Expanded `test_hal_rtc` coverage with roundtrip and invalid-input
  tests for interrupt mask, event flags, CLKOUT, timer, and alarm APIs.
- RTC backend flags now support independent backend selection:
  `HAL_ENABLE_PCF8563` and `HAL_ENABLE_DS3231` can be enabled
  independently or together; either backend propagates
  `HAL_ENABLE_RTC` automatically.
- Documentation synchronized with the current RTC surface:
  `README.md` and `JaszczurHAL_API.md` now include RTC module scope,
  flags/dependency notes, API contracts, and test-suite coverage.

## [Unreleased] - 2026-05-22 (DS18B20 non-blocking module bootstrap)

### Added
- New `hal_ds18b20` public API with non-blocking flow:
  `hal_ds18b20_request()`, `hal_ds18b20_poll()`,
  `hal_ds18b20_take_latest()`.
- RP backend implementation of `hal_ds18b20` with software 1-Wire
  timing (reset/presence, read/write slots, scratchpad CRC check).
- Mock backend implementation + helper observability APIs in `hal_mock`
  for presence/CRC control and injected temperatures.
- New host unit test suite `test_hal_ds18b20`.

### Changed
- `hal/hal.h` now exposes `hal_ds18b20.h` when `HAL_ENABLE_DS18B20`
  is defined.
- `hal_config.h` and `doc/HAL_FLAGS.txt` now document
  `HAL_ENABLE_DS18B20` and `HAL_DS18B20_MAX_INSTANCES`.
- Host-test build registers `test_hal_ds18b20` in `tests/CMakeLists.txt`.
- STM32 bootstrap profile initially left DS18B20 off until the shared OneWire
  backend landed.
- `hal_timer` now exposes an alarm-pool API (`hal_timer_pool_*`) so RP2040
  projects can create dedicated pools on additional hardware alarms and scale
  logical timer count beyond the default pool.
- RP2040 timer backend now treats `add_alarm_in_us()` return values `<= 0`
  as invalid IDs (fix for missed failure path when the SDK returns `0`).
- DS18B20 conversion wait uses `hal_micros64()` deadlines in the non-blocking
  state machine.
- Documentation synchronized with current APIs:
  `README.md`, `JaszczurHAL_API.md`, and `doc/HAL_FLAGS.txt` now reflect
  extended `hal_timer` semantics and the DS18B20 module/test surface.

## [1.5.1] - 2026-05-18

Minor update.

## [1.5.0] - 2026-05-04

Next release.

## [Unreleased] - 2026-05-18 (OTA + LittleFS module bootstrap)

### Added
- New opt-in `HAL_ENABLE_LITTLEFS` feature flag and public `hal_littlefs` API.
- RP implementation of `hal_littlefs` as a thread-safe wrapper around the
  pinned littlefs mount/format/path helpers.
- Mock backend implementation + helper observability APIs in `hal_mock`
  for mount/format result control, file-existence injection and size stats.
- New host unit test suite `test_hal_littlefs`.
- New opt-in `HAL_ENABLE_OTA` feature flag and public `hal_ota` API.
- RP implementation of `hal_ota` as an authenticated staging/applier service
  with queued callback dispatch from `hal_ota_handle()`.
- Mock backend implementation + helper observability APIs in `hal_mock`
  for OTA event injection and callback dispatch validation.
- New host unit test suite `test_hal_ota`.

### Changed
- `hal/hal.h` and `tools_c.h` now expose `hal_littlefs.h` and `hal_ota.h`
  when corresponding `HAL_ENABLE_*` flags are enabled.
- `hal_config.h` now validates `HAL_ENABLE_OTA` cannot be combined with
  `HAL_ENABLE_WIFI`.
- Host-test build enables `HAL_ENABLE_LITTLEFS` and `HAL_ENABLE_OTA` in
  `hal_mock` compile definitions and registers both suites in
  `tests/CMakeLists.txt`.
- Expanded `test_hal_littlefs` with format-failure state preservation and
  missing-path remove semantics.
- Expanded `test_hal_ota` with callback replace/unregister flow and re-begin
  queue-clear coverage.
- Documentation updated (`README.md`, `doc/HAL_FLAGS.txt`,
  `JaszczurHAL_API.md`).

## [Unreleased] - 2026-05-18 (UDP module bootstrap)

### Added
- New opt-in `HAL_ENABLE_UDP` feature flag and public `hal_udp` API.
- RP implementation of `hal_udp` as a thread-safe facade over the shared lwIP
  raw UDP engine.
- Mock backend implementation + helper observability APIs in `hal_mock`
  for inbound packet injection and outbound packet capture.
- New host unit test suite `test_hal_udp`.

### Changed
- `hal/hal.h` and `tools_c.h` now expose `hal_udp.h` when
  `HAL_ENABLE_UDP` is enabled.
- `hal_config.h` now validates `HAL_ENABLE_UDP` cannot be combined with
  `HAL_ENABLE_WIFI`.
- Host-test build enables `HAL_ENABLE_UDP` in `hal_mock` compile
  definitions and registers the new suite in `tests/CMakeLists.txt`.
- Expanded `test_hal_udp` with chunked-read behavior and stop/reset state
  coverage for cached remote endpoint + packet context.
- Documentation updated (`README.md`, `doc/HAL_FLAGS.txt`,
  `JaszczurHAL_API.md`).

## [Unreleased] - 2026-05-18 (WireGuard IPv4 helper APIs)

### Added
- New `hal_wireguard_parse_ipv4(...)` helper in `hal_wireguard` public API,
  for strict dotted-IPv4 to octet-array conversion.
- New IPv4-text convenience wrappers in `hal_wireguard` public API:
  `hal_wireguard_begin_text(...)`,
  `hal_wireguard_begin_advanced_text(...)`,
  `hal_wireguard_kick_handshake_text(...)`.
- Validation coverage for the new parser in `test_hal_wireguard`.
- Validation coverage for WireGuard text wrappers in `test_hal_wireguard`
  (success + invalid IPv4 paths).

### Changed
- Documentation updated (`README.md`, `JaszczurHAL_API.md`) with the new
  WireGuard helper APIs.

## [Unreleased] - 2026-05-18 (WireGuard module bootstrap)

### Added
- New opt-in `HAL_ENABLE_WIREGUARD` feature flag and public `hal_wireguard` API.
- RP backend implementation of `hal_wireguard` as a thread-safe
  wrapper around the bundled WireGuard sources.
- Mock backend implementation + helper observability APIs in `hal_mock`
  for WireGuard config capture, endpoint inject and handshake trigger.
- New host unit test suite `test_hal_wireguard`.

### Changed
- `hal/hal.h` and `tools_c.h` now expose `hal_wireguard.h` when
  `HAL_ENABLE_WIREGUARD` is enabled.
- Bundled WireGuard sources (`WireGuard.cpp`, `wireguard.c`,
  `wireguardif.c`, `wireguard-platform.c`, `crypto.c`)
  are now compile-gated by `HAL_ENABLE_WIREGUARD`.
- Host-test build enables `HAL_ENABLE_WIREGUARD` in `hal_mock`
  compile definitions and registers the new suite in
  `tests/CMakeLists.txt`.
- Documentation updated (`README.md`, `doc/HAL_FLAGS.txt`,
  `JaszczurHAL_API.md`).

## [Unreleased] - 2026-05-18 (MQTT module bootstrap)

### Added
- New opt-in `HAL_ENABLE_MQTT` feature flag and public `hal_mqtt` API.
- RP backend implementation of `hal_mqtt` as a thread-safe
  wrapper around bundled PubSubClient (`frameworks/PubSubClient`).
- Mock backend implementation + helper observability APIs in
  `hal_mock` for MQTT state, publish/subscribe capture and inbound
  message injection.
- New host unit test suite `test_hal_mqtt`.

### Changed
- `hal/hal.h` and `tools_c.h` now expose `hal_mqtt.h` when
  `HAL_ENABLE_MQTT` is enabled.
- Bundled `PubSubClient.cpp` is now compile-gated by
  `HAL_ENABLE_MQTT` (same conditional model as other optional drivers).
- Host-test build enables `HAL_ENABLE_MQTT` in `hal_mock` compile
  definitions and registers the new suite in `tests/CMakeLists.txt`.
- Documentation updated (`README.md`, `doc/HAL_FLAGS.txt`,
  `JaszczurHAL_API.md`).

## [Unreleased] - 2026-04-30 (Fiesta R1.8 - serialized TX on hal_serial)

### Added
- New private TX mutex (`s_tx_mutex`) inside every `hal_serial.cpp`
  backend (RP2040, STM32G474, mock). `hal_serial_print` and
  `hal_serial_println` now lock it for the duration of the underlying
  debug-console write path.
- `hal_serial_set_flush(bool enabled)` exposes a portable flush knob. On
  RP2040 it can request an extra USB CDC flush/task poll before the TX mutex is
  released; STM32G474 and mock accept the setting as a no-op.

### Changed
- Single point of serialization for the TX path. Previously each of
  `hal_deb`, `hal_derr`, `hal_derr_limited` and
  `hal_serial_session_println` ran its own per-function mutex (or
  none) while the actual transport write path was not shared across all
  emitters. On dual-core RP2040 this allowed core 1 `hal_deb` to splice bytes
  in the middle of a framed session reply being emitted by core 0 (or
  vice-versa). The TX mutex closes that API-level race.
- The new TX mutex is acquired at the `hal_serial_print/println`
  boundary, so every caller (debug helpers, session helper, direct
  uses) goes through the same gate. The existing per-function
  mutexes remain in place to serialize debug helper state; the new lock is
  strictly nested inside them and never leads to deadlock because the outer
  mutexes never wrap each other.
- The TX mutex is lazy-created via a small `ensure_tx_mutex` helper
  so callers that emit before `hal_debug_init` (very early bring-up,
  test fixtures) still see a valid lock.
- STM32G474 / mock backends keep the mutex but have no USB CDC flush to perform.

### Migration
- Source-compatible: no API changes.
- Throughput note: every emitter now serializes on the shared TX mutex. On
  RP2040, the optional flush knob may add extra USB CDC polling before the mutex
  is released; leaving it disabled keeps the default path focused on forward
  progress while preserving message-boundary serialization.
- Multi-core projects that previously serialised TX with their own
  global mutex can drop it; double-locking the same critical section
  is harmless but redundant.

## [Unreleased] - 2026-04-30 (Fiesta R1.7 - structural BYE in framed session)

### Added
- `hal_serial_session_vocabulary_t` gains `cmd_bye` and `reply_bye_ok`
  fields. When a vocabulary populates `cmd_bye`, the dispatch path
  recognises it as a structural session-close command: the helper
  emits `reply_bye_ok` (when set), drops `session->active`, and clears
  any pending crypto auth state. Inactive sessions accept BYE and
  re-emit OK (idempotent).
- `hal_serial_session__handle_bye` lives outside `HAL_ENABLE_CRYPTO`
  so projects that compile out the AUTH path can still close sessions
  cleanly. This lets the host orchestrate a graceful disconnect (e.g.
  re-enabling debug logs that were muted while the session was
  active) without polling for an activity timeout.

### Changed
- BYE is the second structural command alongside HELLO: it is
  vocabulary-gated like AUTH/REBOOT (NULL `cmd_bye` -> command
  unrecognised, line falls through to the unknown handler), but its
  handler is unconditionally compiled in. HELLO remains the only
  command whose token spelling is hard-coded.

### Migration
- Existing projects that don't populate `cmd_bye` see no behaviour
  change: `SC_BYE` (or whatever the host calls it) falls through to
  the unknown handler exactly like before.
- Fiesta's `fiesta_default_vocabulary` adds the two new fields so
  every Fiesta firmware that picks up this HAL release will respond
  natively to `SC_BYE` from a companion host GUI / CLI.

## [Unreleased] - 2026-04-27 (Fiesta R1.6 - strip SC_* literals from production code)

### Changed
- `hal_serial_session_vocabulary_default` is now an empty placeholder
  (every field NULL). The R1.0 decoupling kept Fiesta's SC_* tokens
  baked in as the per-field NULL fallback; R1.6 finishes the
  decoupling. Projects that need AUTH or REBOOT_BOOTLOADER handlers
  MUST pass a populated vocabulary via
  `hal_serial_session_init_with_vocabulary`. The legacy
  `hal_serial_session_init` still works for HELLO-only sessions.
- Dispatch path in `hal_serial_session.h` gained NULL guards on the
  three command lookups (`cmd_auth_begin`, `cmd_auth_prove`,
  `cmd_reboot_bootloader`) and the one fmt lookup
  (`reply_auth_challenge_fmt`). NULL means "this command is not
  recognised by this session" -> falls through to the unknown-line
  handler. The reply-side `println` calls were already NULL-safe.

### Tests
- `test_hal_serial_session.cpp` introduced a test-local
  `k_test_sc_vocab` fixture carrying the SC_* family verbatim, and an
  `init_session_with_test_vocab` helper. All 24 existing init call
  sites route through it so the suite still asserts on the SC_*
  wire output. JaszczurHAL `src/` now has zero SC_* literals; SC_*
  appears only in `tests/` as fixture data (acceptance criterion
  from the R1.0 pre-flight grep snapshot).
- `test_hal_serial_session_vocabulary.cpp` updated the two cases
  that encoded the R1.0 fallback contract:
  * `test_classic_init_default_vocabulary_is_empty` - verifies the
    new empty-default semantics (unknown-cmd silently dropped,
    HELLO still works structurally).
  * `test_partial_vocab_unset_fields_remain_unrecognised` - verifies
    that NULL fields fall back to NULL (= unrecognised) rather than
    to a built-in SC_* token, so SC_AUTH_BEGIN with a partial vocab
    that doesn't set `cmd_auth_begin` falls through to the unknown
    handler.
- Full HAL ctest 30/30 green.

### Migration
- Fiesta firmware (ECU/Clocks/OilAndSpeed) already passes
  `fiesta_default_vocabulary` explicitly via R1.2/R1.4/R1.5, so this
  change is silent on that side. Other consumers that relied on the
  built-in SC_* defaults need to either supply their own vocabulary
  or accept that AUTH/REBOOT commands fall through to their unknown
  handler.

## [Unreleased] - 2026-04-27 (Fiesta R1.0 - session vocabulary decoupling)

### Added
- `hal_serial_session_vocabulary.h`: optional vocabulary table that lets
  a project override the inbound command tokens (`SC_AUTH_BEGIN`,
  `SC_AUTH_PROVE`, `SC_REBOOT_BOOTLOADER`) and the outbound reply tokens
  (`SC_OK AUTH_OK`, `SC_AUTH_FAILED ...`, `SC_NOT_AUTHORIZED`,
  `SC_NOT_READY HELLO_REQUIRED`, `SC_OK REBOOT`, `SC_OK AUTH_CHALLENGE %s`,
  `SC_UNKNOWN_CMD`) used by the framed session helper. Each field is
  independently optional - NULL falls back to the matching field of the
  exposed `hal_serial_session_vocabulary_default` singleton.
- `hal_serial_session_init_with_vocabulary()`: new entry point that takes
  a vocabulary pointer alongside the existing identity arguments. The
  classic `hal_serial_session_init()` is now a thin wrapper that passes
  NULL, so existing call sites keep their wire behaviour unchanged.
- `test_hal_serial_session_vocabulary`: new ctest target with 9 cases
  covering full override, partial (per-field) NULL fallback, fall-through
  of original `SC_*` literals to the unknown handler when commands are
  renamed, and verifications for unknown / not-ready / not-authorized /
  auth-failed / reboot reply paths.

### Changed
- The framed session helper no longer hard-codes the SC token strings -
  every dispatch / reply site now looks them up via
  `HAL_SERIAL_SESSION_VOCAB(session, field)`. Default behaviour is
  byte-identical: existing `test_hal_serial_session` (24 cases) passes
  unmodified, and the wire output verified by ctest is unchanged (`OK
  HELLO`, `SC_OK AUTH_CHALLENGE ...`, `SC_OK AUTH_OK`, `SC_OK REBOOT`,
  `SC_AUTH_FAILED ...`, `SC_NOT_AUTHORIZED`, `SC_NOT_READY HELLO_REQUIRED`,
  `SC_UNKNOWN_CMD`).
- HELLO and the structural `OK HELLO module=...` reply remain
  intentionally non-configurable: they encode protocol structure that
  every host parses, not project vocabulary.

### Notes
- HAL itself stays project-agnostic: the vocabulary header has no Fiesta
  scDefinitions dependency, the default singleton is purely a captured
  snapshot of the historical defaults, and consumers that want the old
  behaviour pass NULL (or simply call the classic init).
- This is the foundation for Fiesta refactor R1.1+, which moves the SC
  token strings into a single source of truth on the Fiesta side and
  passes the table at session init.

## [Unreleased] - 2026-04-27 (Framed session Phase 5)

### Added
- `hal_serial_session`: built-in `SC_REBOOT_BOOTLOADER` framed command,
  gated on `hal_serial_session_is_authenticated()`. Authenticated
  sessions get `SC_OK REBOOT`, a brief drain window, and a call to
  `hal_enter_bootloader()`. Unauthenticated sessions get
  `SC_NOT_AUTHORIZED` and the boot ROM is NOT entered. Like the AUTH
  handlers this surface is wrapped in `#ifdef HAL_ENABLE_CRYPTO`; with
  crypto off the command falls through to the user unknown-handler.
- 4 new `test_hal_serial_session` cases:
  reboot-without-auth-rejected, reboot-after-hello-only-rejected,
  reboot-after-auth-acks-and-enters-bootloader, and
  reboot-blocked-after-new-hello-clears-auth. Suite: 20 -> 24.

### Notes
- `hal_enter_bootloader` already existed; Phase 5 only wires it into
  the framed protocol behind the auth gate. Mock backend keeps its
  observable flag (`hal_mock_bootloader_was_requested`).

## [Unreleased] - 2026-04-26 (HAL_ENABLE_CRYPTO opt-in)

### Changed
- `hal_crypto` is now an **opt-in** module gated by `HAL_ENABLE_CRYPTO`
  (define it in `hal_project_config.h` or via `-D`). Without the flag
  `hal_crypto.h` expands to nothing and `hal_crypto.cpp` compiles to an
  empty translation unit, so projects that never touch crypto pay zero
  code/RAM cost. `hal_sc_auth.h` follows the same gate.
- `hal_serial_session` keeps working without crypto: the
  `SC_AUTH_BEGIN` / `SC_AUTH_PROVE` handlers, the auth state fields
  (`authenticated`, `challenge_pending`, `challenge[]`, `auth_counter`,
  `auth_failures`, `uid_bytes`), and the `hal_sc_auth.h` include all
  collapse to nothing. `hal_serial_session_is_authenticated()` stays
  callable and returns `false` unconditionally when crypto is off.
- `hal.h` and `tools_c.h` now include `hal_crypto.h` only inside an
  `#ifdef HAL_ENABLE_CRYPTO` guard.
- `hal_config.h` documents the new flag in its `HAL_ENABLE_*` section
  alongside the existing `HAL_ENABLE_CJSON`.
- VS Code project templates (`vscode-templates/{linux,windows}`) gained
  a commented-out `HAL_ENABLE_CRYPTO` block so users can see the flag
  next to the existing module enable list.

### Build / tests
- The host-test `hal_mock` library defines `HAL_ENABLE_CRYPTO` publicly
  so the existing `test_hal_crypto` and the auth cases in
  `test_hal_serial_session` keep running. Hardware builds keep
  the flag off by default; consumer projects opt in via their own
  `hal_project_config.h`.

### Migration
- Projects that already use any `hal_*` symbol from `hal_crypto.h`
  (Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20, AEAD) or any
  `hal_sc_auth_*` helper must add `#define HAL_ENABLE_CRYPTO` to their
  `hal_project_config.h`. Without the flag the linker reports the
  helpers as undefined.

## [Unreleased] - 2026-04-26 (Auth handshake Phase 3)

### Added
- `hal_crypto`: SHA-256 and HMAC-SHA256 helpers (`hal_sha256`, `hal_sha256_hex`,
  `hal_hmac_sha256`, `hal_hmac_sha256_hex`). Portable C++ implementation
  validated against FIPS 180-2 and RFC 4231 vectors. Bit-stable with the
  host-side mirror copy (for example `sc_sha256.c`).
- `hal_sc_auth.h` - new header-only helper for the framed-session
  authentication handshake. Defines the compile-time salt
  (`FIESTA-SC-AUTH-v1`), per-device key derivation
  (`K_device = HMAC-SHA256(salt, uid_bytes)`), challenge/response
  computation (`HMAC-SHA256(K_device, challenge || session_id_be32)`),
  and a constant-time MAC comparison helper. Salt and constants must
  stay byte-for-byte in sync with the project-specific host mirror in
  the companion repository.
- `hal_serial_session_is_authenticated(session)` - public reader for the
  new auth state.
- Built-in framed commands `SC_AUTH_BEGIN` and `SC_AUTH_PROVE <hex>` in
  `hal_serial_session_poll`. Modules consume them automatically through
  the existing wrapper - no per-module rollout work required.

### Changed
- `hal_serial_session_t` gained five auth fields (`authenticated`,
  `challenge_pending`, `challenge[16]`, `auth_counter`, `auth_failures`)
  plus a cached binary `uid_bytes` used for in-RAM key derivation. A new
  HELLO mints a new `session_id` and clears `authenticated` /
  `challenge_pending`.

### Tests
- 7 new cases in `test_hal_serial_session` covering AUTH_BEGIN gating,
  challenge issuance, fresh-challenge-per-BEGIN, correct-MAC success,
  bad-MAC rejection with one-shot challenge consumption, malformed
  payload rejection, and auth-clear-on-new-HELLO. Suite size: 13 -> 20.

## [Unreleased] - 2026-04-26 (Framed protocol Phase 2)

### Added
- `hal_serial_frame.h` - new header-only wire-framing helpers shared with
  companion host tools. Exposes `hal_serial_frame_encode`,
  `hal_serial_frame_decode`, and `hal_serial_frame_crc8` along with the
  constants `HAL_SERIAL_FRAME_PREFIX` (`"$SC,"`),
  `HAL_SERIAL_FRAME_PREFIX_LEN`, `HAL_SERIAL_FRAME_PAYLOAD_MAX` (256) and
  `HAL_SERIAL_FRAME_LINE_MAX`. Frame format:
  `$SC,<seq>,<payload>*<crc8>\n`. CRC-8/CCITT (poly `0x07`, init `0x00`,
  no reflect, no xor-out) over the bytes between the leading `$` and the
  `*` separator. The `"123456789" -> 0xF4` reference vector is asserted by
  the host-test suite.
- `hal_serial_session_set_unknown_handler(session, cb, user)` - register
  a per-module sink for unrecognised inner payloads (used by ECU/Clocks/
  OilAndSpeed to implement their `SC_*` command sets).
- `hal_serial_session_println(session, payload)` - emit one inner payload
  as a framed reply that echoes the in-flight request's `<seq>`. No-op
  outside the request-dispatch window.

### Changed
- **BREAKING:** `hal_serial_session.h` is now framed-only. Lines that do
  not start with the `$SC,` sentinel are silently discarded by
  `hal_serial_session_poll`. The previous plain-text fall-through (which
  replied `ERR UNKNOWN`) has been removed; host-side tools are expected
  to frame requests, and dropping the legacy path eliminates a
  class of bugs (substring matches against debug-log lines, unframed
  bytes corrupting the stream, modules accidentally responding to noise).
- **BREAKING:** Default reply for unrecognised framed payloads changed
  from `ERR UNKNOWN` (plain text) to `SC_UNKNOWN_CMD` (inside the frame
  envelope, echoing the inbound `<seq>`).
- `HAL_SERIAL_SESSION_MAX_LINE` widened from 48 to 128 bytes to
  accommodate the framing overhead and longer SC payloads.
- `hal_serial_session_t` gained two new fields:
  - `bool in_request` - gates `hal_serial_session_println` so modules
    cannot inject unsolicited bytes,
  - `uint16_t request_seq` - the seq carried in the in-flight request,
    automatically used by `hal_serial_session_println` to correlate
    replies.
- `hal_serial_session.h` documentation rewritten to describe the framed
  protocol (see updated `JaszczurHAL_API.md` §`hal_serial_session`).

### Tests
- `test_hal_serial_session` rewritten for the framed protocol: round-trip
  HELLO with CRC validation, request<->response seq echo, custom unknown
  handler dispatch and reply seq inheritance, default `SC_UNKNOWN_CMD`
  reply when no handler is registered, and silent drop of non-framed
  input. New independent CRC reference vector locks the wire format on
  the firmware side.

## [Unreleased] - 2026-04-24

### Added
- `hal_get_device_uid(uid[8])` and `hal_get_device_uid_hex(buf, buflen)` -
  read the RP2040 64-bit flash unique id, either as a raw 8-byte buffer or
  as an uppercase 16-character hex string. The RP backend wraps
  `pico_get_unique_board_id()`; mock backend returns a deterministic default
  (`E661A4D1234567AB`), overridable via `hal_mock_set_device_uid()` and
  resettable via `hal_mock_reset_device_uid()`. New constants
  `HAL_DEVICE_UID_BYTES` (8) and `HAL_DEVICE_UID_HEX_BUF_SIZE` (17) exposed
  for buffer sizing.
- `hal_serial_session_init()` now takes three additional parameters:
  `module_tag`, `fw_version`, `build_id`. These identity fields are captured
  once at init time, cached in `hal_serial_session_t`, and reported on every
  accepted HELLO. `hal_serial_session_poll()` correspondingly drops its
  `module_tag` parameter. The session context also caches the device UID hex
  string at init time via `hal_get_device_uid_hex()`.
- Extended HELLO response format (still plain text, still one line):
  `OK HELLO module=<name> proto=1 session=<id> fw=<ver> build=<id> uid=<hex>`.
  When `fw_version` or `build_id` is NULL/empty at init, the field reports
  `unknown`. The response buffer was widened from 96 B to 192 B.
- `hal_serial_session.h` constant `HAL_SERIAL_SESSION_UNKNOWN` ("unknown")
  - fallback used by the HELLO response when identity fields are NULL/empty.
- Tests: 7 new tests in `test_hal_system` covering
  `hal_get_device_uid*` (default pattern, injected value, NULL-safety,
  hex formatting uppercase, small-buffer rejection, NULL-buffer safety);
  `test_hal_serial_session` rewritten to cover the new init signature,
  the `unknown` fallback for NULL identity, the `uid=` field (default and
  injected), and full HELLO field assertions. Total HAL host-test count
  unchanged at 28 suites; net new assertions.
- `setDebugPrefixWithColon(moduleName)` - utility helper in `tools_api.h` /
  `tools.cpp` that appends `:` to a module tag and forwards the result to
  `hal_deb_set_prefix()`. Intended to replace repetitive local-buffer +
  `concatStrings(..., MODULE_NAME, ":")` setup code in clients.
- `hal_enter_bootloader()` - HAL entry point for RP2040 BOOTSEL/UF2 reboot.
  The RP backend calls `reset_usb_boot(0, 0)` and does not return;
  mock backend sets an observable flag. Exposed via `hal_system.h`.
- Mock helpers for bootloader observability:
  `hal_mock_bootloader_was_requested()` and
  `hal_mock_bootloader_reset_flag()`.
- `hal_serial_session.h` - header-only text session helper for desktop
  configurator bootstrap. Parses line-oriented input (`\r`/`\n`), handles
  the `HELLO` command, returns `ERR UNKNOWN` for unsupported text.
  Session state (`hal_serial_session_t`) carries active flag, session id,
  hello counter, last-activity timestamp, RX line buffer, and identity
  pointers (module_tag / fw_version / build_id) + cached UID hex.
- `concatStrings(dst, dst_size, src_a, src_b)` - bounded string concat
  helper in `tools.cpp` with full unit-test coverage (zero-length src,
  exact-fit, truncation, NULL args).
- `hal_i2c_write_byte(address, data, *outWriteOk)` /
  `hal_i2c_write_byte_bus(bus, address, data, *outWriteOk)` - one-shot
  convenience helper that performs the common "beginTransmission +
  write one byte + endTransmission" sequence. The internal I2C mutex
  is acquired and released automatically; the optional `outWriteOk`
  pointer receives the queue-byte status, and the return value is the
  `endTransmission` error code. Extracted from ECU PCF8574/Adjustometer
  call sites that previously open-coded the three-step pattern.
- `hal_i2c_read_byte(address, *outReadOk)` /
  `hal_i2c_read_byte_bus(bus, address, *outReadOk)` - symmetric read
  counterpart: requests and returns a single byte from a slave. The
  optional `outReadOk` pointer lets callers distinguish a valid `0x00`
  value from a failed transaction (request_from short read or
  `hal_i2c_read()` returning -1). Extracted from ECU PCF8574 read path.
- Tests: 6 new `hal_i2c_write_byte` tests (begin/write/end sequence,
  busy-bus NACK path, NULL out-flag, lock-depth balance, per-bus
  routing, transaction-count increment) and 6 new `hal_i2c_read_byte`
  tests (injected-byte round-trip, zero value vs. failure disambiguation,
  NULL out-flag, lock-depth balance, per-bus routing, transaction-count
  increment), plus 1 test verifying that `hal_i2c_read_byte()` holds
  the mutex during the actual byte read. Total I2C test count: 30.
- `hal_i2c_bus_clear(sda_pin, scl_pin)` /
  `hal_i2c_bus_clear_bus(bus, sda_pin, scl_pin)` - I2C bus clear
  procedure per I2C specification §3.1.16: toggles SCL up to 9 times
  at GPIO level to release a slave holding SDA low, then generates a
	  STOP condition. Must be called before `hal_i2c_init()`.
	  Hardware backends perform the recovery at GPIO level before returning
	  the pins to their I2C alternate function.
- Mock: `hal_mock_i2c_get_bus_clear_count()` /
  `hal_mock_i2c_get_bus_clear_count_bus(bus)` - return the number of
  `hal_i2c_bus_clear()` calls since the last `hal_i2c_init()`.
- Tests: 3 new I2C bus clear tests (count, reset-on-init, bus
  independence). Total I2C test count: 17.
- `hal_serial_available()` - return the number of bytes available for
  reading from the backend serial RX path.
- `hal_serial_read()` - read one byte from the backend serial RX path; returns
  0-255 or -1 when empty.
- `float_to_u32()` / `u32_to_float()` - `static inline` bitcast helpers
  (float ↔ uint32_t via memcpy) in `tools_api.h`.
- Mock: `hal_mock_serial_inject_rx(data, len)` - inject bytes into the
  mock serial RX buffer for testing `hal_serial_available/read`.
- Tests: `test_tools` now verifies that `setDebugPrefixWithColon("ECU")`
  produces the expected `ECU:` debug prefix.
- `hal_i2c_slave_get_transaction_count()` and
  `hal_i2c_slave_get_transaction_count_bus(uint8_t bus)` - return the
  number of completed I2C bus transactions (master reads and writes)
	  since initialisation. Incremented from the backend slave receive/read
	  event path, so the counter reflects genuine bus activity.
  Resets to 0 on `hal_i2c_slave_init*()`. Wraps at `UINT32_MAX`.
  Thread-safe (atomic access).
- `hal_i2c_get_transaction_count()` and
  `hal_i2c_get_transaction_count_bus(uint8_t bus)` - symmetric API
  for the I2C master (controller) side. Incremented on every
  `hal_i2c_end_transmission*()` (write) and `hal_i2c_request_from*()`
  (read). Resets to 0 on `hal_i2c_init*()`. Wraps at `UINT32_MAX`.
- `hal_gpio_set_irq_priority(priority)` and `hal_irq_priority_t`
  (`HAL_IRQ_PRIORITY_HIGHEST` / `HIGH` / `DEFAULT` / `LOW`) -
  configurable NVIC priority for the GPIO interrupt bank. On RP2040 all
  GPIO pins share `IO_IRQ_BANK0`; call after `hal_gpio_attach_interrupt()`.
  Raising priority above other peripherals (e.g. I2C) prevents their
  ISRs from blocking edge counting. No-op on platforms without
  configurable IRQ priorities. Mock backend exposes the configured
  value for testing.

### Changed
- **Breaking API**: `hal_serial_session_init()` signature changed from
  `(session)` to `(session, module_tag, fw_version, build_id)`; identity is
  now bound once at init rather than re-passed on every poll.
  `hal_serial_session_poll()` signature changed from
  `(session, module_tag)` to `(session)`. Callers in downstream projects
  (ECU / Clocks / OilAndSpeed wrappers in Fiesta) must be updated together.
- HELLO response format extended (additive): now always carries `fw=`,
  `build=`, `uid=` fields in addition to `module=`, `proto=`, `session=`.
  Existing parsers that only looked for `module=` / `session=` prefixes keep
  working; parsers that checked the full line end-of-string must be updated.
- Documentation updated to cover `setDebugPrefixWithColon(...)`, fix the
  published `src/` / `hal/impl/` layout in the API reference, and align the
  utility include guidance (`tools.h` vs `tools_c.h`) with the actual tree.
- `hal_pwm_freq_create()` no longer starts the PWM slice immediately.
  The GPIO function and slice enable are deferred until the first
  `hal_pwm_freq_write()` call, preventing a power-on glitch on pins
  with inverted logic (0 % duty = actuator ON).
- `hal_i2c_slave_reg_write8[_bus]()` and
  `hal_i2c_slave_reg_write16[_bus]()` return `void` again (reverts the
  short-lived return-value approach: writes always target the local
  register-map buffer and therefore always succeed, making return values
  meaningless for detecting real bus activity).

### Fixed
- stm32g474 backend source files now compile as symbol-empty units on
  non-STM32 targets. This avoids duplicate-definition link failures
  when platform build systems compile all backend directories under `src/`.
- stm32g474 mock serial RX path (`hal_serial.cpp`) now guards index ranges and
  normalizes requested lengths before buffer access, removing host-compiler
  array-bounds warnings and preventing potential out-of-bounds reads.
- Mock `hal_i2c_end_transmission_bus()` now returns 2 (NACK on address)
  when the mock busy flag is set. Previously it always returned 0,
  making it impossible to test I2C error paths.
- `hal_i2c_read_byte()` / `hal_i2c_read_byte_bus()` now hold the internal
  I2C mutex across the full one-shot `request + read` sequence, making
  the helper atomic on a given bus (symmetric with `hal_i2c_write_byte()`).
- Mock: added `hal_mock_i2c_get_read_byte_lock_depth()` /
  `hal_mock_i2c_get_read_byte_lock_depth_bus()` to expose lock-depth
  captured at the byte-read point inside one-shot `read_byte` helpers
  for mutex-behavior tests.

## [1.4.0] - 2026-04-14

### Added
- New `HAL_TOOLS_ADC_MAXVALUE` macro in `tools_sensor_config.h`, derived from
  `HAL_TOOLS_ADC_BITS` (default 12).

### Changed
- `getAverageValueFrom()` now performs a dummy read before the sampling loop
  to avoid RP2040 ADC mux cross-talk (residual charge from the previous channel).
- `ntcToTemp()` uses `HAL_TOOLS_ADC_MAXVALUE` instead of hardcoded `4095.0`,
  enabling correct operation on ADC systems with non-12-bit resolution.
- PID controller: clamping anti-windup - integral accumulation is now skipped
  when the output is saturated in the direction of the error, preventing
  windup at output limits. Existing hard clamp via `setMaxIntegral()` retained
  as secondary safeguard.

## [1.3.0] - 2026-04-10

### Added
- New table-based soft timer APIs in hal_soft_timer:
  - hal_soft_timer_table_entry_t
  - hal_soft_timer_setup_table(...)
  - hal_soft_timer_tick_table(...)
- New CAN payload helper in hal_can:
  - hal_can_encode_temp_i8(...)
- New host test suite tests/test_hal_soft_timer.cpp covering:
  - Core hal_soft_timer wrapper behavior
  - Table setup/tick behavior
  - Idle callback and inter-entry delay behavior
  - Invalid input handling
- New test_hal_can coverage for hal_can_encode_temp_i8(...) positive/negative values,
  range saturation, and fractional truncation.
- New README quick example for table-based soft timer setup/tick usage.

### Changed
- hal_soft_timer_setup_table(...) and hal_soft_timer_tick_table(...) now return bool.
- Both table helpers now validate input arguments:
  - Return false when table is NULL
  - Return false when count is 0
  - Emit hal_derr(...) log on invalid input
- JaszczurHAL API docs updated to include table APIs, return contract, and validation behavior.
- JaszczurHAL docs updated to include the shared CAN temperature-byte helper.
- Test build configuration updated:
  - Added src/hal/hal_soft_timer.cpp to host test utility sources
  - Registered test_hal_soft_timer in tests/CMakeLists.txt
