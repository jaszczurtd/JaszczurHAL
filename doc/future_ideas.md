# JaszczurHAL - Future Architecture Improvements

Architecture notes and remaining recommendations, ordered by practical value.

---

## Recommended next work

## 0. Move Arduino-backed drivers into shared HAL implementations

This is the highest-priority preparation work for real multithreading. Many
current Arduino-backed integrations should be rewritten as target-neutral HAL
drivers that can run on both RP2040 and STM32G474.

The direction should be:

- move reusable driver logic into `src/hal/impl/shared/`;
- keep target-specific glue in the target folders only when it touches hardware,
  SDK calls, startup, IRQs, DMA, or board-specific details;
- shrink `src/hal/impl/arduino/` over time until it becomes a thin compatibility
  layer, and eventually disappears if there is no remaining Arduino-specific
  responsibility;
- avoid depending on Arduino libraries for new portable drivers;
- make shared drivers use HAL primitives for I/O, time, logging, memory policy,
  and synchronization.

This is probably more urgent than enabling FreeRTOS itself, because FreeRTOS
will expose pre-existing assumptions in Arduino libraries: single-threaded use,
implicit global state, weak synchronization, and hidden blocking behavior. Moving
drivers to shared HAL implementations lets the project define explicit locking,
ownership, and ISR/task boundaries before true preemptive scheduling is enabled.

The desired end state is that RP2040 and STM32G474 use the same driver logic
wherever the hardware protocol is the same, with only narrow per-target port
code below it.

---

## 1. Continue FreeRTOS hardening as a second priority

This remains a priority item. The initial opt-in FreeRTOS path is available on
both currently supported embedded targets: RP2040 and STM32G474. The remaining
work is module hardening, hardware smoke testing, and callback/context
documentation.

The opt-in backend uses `HAL_ENABLE_FREERTOS` rather than changing the default
runtime. The public application contract remains stable:

- `app_start()` is called once before `app_task0()` / `app_task1()` begin.
- `app_task0()` and `app_task1()` remain the client-facing API.
- On STM32G474 FreeRTOS entry builds, `app_task0()` and optional `app_task1()`
  are run from separate FreeRTOS tasks.
- On RP2040, arduino-pico owns scheduler startup and the optional secondary
  path remains `loop1()` gated by `HAL_ENABLE_APP_TASK1`.

On RP2040, Arduino-pico already provides a FreeRTOS SMP mode. When enabled, the
core creates a task pinned to core 0 for `setup()` / `loop()` and, if
`setup1()` or `loop1()` exists, another task pinned to core 1 for that path.
This means the existing HAL bridge through `setup()` / `loop()` / `loop1()` can
probably stay in place for RP2040, with the Arduino core owning scheduler
startup and core affinity.

RP2040 still should not be treated as a trivial "enable it in the core" change.
FreeRTOS mode changes synchronization and scheduling semantics: other FreeRTOS
tasks can run under the SMP scheduler, Arduino libraries may not be safe under
preemptive multithreading, and module-level state still needs a per-module
task-safety pass.

The client-facing API should be 1:1 across both targets. The FreeRTOS version
should also be kept the same for both targets. Configuration should share a
common base, with target-specific overrides only where the FreeRTOS port,
interrupt priorities, tick source, heap/stack setup, or startup code require it.

The implementation includes FreeRTOS-aware synchronization for `hal_sync`;
future work should focus on eager/safe mutex creation, owner-task patterns
where needed, and callback context documentation.

Once both targets build and run with the opt-in backend, decide whether FreeRTOS
should become the default runtime for embedded targets.

## 2. Add status-returning APIs without breaking bool wrappers

**Problem:** many HAL APIs return `bool` or `NULL`, which hides the failure
reason. For digipot this collapses invalid config, I2C NACK, read-back mismatch,
and pool exhaustion into the same result.

**Solution:** add `_ex` functions returning a shared status enum, then keep the
existing API as compatibility wrappers.

