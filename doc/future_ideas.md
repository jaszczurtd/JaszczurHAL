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

## Updated priority summary

| Priority | Change | Difficulty | Gain | Status |
|---|---|---:|---:|---|
| 0 | Move Arduino-backed drivers into shared HAL implementations | Medium | High | In progress |
| 1 | Continue FreeRTOS hardening (module-level + hardware validation) | Medium | High | In progress |
| 2 | Status-returning `_ex` APIs (partial coverage; extend to digipot) | Medium | Medium/high | Add incrementally |
| 3 | Polish legacy utility API after Arduino decoupling | Low/medium | Medium | Add incrementally |
| 4 | Header-based board descriptions | Low/medium | Medium | Optional layer |
| 5 | Linker-section/static device model | High | Low now | Backlog |

Further STM32 backend catch-up in module/runtime coverage still ranks ahead of
the optional polish items 3-5.

The safest implementation path is still incremental: preserve the current public
API, add tests around each behavior before refactoring dispatch, and avoid
changing build-system assumptions in the same patch as driver logic.
