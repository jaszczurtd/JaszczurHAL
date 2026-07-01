# JaszczurHAL - Future Ideas

Simple backlog of future architecture and implementation work.

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

- Add STM32 WiFi through offloaded network modules.
  - Prefer module-based WiFi first, not a full native WiFi stack.
  - Use Zephyr `eswifi` and `esp_at` as design references, not direct copies.
  - Keep `hal_wifi.h` as the first integration point.
  - Consider a module flag such as `HAL_ENABLE_ESWIFI` or
    `HAL_ENABLE_WIFI_MODEM`.
  - Implement a target-neutral command engine over `hal_uart` and/or `hal_spi`.
  - Implement STA scan/connect/disconnect/status/RSSI/MAC/IP first.
  - Add AP mode second.
  - Add simple TCP/UDP client support after the WiFi facade works.
  - Add TLS only if the selected module provides TLS offload.

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