```c
typedef enum {
    HAL_OK = 0,
    HAL_ERR_INVALID_ARG,
    HAL_ERR_INVALID_CFG,
    HAL_ERR_UNSUPPORTED,
    HAL_ERR_POOL_FULL,
    HAL_ERR_I2C_NACK,
    HAL_ERR_I2C_TIMEOUT,
    HAL_ERR_VERIFY_FAIL,
} hal_status_t;

hal_status_t hal_digipot_init_ex(const hal_digipot_config_t *cfg,
                                 hal_digipot_t *out);

hal_status_t hal_digipot_set_resistance_ex(hal_digipot_t h,
                                           uint32_t ohms);
```

Migration pattern:

```c
hal_digipot_t hal_digipot_init(const hal_digipot_config_t *cfg) {
    hal_digipot_t h = NULL;
    return hal_digipot_init_ex(cfg, &h) == HAL_OK ? h : NULL;
}

bool hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms) {
    return hal_digipot_set_resistance_ex(h, ohms) == HAL_OK;
}
```

Important caveat: useful I2C-specific statuses require the lower-level I2C API
to preserve more detail than a generic `bool`. Start with status codes that can
be reported accurately today, then improve I2C mapping per backend.

**Difficulty:** medium
**Gain:** medium/high for debugging hardware failures

---

## 3. Polish the legacy utility API after Arduino decoupling

**Problem:** the direct Arduino dependency is gone, but the tools layer still
mixes several roles: numeric helpers, debug aliases, scan helpers, C/C++
compatibility, drawing helpers, timers, PID, Unity, and bundled cJSON.

That is acceptable for compatibility, but it makes `tools.h` broader than the
portable HAL boundary and less clear than the focused `hal/*` modules.

**Solution:** keep `tools` HAL-only, but split the public convenience surface
when touching those files next:

- document `#include <JaszczurHAL.h>` / `#include <hal/hal.h>` as the primary
  portable surface and `#include <tools.h>` as optional compatibility helpers,
- consider smaller utility headers such as `tools_numeric.h`,
  `tools_strings.h`, `tools_debug_aliases.h`, and `tools_network_helpers.h`,
- keep `tools_api.h` and `tools_c.h` free of C++ and Arduino-only types,
- avoid adding new convenience helpers to `tools` when they naturally belong
  in a HAL module.

**Difficulty:** low/medium
**Gain:** medium for readability and future ports

---

## 4. Optional board-description layer

**Problem:** board wiring and device constants are currently assembled by each
application at runtime. That is flexible, but repeated projects can drift.

**Solution:** add an optional header-based board description convention. Keep it
outside the HAL core so small sketches can continue to pass config structs
directly.

Example:

```c
// boards/my_board.h
#define BOARD_DIGIPOT_BUS      0
#define BOARD_DIGIPOT_ADDR     0x28
#define BOARD_DIGIPOT_E2E      50000u
#define BOARD_DIGIPOT_MODE     HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL
```

Possible helper:

```c
hal_digipot_config_t hal_board_default_digipot_config(void);
```

JSON/YAML generation can wait. A plain header gives most of the value without
adding a build-system dependency.

**Difficulty:** low/medium
**Gain:** medium for reusable boards and examples

---

## 5. Revisit static pools only when RAM pressure is proven

**Problem:** static pools reserve RAM for the configured maximum instance count.
For digipot:

```c
static hal_digipot_impl_s s_pool[HAL_DIGIPOT_MAX_INSTANCES];
```

**Current context:** this pool exists only when `HAL_ENABLE_DIGIPOT` is active,
and the default max is small. Projects can already lower or raise the compile
time cap with `HAL_DIGIPOT_MAX_INSTANCES`.

**Recommendation:** do not prioritize linker sections yet. A linker-section
device model is attractive for board-defined devices, but it is a larger
cross-toolchain change and gives little benefit for the current digipot pool.

If this becomes necessary later:

