# JaszczurHAL - Future Ideas

Simple backlog of future architecture and implementation work.

- High priority: shared network architecture for STM32 WiFi modem first.
  - Treat STM32 connectivity as the next major platform-unlock item, not as a
    small target-specific patch.
  - Reuse lessons and helper patterns from the existing `hal_modem_at`
    generic AT-command engine and `hal_simcom_a76xx` cellular driver. They
    already cover UART-backed AT command/response flow, URC dispatch,
    watchdog-friendly waits, response buffering and modem-family layering.
    The WiFi work still needs a separate ESP-AT/network contract; do not fork
    the cellular API shape blindly.
  - Use an offloaded WiFi module first. ESP8266MOD is acceptable for prototype
    work and for proving the architecture, but do not name the architecture
    after ESP8266. ESP8266EX is already marked by Espressif as not recommended
    for new designs, so keep the design replaceable by ESP32-C3/S3-class
    modules or other AT/offloaded modules.
  - Prefer role-based flags over chip-based API names:
    `HAL_ENABLE_WIFI_MODEM`, `HAL_ENABLE_ESP_AT`, and only optionally a
    narrower `HAL_ENABLE_ESP8266_AT` profile.
  - Add a target-neutral internal network interface layer, for example
    `hal_netif_ops`, underneath the public `hal_wifi`, `hal_net`, `hal_udp`,
    `hal_tcp` and BSD sockets APIs. Do not expose this as the primary public
    user API unless it proves necessary.
  - The shared layer should own behavior, state machines and tests:
    WiFi scan/connect/disconnect/status/RSSI/MAC/IP, DNS resolution, TCP/UDP
    open/send/recv/close, timeout semantics, reconnect handling, socket state
    and error mapping.
  - Backend-specific adapters should only bind that contract to transport:
    STM32G474 -> ESP-AT over `hal_uart` plus optional reset/enable GPIO;
    RP2040 -> current Pico W / CYW43 / Arduino-Pico network stack;
    future ESP32 -> native Arduino-ESP32 or ESP-IDF backend.
  - Place reusable ESP-AT logic under `src/hal/impl/shared/drivers/esp_at/`
    or a similarly named shared network/modem folder. Keep transport abstract:
    read/write/flush/reset/time/lock should come from a small adapter over HAL
    UART/GPIO/system primitives.
  - The ESP-AT parser must handle asynchronous URCs interleaved with command
    responses, command serialization with one modem lock, `CIPMUX` link IDs,
    partial receives, disconnect notifications and reset/resync after parser
    loss.
  - Initial STM32 milestone:
    1. `AT` probe, firmware/version query, reset and echo-off.
    2. STA mode, scan, connect, disconnect, status, RSSI, MAC and IP.
    3. `hal_net_resolve_ipv4`.
    4. TCP client sockets.
    5. UDP sockets.
    6. BSD sockets compatibility over the existing adapter.
    7. MQTT only after TCP behavior is stable.
  - Keep TLS, OTA and WireGuard out of the first milestone. Add TLS only when
    the selected module provides reliable TLS offload and the API can report
    meaningful errors.
  - Hardware notes for ESP8266-class modules: require solid 3.3 V power with
    current headroom for WiFi TX peaks, local decoupling, proper boot strap
    pins, and explicit `EN`/`RST` control where possible.

- Investigate occasional Fiesta CDC / serial-debug link drops during monitor
  sessions.
  - Not critical for runtime operation because it has not been observed with
    USB disconnected.
  - Symptoms are intermittent/random rather than reliably reproducible.
  - Observed trigger: starting/stopping the persistent serial monitor can
    make Pico-class boards disappear/reappear from the host-side CDC view,
    especially when the monitor is terminated brutally (for example by `kill`)
    instead of exiting cleanly.
  - Important refinement: this currently looks more like USB CDC disconnect /
    reconnect or host-side re-enumeration than a full MCU reset. The Fiesta app
    can keep running normally, visible on the display, while the USB serial
    connection drops or restarts from the host perspective.
  - Current suspicion: behaviour may depend on the USB hub/host path used for
    the connected module.
  - Likely blind alleys unless new evidence appears:
    1200-baud bootloader touch does not match the symptom if the board returns
    to the running app rather than UF2/upload mode; watchdog reset also does
    not match when the display/app loop keeps running continuously.
  - Diagnostic angles: distinguish physical USB connect/disconnect from serial
    port open/close, upload handoff and monitor reconnect; check for 1200-baud
    bootloader touch, DTR/HUPCL side effects, CDC TX backpressure, excessive
    debug logging, watchdog resets and power/ground disturbance through USB.
    Correlate device uptime/display refresh with host `dmesg -w` and
    `udevadm monitor --kernel --property --subsystem-match=tty` so MCU reset,
    CDC-only re-enumeration and hub/port reset are separated cleanly.

