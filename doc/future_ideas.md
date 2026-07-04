# JaszczurHAL - Future Ideas

Simple backlog of future architecture and implementation work.

- High priority: shared network architecture for STM32 WiFi modem first.
  - Treat STM32 connectivity as the next major platform-unlock item, not as a
    small target-specific patch.
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

- Continue FreeRTOS hardening.
  - Harden module-level synchronization and ownership.
  - Document callback contexts, task contexts and ISR/task boundaries.
  - Run hardware smoke tests before considering FreeRTOS as a default runtime.

- Expand status-returning APIs without breaking existing wrappers.
  - Shared `hal_status_t` exists; add `_ex` functions returning it where useful.
  - Keep current `bool` / `NULL` APIs as compatibility wrappers.
  - Start with statuses that can be reported accurately today.
  - Improve backend-specific I2C error mapping later.
  - Good first candidates: digipot init and set-resistance paths.

- Continue CAN API v2 follow-up work.
  - Add interrupt-driven RX/TX completion paths.
  - Consider callback-style filter ownership only when interrupts need it.
  - Add transceiver enable/standby abstraction.
  - Add stronger mock/loopback coverage for filters, CAN FD shape and states.
  - Validate the STM32G474 native FDCAN backend on hardware.
  - Keep Zephyr CAN as a reference checklist for timing, states, filters,
    error counters and transceiver handling.

- Add an ADP5360 shared I2C PMIC driver.
  - Put reusable register/protocol logic in `src/hal/impl/shared/`.
  - Use the existing shared I2C device-driver pattern.
  - Keep board policy outside the driver.

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

- Continue STM32 backend catch-up.
  - Focus on modules that still lack real STM32G474 backends:
    `mqtt`, `ota`, `swserial`, `udp`, `wifi` and `wireguard`.
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