```c
#define HAL_DIGIPOT_DEFINE(name, config) \
    static hal_digipot_impl_s _digipot_##name \
    __attribute__((section(".hal_digipot"))) = { .cfg = config };
```

Before doing this, verify behavior on:

- Arduino-pico build,
- STM32G474 linker script,
- host/mock CMake tests,
- dead-code elimination with unused devices.

**Difficulty:** high
**Gain:** low until board-defined static devices exist

---

## 6. Mine selected Zephyr drivers for portable shared HAL drivers

The local `zephyr/` tree contains several drivers that are useful as design
references for JaszczurHAL. They should not be copied mechanically: Zephyr
drivers are built around devicetree, Kconfig, `struct device`, work queues, and
Zephyr's networking/device APIs. The useful part is the register-level protocol
logic and the mature shape of common driver APIs.

The safest approach is to extract small, target-neutral pieces into
`src/hal/impl/shared/`, then wrap them with JaszczurHAL configuration structs,
HAL I/O primitives, mutexes, and compatibility wrappers.

### WiFi for STM32 via offloaded network modules

The most useful material in `zephyr/drivers/wifi` for JaszczurHAL/STM32 is not
native WiFi MAC/PHY support. It is the family of offloaded network-processor
drivers where the host MCU talks to a module over UART, SPI, SDIO, or vendor
transport while the module owns TCP/IP.

The strongest candidate is `zephyr/drivers/wifi/eswifi`, for Inventek eS-WiFi.
It is close to the kind of networking support JaszczurHAL can add on STM32
without pulling in a full WiFi stack, DHCP client, WPA supplicant, and socket
stack.

Useful pieces from ES-WiFi:

- bus abstraction with UART and SPI request functions;
- UART RX ring-buffer parser with prompt detection;
- SPI command/data-ready protocol;
- shared AT response parser for responses shaped like `\r\n...\r\nOK\r\n> `;
- WiFi commands for scan, connect, disconnect, status, RSSI, MAC, and AP mode;
- socket-offload command mapping for TCP/UDP client/server operations.

Examples of command mapping:

- scan: `F0`
- connect: `C1=ssid`, `C2=pass`, `C3=security`, `C0`
- disconnect: `CD`
- status: `CS`
- RSSI: `CR`
- MAC: `Z5`
- AP mode: `A1`, `A2`, `AS`, `AC`, `Z6`, `AD`, `AE`
- sockets: `P0` select socket, `P1` protocol, `P2` local port, `P3` remote IP,
  `P4` remote port, `P5` server start/stop, `P6` client start/stop, `S3` send,
  `R0`/`R1`/`R2` receive.

Zephyr-specific pieces to avoid in the first pass:

- `net_if`, `net_pkt`, `net_context`, and socket offload registration;
- Zephyr work queues and semaphores;
- devicetree/Kconfig plumbing;
- direct reuse of Zephyr's shell integration.

Recommended JaszczurHAL shape:

- keep the existing `hal_wifi.h` facade as the first integration point;
- add a STM32 backend selected by a new module flag such as
  `HAL_ENABLE_ESWIFI` or a more generic `HAL_ENABLE_WIFI_MODEM`;
- implement a target-neutral command engine over `hal_uart` and/or `hal_spi`;
- implement STA connect/disconnect/status/RSSI/MAC/IP/scan first;
- add AP mode second;
- add simple TCP/UDP client APIs or a small `hal_net_client_*` layer after the
  WiFi facade works;
- add TLS only when the selected module provides TLS offload.

Other WiFi drivers worth studying:

- `esp_at`: practical for ESP8266/ESP32 AT modules; good RX/parser concepts,
  though it uses Zephyr's modem command infrastructure heavily.
- `winc1500`: useful for Microchip WINC1500, but depends on Microchip/ASF
  driver code.
- `simplelink`: useful as another socket-offload pattern.
- `siwx91x`: useful for API shape around STA/AP/scan/socket/power-save.

