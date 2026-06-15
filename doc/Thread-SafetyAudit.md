# Thread-Safety Audit

Current status after the initial FreeRTOS hardening pass.

Date: 2026-06-15

## Scope

This audit classifies the current HAL/runtime thread-safety contract for
`HAL_ENABLE_FREERTOS`. It remains intentionally conservative:

- init/create/deinit/destroy paths remain single-task setup/teardown unless a
  module explicitly says otherwise;
- runtime paths protected by `hal_mutex_t` become task-safe only after the target
  `hal_sync` backend is FreeRTOS-aware;
- callbacks and ISR paths have stricter rules than normal task code;
- Arduino-origin wrappers remain suspect under preemptive scheduling even when a
  singleton mutex serializes their public wrapper API.

## Cross-Cutting Findings

| ID | Finding | Risk | Required follow-up |
|----|---------|------|--------------------|
| TS-A02 | `hal_critical_section_*` is a hard full interrupt mask, not a scheduler lock. | Using it for task mutual exclusion would harm latency and still would not be a cross-core lock on RP2040. | Keep it for timing/ISR-shared data only. Add a separate scheduler-lock primitive later if needed. |
| TS-A04 | Arduino-origin connectivity/storage libraries are serialized by HAL mutexes but may contain hidden global state or blocking behavior. | A wrapper mutex protects HAL calls, not necessarily callbacks, background tasks, LWIP/CYW43/USB interactions, or third-party internal globals. | Keep them documented as serialized wrappers; migrate protocol/device logic to shared HAL-native drivers where practical. |
| TS-A05 | Timer and interrupt callback contexts are mixed. | Code that is safe in task context may deadlock or corrupt state from ISR/alarm callbacks. | Do not change callback context silently. Document ISR/alarm vs task-service callback for every timer path before FreeRTOS timer integration. |

Implemented background, removed from the active risk list:

- scheduler-aware `hal_delay_ms()` / `hal_idle()` gating is implemented for
   RP2040 and STM32G474 FreeRTOS builds;
- singleton/per-bus lazy mutex publication is hardened through
   `jh_hal_mutex_create_once()` across the audited modules;
- `hal_i2c_slave` no longer takes HAL mutexes from Wire callbacks;
- host-side `JH_ENABLE_FREERTOS_POSIX_TESTS` exists as a CI regression layer
   for the FreeRTOS mutex/delay/create-once contract.

## Modules Requiring Special Care

Most modules now follow one of two stable patterns:

- single-task lifecycle with mutex-protected runtime access, or
- stateless/pure helpers with no FreeRTOS-specific concerns.

The modules and areas that still need explicit care are:

| Module / area | Current constraint | Why it still matters |
|---------------|--------------------|----------------------|
| `hal_sync` | `hal_mutex_*` is not ISR-safe; `hal_critical_section_*` is a hard full-mask critical section, not a scheduler lock. | Misusing critical sections for task mutual exclusion would hurt latency and still not solve cross-core ownership cleanly. |
| `hal_system` delay/idle | `hal_delay_ms()` / `hal_idle()` are scheduler-aware, but `hal_delay_us()` remains a busy-wait path. | Timing-sensitive code can still affect latency under load and needs hardware validation. |
| `hal_gpio`, `hal_pcnt`, `hal_can` | GPIO/edge/INT callbacks run in ISR context. | Callers must keep handlers minimal and avoid blocking HAL calls from ISR context. |
| `hal_timer`, `hal_timer_ext`, `hal_soft_timer`, `SmartTimers` | Callback context is mixed: ISR/alarm-style on low-level paths, caller/task context on higher-level paths. | FreeRTOS timer work must not silently change callback expectations. |
| `hal_uart`, `hal_swserial`, `hal_gps` transport ownership | Still effectively single-owner unless the application imposes a stronger transport model. | Reentrancy and shared-port ownership are not solved by mutexes alone. |
| `hal_i2c` / `hal_spi` | Bus locking exists, but multi-step transactions still depend on disciplined caller/driver lock coverage. | Releasing or skipping the bus lock at the wrong boundary can reintroduce races under multitask use. |
| `hal_i2c_slave` | Backend-local register-map lock avoids HAL mutex use in callbacks, but hardware behavior still needs smoke validation. | Callback safety is improved, but final confidence still depends on on-target behavior. |
| `hal_littlefs`, `hal_sdlogger` | Serialized wrappers around storage/file paths, with fault/reset-sensitive usage patterns. | Long blocking calls and crash/logging paths are still conservative by design. |
| `hal_display` | Drawing is serialized, but long transactions can block other tasks. | Large TFT/OLED updates may create visible latency or contention. |
| `hal_onewire`, `hal_ds18b20`, `hal_thermocouple`/MAX6675, `hal_rgb_led` | Bit-bang/timing-critical paths still rely on busy waits and/or hard critical sections. | These paths need on-hardware validation under scheduler load, not just structural locking. |
| `hal_modem_at`, `hal_simcom_a76xx` | Internal callbacks execute with stronger locking/context assumptions than normal request/response calls. | Tick and message callbacks can still deadlock or recurse if the contract drifts. |
| `hal_wifi`, `hal_udp`, `hal_mqtt`, `hal_ota`, `hal_wireguard` | Serialized wrappers over Arduino-origin libraries, not deeply audited HAL-native runtimes. | Hidden background tasks, third-party globals, and callback behavior remain the main unresolved RTOS risk. |
| `pidController`, `hal_pid_controller` | Still documented as caller-serialized, not internally thread-safe. | Shared controller instances remain an application-level responsibility. |

## Remaining Follow-Ups

1. Re-audit Arduino-origin connectivity/storage wrappers under real FreeRTOS
   workloads, with special attention to callbacks, hidden background tasks, and
   third-party global state.
2. Preserve and hardware-test timing-sensitive OneWire, RGB LED, and MAX6675
   paths under scheduler load.
3. Keep timer and IRQ callback context documented explicitly so task-context and
   ISR-context assumptions do not silently drift during future timer work.

## Current Result

The original Stage 0 concerns around scheduler-aware delay handling and lazy
singleton/per-bus mutex publication are closed in the current implementation.
The remaining risk is concentrated in third-party Arduino-origin wrappers,
timing-sensitive bit-bang paths, and callback-context boundaries that cannot be
proven safe by mutexes alone.
