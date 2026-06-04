# JaszczurHAL - Future Architecture Improvements

Architecture notes and remaining recommendations, ordered by practical value.

---

## Already addressed / no longer future work

### Fixed-point digital-potentiometer calculations

**Status:** implemented in `src/hal/hal_digipot.cpp`.

The digipot wiper calculations should stay integer-only. RP2040 has no FPU,
so even simple `float` expressions pull in soft-float helpers and cost extra
cycles on every resistance update.

Rules preserved from the previous implementation:

- voltage-divider calculations truncate toward zero,
- MAX5395 rheostat calculations truncate toward zero,
- MCP401x rheostat calculations round to nearest tap when the remainder is
  strictly greater than half a step.

The current supported resistance range is small enough for `uint32_t`
intermediate multiplication:

```c
100000u * 255u == 25500000u
```

That fits comfortably in `uint32_t`, so there is no need for `uint64_t` or
fixed-point scaling helpers wider than the values already used by the API.

### Central compile-time configuration

**Status:** mostly implemented in `src/hal/hal_config.h` and documented in
`src/HAL_FLAGS.txt`.

The repo now has a single project-level configuration hook:

```c
// project-local, auto-included when present
#include "hal_project_config.h"
```

`HAL_ENABLE_*` flags are opt-in, dependencies are propagated automatically,
and generic facade modules emit compile-time errors when enabled without a
backend. Examples:

- `HAL_ENABLE_MCP401X` -> `HAL_ENABLE_DIGIPOT`, `HAL_ENABLE_I2C`
- `HAL_ENABLE_MAX5395` -> `HAL_ENABLE_DIGIPOT`, `HAL_ENABLE_I2C`
- `HAL_ENABLE_DISPLAY` requires `HAL_ENABLE_TFT` or `HAL_ENABLE_SSD1306`

Remaining useful polish:

- document one canonical `hal_project_config.h` workflow for Arduino sketches,
  host tests, and bare-metal builds,
- optionally add CMake presets or a small generated `hal_build_config.h` for
  non-Arduino projects,
- consider Kconfig only if the flag matrix becomes too large to audit by hand.

### Arduino-free tools layer

**Status:** implemented.

The `src/utils` tools layer no longer depends directly on Arduino platform
headers or Arduino-only public types. The former hard dependencies were moved
behind HAL APIs:

- WiFi scanning now uses `hal_wifi_scan_networks()` and
  `hal_wifi_get_scan_result()`.
- SD/crash logging moved out of `tools` into `hal_sdlogger`, with the
  Arduino-specific `SD.h` / `SPI.h` code contained in
  `src/hal/impl/arduino/frameworks/sdlogger/`.
- Tools declarations use C/portable types such as `const char *`, `bool`,
  fixed-width integers, and caller-provided buffers rather than Arduino
  `String`, `File`, or `SPISettings`.
- `src/utils/tools.cpp`, `SmartTimers`, `pidController`,
  `multicoreWatchdog`, and `draw7Segment` call HAL APIs for time, delays,
  ADC, WiFi, display, watchdog, and logging.

Current caveats are compatibility-level rather than Arduino dependencies:

- `hal_config.h` still provides non-Arduino `PROGMEM` and `F()` fallbacks.
  They are central compatibility macros for bundled Arduino-origin drivers and
  host builds, not a tools-specific dependency.
- `src/arduino_host_stubs/` still exists for host diagnostics of Arduino
  backend code, but `test_tools` and `test_multicoreWatchdog` no longer need
  those stubs on their include path.
- The tools layer remains a legacy/convenience layer. New application logic
  should still prefer direct `hal/*` APIs when possible.

---

## Recommended next work

## 1. Split digipot chip logic and introduce an ops table

**Problem:** `hal_digipot.cpp` currently owns pool management, public dispatch,
MCP401x logic, and MAX5395 logic. Adding a third chip still means editing the
same central file.

**Solution:** combine the previous "vtable" and "separate driver files" ideas
into one refactor. The public module should own handles, locking, and dispatch;
chip files should own validation, init, set-resistance, and step-count logic.

Suggested shape:

```text
src/hal/
  hal_digipot.h              public interface
  hal_digipot.cpp            handle pool + dispatch
  impl/shared/digipot/
    hal_digipot_ops.h        internal ops contract
    digipot_mcp401x.cpp      MCP401x implementation
    digipot_max5395.cpp      MAX5395 implementation
```

Internal contract:

```c
typedef struct hal_digipot_ops {
    bool (*validate)(const hal_digipot_config_t *cfg);
    bool (*init)(const hal_digipot_config_t *cfg);
    bool (*set_resistance)(const hal_digipot_config_t *cfg, uint32_t ohms);
    uint16_t (*step_count)(void);
} hal_digipot_ops_t;
```

Each instance stores `const hal_digipot_ops_t *ops`, so the public
`hal_digipot_set_resistance()` no longer needs a chip switch. Adding a new chip
still requires extending the public enum/config surface, but it should not
require editing the existing chip implementations.

**Difficulty:** medium
**Gain:** high once more digipot chips are added

---

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
| Done | Fixed-point digipot arithmetic | Low | High | Implemented |
| Done-ish | Central `HAL_ENABLE_*` config | Medium | Medium | Implemented, polish remains |
| Done | Arduino-free tools layer | Low/medium | Medium/high | Implemented |
| 1 | Digipot driver split + ops table | Medium | High | Next architecture step |
| 2 | Status-returning `_ex` APIs | Medium | Medium/high | Add incrementally |
| 3 | Polish legacy utility API after Arduino decoupling | Low/medium | Medium | Add incrementally |
| 4 | Header-based board descriptions | Low/medium | Medium | Optional layer |
| 5 | Linker-section/static device model | High | Low now | Backlog |

The safest implementation path is still incremental: preserve the current public
API, add tests around each behavior before refactoring dispatch, and avoid
changing build-system assumptions in the same patch as driver logic.