Less useful for JaszczurHAL/STM32 startup work:

- `esp32`: for ESP32 as the target, not STM32 hosting a module.
- `nrf_wifi`, `nxp`, and `infineon/airoc`: valuable only if those exact vendor
  WiFi stacks and buses become project goals.

**Difficulty:** medium/high
**Gain:** high for STM32 networking

---

### CAN API and STM32G474 native FDCAN direction

The current public CAN API is intentionally small, but it now has the first
backend-selection seam in place. `hal_can_create()` and
`hal_can_create_with_retry()` take `hal_can_config_t`, with
`HAL_CAN_BACKEND_MCP2515` as the only implemented backend for now. The MCP2515
config carries `spi_bus`, `cs_pin`, `bitrate_hz`, `oscillator_hz`,
`one_shot_tx`, and `sleep_wakeup`, so call sites no longer bake MCP2515
construction details directly into the create function signature.
The compile-time backend flag is `HAL_ENABLE_MCP2515`, which propagates both
`HAL_ENABLE_CAN` and `HAL_ENABLE_SPI`; the CAN facade itself no longer pulls in
SPI.

The compatibility surface still supports `id + len + data`, polling receive, a
simple two-standard-ID filter helper, and retry/interrupt setup helpers. Both
the Arduino-style and STM32G474 backends currently wrap the shared MCP2515
driver; the STM32G474 backend does not yet use the MCU's native FDCAN
peripheral.

Current MCP2515 backend-selector status:

- done: `hal_can_backend_t`, `hal_can_mcp2515_config_t`, `hal_can_config_t`;
- done: default config helper for 500 kbps / 8 MHz MCP2515 on SPI bus 0;
- done: RP2040/Arduino, STM32G474, and mock implementations accept config;
- done: `HAL_ENABLE_MCP2515` owns the SPI dependency instead of
  `HAL_ENABLE_CAN`;
- done: MCP2515 default config moved into the MCP2515 backend area;
- done: MCP2515-specific init/send/receive/filter operations moved into
  `impl/shared/mcp2515/hal_can_mcp2515.cpp`, while target `hal_can.cpp` remains
  the public facade/dispatcher;
- done: bitrate and oscillator mapping for the MCP2515 driver constants;
- done: one-shot TX and sleep-wakeup moved from hardcoded behavior to config;
- not done: any backend other than MCP2515;
- not done: a real native STM32G474 FDCAN implementation.

Zephyr's CAN subsystem is useful in two ways:

1. As a model for a richer common CAN API.
2. As a guide for a future native STM32G474 FDCAN backend.

Useful common API concepts from `zephyr/include/zephyr/drivers/can.h`:

- `can_frame`: ID, DLC, flags, payload, optional timestamp.
- frame flags: extended ID, RTR, CAN FD, BRS, ESI.
- `can_filter`: ID, mask, flags.
- `can_frame_matches_filter()`.
- DLC/byte conversion helpers for CAN FD.
- controller modes: normal, loopback, listen-only, one-shot, triple-sampling,
  manual recovery, FD.
- controller states: error-active, error-warning, error-passive, bus-off,
  stopped.
- TX/RX error counters.
- start/stop semantics.
- bitrate and timing calculation.

Recommended JaszczurHAL API evolution:

- treat the current config-based create API as stage 1 of the CAN backend
  split;
- add a richer frame/filter API as stage 2:
  `hal_can_frame_t`, `hal_can_filter_t`, `hal_can_mode_t`,
  `hal_can_state_t`, `hal_can_error_counters_t`;
- keep `hal_can_send()` and `hal_can_receive()` as compatibility wrappers for
  classic 8-byte data frames;
- add `hal_can_send_frame()` and `hal_can_receive_frame()`;
- add `hal_can_add_filter()` / `hal_can_remove_filter()` or a simpler static
  filter API;
- add `hal_can_get_state()` and `hal_can_get_error_counters()`;
- add `hal_can_set_mode()`, `hal_can_start()`, and `hal_can_stop()` when the
  backend can support them;