- Continue FreeRTOS hardening.
  - Harden module-level synchronization and ownership.
  - Document callback contexts, task contexts and ISR/task boundaries.
  - Run hardware smoke tests before considering FreeRTOS as a default runtime.

- HAL-wide `hal_status_t` migration.
  - DONE: status-first error reporting has been implemented across the audited
    HAL component scope. Future work should treat `hal_status_t` as the
    required error-signalling path for new components and fallible public APIs.

- Continue CAN API v2 follow-up work.
  - Add interrupt-driven RX/TX completion paths.
  - Consider callback-style filter ownership only when interrupts need it.
  - Add transceiver enable/standby abstraction.
  - Add stronger mock/loopback coverage for filters, CAN FD shape and states.
  - Validate the STM32G474 native FDCAN backend on hardware.
  - Keep Zephyr CAN as a reference checklist for timing, states, filters,
    error counters and transceiver handling.

- ADP5360 shared I2C PMIC driver.
  - DONE: added a Zephyr-inspired shared HAL-only ADP5360 driver under
    `src/hal/impl/shared/drivers/adp5360/`, public `hal_adp5360.h`, host
    tests, API docs and `examples/54_adp5360_pmic`.
  - Remaining follow-up: optional INT/PGOOD/reset-status GPIO callback support
    if an application needs interrupt-driven PMIC events.

- Add optional 10-bit I2C master addressing.
  - The whole public I2C API takes `uint8_t address` (7-bit). A 10-bit
    address spans `0x000`-`0x3FF`, so it needs a wider value (`uint16_t`)
    plus an explicit addressing-mode marker: the value alone cannot be
    distinguished from a 7-bit address.
  - Prefer an additive, non-breaking design over widening the existing ABI.
    Widen only the internal transfer helpers to `(uint16_t addr,
    bool addr_10bit)`; keep every current public function delegating with
    `addr_10bit=false` so existing code and shared drivers are untouched.
  - Expose a narrow set of new 10-bit entry points for the atomic modern
    paths that real 10-bit devices use, e.g. `hal_i2c_write_read10_ex()`,
    `hal_i2c_read_bytes10_ex()` and a raw-command write. Do not double the
    whole API surface.
  - Scope out (document as intentional): the legacy buffered Arduino-Wire
    path (`hal_i2c_begin_transmission()` / `cur_addr` are `uint8_t`, and
    Wire semantics are 7-bit anyway) and `hal_i2c_slave` 10-bit target mode
    (separate follow-up).
  - Backend feasibility:
    - STM32G474 (register-level): straightforward. In `i2c_hw_write/read/
      write_read/ack`, program `CR2` with the `ADD10` bit set, place the full
      address in `SADD[9:0]` without the `<<1` shift, and handle `HEAD10R`
      for the read phase. Add the missing `ADD10`/`HEAD10R` bit defines to
      the register map.
    - RP2040: the backend calls Pico SDK `i2c_read_timeout_us()` /
      `i2c_write_timeout_us()`, whose `uint8_t addr` signatures program a
      7-bit `IC_TAR`. 10-bit master needs `IC_CON.IC_10BITADDR_MASTER` and a
      full 10-bit `IC_TAR`, so add a small custom path instead of relying on
      those SDK helpers. This is the most work of the three.
    - Mock: trivial. Widen the stored address and extend expectation matching.
  - Validation: 10-bit addresses use the `11110xx` prefix on the wire; the
    mode helper must not confuse them with reserved 7-bit ranges. Add
    mock-based unit tests for 10-bit transactions; real ACK behavior on
    STM32/RP2040 needs an actual 10-bit device on the bus.
  - Suggested order: land STM32 + mock + tests first (lowest risk), then the
    RP2040 SDK path.

- Polish the legacy utility API after Arduino decoupling.
  - Keep `#include <JaszczurHAL.h>` and `#include <hal/hal.h>` as the primary
    portable include surfaces.
  - Treat `tools.h` as optional compatibility helpers.
  - Consider smaller headers such as `tools_numeric.h`, `tools_strings.h`,
    `tools_debug_aliases.h` and `tools_network_helpers.h`.
  - Keep `tools_api.h` and `tools_c.h` free of C++ and Arduino-only types.
  - Avoid adding convenience helpers to `tools` when they belong in a HAL
    module.

- Add an optional board-description layer.
  - Keep it outside the HAL core.
  - Start with plain header-based board descriptions.
  - Use it for reusable pin maps, bus IDs, device addresses and defaults.
  - Let small applications continue to pass config structs directly.
  - Defer JSON/YAML generation until there is a clear need.

- Revisit static pools only when RAM pressure is proven.
  - Static pools are acceptable while optional modules stay small and guarded.
  - Keep compile-time max-instance knobs such as `HAL_DIGIPOT_MAX_INSTANCES`.
  - Defer linker-section device registration for now.
  - Before introducing linker sections, verify Arduino-pico, STM32 linker
    scripts, host/mock tests and dead-code elimination behavior.

- Mine selected Zephyr drivers for shared HAL driver ideas.
  - Use Zephyr as a design reference, not as code to copy mechanically.
  - Extract only small target-neutral protocol pieces.
  - Avoid devicetree, Kconfig, `struct device`, work queues, Zephyr networking
    objects and shell integration in the first pass.
  - Wrap extracted ideas with JaszczurHAL config structs, HAL I/O primitives,
    mutexes and compatibility wrappers.
  - Display-driver migration notes:
    - The most valuable Zephyr display idea is the generic raw write contract:
      pixel format, buffer descriptor with `pitch`, `width`, `height` and
      `buf_size`, capabilities reporting and `write(x, y, desc, buf)` style
      area updates. DONE: JaszczurHAL now exposes
      `hal_display_get_capabilities_ex()`, `hal_display_set_pixel_format_ex()`
      and `hal_display_write_raw_ex()` with the public pixel/buffer types.
    - Keep the current JaszczurHAL GFX/text/high-level display API. Treat the
      Zephyr-style API as a lower-level raw blit/capabilities layer underneath
      or beside it.
    - DONE: adapted immediate RGB backends (`ILI9341`, ST77xx/GC9A01 and
      SSD1331/SSD135x) plus ST7567 to capabilities/raw-write while preserving
      the existing public wrappers. SSD1306 advertises its buffered GFX path;
      direct raw writes remain intentionally unsupported there.
    - Reuse Zephyr's proven handling for `pitch > width` by splitting writes
      into row-sized transfers, and keep RGB565 byte order explicit.
    - DONE: extended the SSD1306-family driver with Zephyr-informed support
      for `SSD1309`, `SSD1315`, `SH1106` and `CH1115`, including controller
      addressing differences, segment/page/display offsets, hardware
      orientation, contrast, suspend/resume and I2C/SPI bus splitting through a
      status-returning family config entry point.
    - DONE: added Zephyr-informed shared-driver support for `GC9A01`,
      `SSD1331`/`SSD135x` and `ST7567`, all routed through `hal_display` with
      unit and real-facade dispatch coverage.
    - After the raw API exists, consider e-paper controllers such as
      `SSD16xx`/`UC81xx`.
    - Defer DSI/LTDC/MCUX/QEMU/SDL/Renesas-style Zephyr display backends for
      now; they do not map cleanly to the current shared RP2040/STM32G474 HAL
      goals.
    - Treat ILI9xxx GRAM readback and tearing-effect/vblank handling as later
      optional work. They need stronger SPI/MIPI-DBI read and callback
      contracts than the current display layer exposes.
    - Make display raw APIs status-returning from the start (`hal_status_t`)
      and guard shared display state with the existing display mutex pattern
      based on `jh_hal_mutex_create_once`.

- Continue STM32 backend catch-up.
  - Focus on modules that still lack real STM32G474 backends:
    `mqtt`, `ota`, `udp`, `wifi` and `wireguard`.
  - `hal_swserial` is no longer a STM32 backend gap; it lives in the shared
    driver layer over HAL GPIO/time/sync and is used by the STM32 GPS path.
  - Keep module/runtime coverage work ahead of optional polish.
  - Prefer portable HAL-level drivers over Arduino wrappers for any new device
    work.
  - Keep register access, startup, IRQ glue, SDK ownership, pin/peripheral
    bring-up and DMA in backend folders.

- Keep implementation incremental.
  - Preserve the current public API when adding richer APIs.
  - Add focused tests around each behavior before refactoring dispatch.
  - Avoid mixing build-system changes with large driver rewrites in the same
    patch.
  - Run the host/mock build and affected tests after narrow changes.
  - Prefer `./runalltests.sh` for final local signoff when toolchains are
    available.