- add helpers:
  `hal_can_dlc_to_bytes()`, `hal_can_bytes_to_dlc()`,
  `hal_can_frame_matches_filter()`, and frame ID validation.

MCP2515 backend improvements suggested by Zephyr:

- keep MCP2515 as a normal backend behind `hal_can_config_t`, not as the public
  API shape;
- support extended 29-bit IDs and RTR frames in the public HAL API;
- expose listen-only, loopback, one-shot, and sleep/normal modes;
- expose MCP2515 error state and TX/RX error counters already present in the
  shared driver;
- replace `hal_can_set_std_filters(id0, id1)` over time with mask/filter
  primitives;
- keep the old function as a convenience wrapper.

STM32G474 native FDCAN direction:

- add a new backend enum value, likely `HAL_CAN_BACKEND_STM32_FDCAN`, only when
  the first usable native implementation exists;
- add a `hal_can_stm32_fdcan_config_t` branch to `hal_can_config_t` for FDCAN
  instance, RX/TX pins or AF mapping policy, nominal bitrate, mode, and
  transceiver-control options;
- Zephyr's `can_stm32_fdcan.c` describes the STM32G4 FDCAN mapping relative to
  Bosch M_CAN and cites the STM32G4 reference manual behavior;
- Zephyr's shared `can_mcan.c` shows the core flow for start/stop, timing,
  filters, TX, RX FIFO handling, interrupts, bus-off recovery, and error
  counters;
- a JaszczurHAL port should not copy the full Zephyr stack, but can use it as a
  checklist and register-behavior reference;
- first useful native backend target: classic CAN, nominal bitrate, RX FIFO0,
  TX FIFO/queue, standard and extended filters, state/error counters;
- CAN FD support can be a second stage.

Also useful:

- `can_loopback.c` is a good reference for a stronger mock/loopback backend
  with filters and state.
- `transceiver/can_transceiver_gpio.c` is a small, portable idea for handling
  external transceiver enable/standby pins.
- `can_shell.c` is useful as a reference for human-readable CAN frame printing
  and test commands.

Less useful initially:

- PCI/Linux/vendor-specific drivers such as Kvaser, native Linux, NXP, Nordic,
  Renesas, and Numaker unless those exact targets become goals.
- `mcp251xfd` and `tcan4x5x` are interesting only if external CAN FD over SPI
  becomes a hardware goal.

**Difficulty:** medium for API/helpers, high for native STM32G474 FDCAN
**Gain:** high for diagnostics, portability, and STM32G474 capability

---

## Updated priority summary

| Priority | Change | Difficulty | Gain | Status |
|---|---|---:|---:|---|
| 0 | Move Arduino-backed drivers into shared HAL implementations | Medium | High | In progress |
| 1 | Continue FreeRTOS hardening (module-level + hardware validation) | Medium | High | In progress |
| 2 | Status-returning `_ex` APIs (partial coverage; extend to digipot) | Medium | Medium/high | Add incrementally |
| 3 | CAN API v2 and STM32G474 native FDCAN direction | Medium/high | High | Started: backend config in place, MCP2515 only |
| 4 | STM32 WiFi via offloaded modules (`eswifi` / `esp_at`) | Medium/high | High | Backlog |
| 5 | ADP5360 shared I2C PMIC driver | Medium | Medium/high | Backlog |
| 6 | Polish legacy utility API after Arduino decoupling | Low/medium | Medium | Add incrementally |
| 7 | Header-based board descriptions | Low/medium | Medium | Optional layer |
| 8 | Linker-section/static device model | High | Low now | Backlog |

Further STM32 backend catch-up in module/runtime coverage still ranks ahead of
the optional polish items 6-8.

The safest implementation path is still incremental: preserve the current public
API, add tests around each behavior before refactoring dispatch, and avoid
changing build-system assumptions in the same patch as driver logic.
