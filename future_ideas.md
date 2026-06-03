# JaszczurHAL — Future Architecture Improvements

Recommendations for improving the architecture, ordered by priority.

---

## 1. Eliminate `float` from wiper calculations

**Problem:** On Cortex-M0/M0+ (RP2040) there is no FPU — each float operation costs ~20–70 cycles via soft-float library.

**Solution:** Fixed-point arithmetic:

```c
// Instead of:
wiper = (uint8_t)(((float)ohms / (float)cfg->e2e_resistance) * MAX_STEPS);

// Use:
wiper = (uint8_t)((uint32_t)ohms * MAX_STEPS / cfg->e2e_resistance);
```

For 32-bit values and 8-bit result there is no overflow risk (max `100000 * 255 = 25.5M` — fits in `uint32_t`).

---

## 2. Replace `switch/case` dispatch with vtable (ops pattern)

**Problem:** Every new chip requires modifying existing code (`hal_digipot_set_resistance` has switch on chip type).

**Solution:** Linux-style ops/vtable pattern:

```c
typedef struct hal_digipot_ops {
    bool (*init)(const hal_digipot_config_t *cfg);
    bool (*set_resistance)(const hal_digipot_config_t *cfg, uint32_t ohms);
    bool (*validate)(const hal_digipot_config_t *cfg);
} hal_digipot_ops_t;

// Per-chip registration:
static const hal_digipot_ops_t mcp401x_ops = {
    .init = NULL,
    .set_resistance = mcp401x_set_resistance,
    .validate = mcp401x_validate,
};
```

Adding a new chip = new file with `_ops`, zero modifications to `hal_digipot.cpp`.

---

## 3. Separate driver files from HAL API

**Problem:** Chip logic (MCP401x, MAX5395) and public API live in one file.

**Solution:**

```
hal/
  hal_digipot.h          ← public interface (stable)
  hal_digipot.c          ← dispatch + pool management
  drivers/
    digipot_mcp401x.c    ← single chip logic
    digipot_max5395.c    ← single chip logic
```

Each driver registers its `ops` — the public file never changes.

---

## 4. Unified error handling

**Problem:** Functions return `bool` or `NULL` — no information about *why* an operation failed.

**Solution:**

```c
typedef enum {
    HAL_OK = 0,
    HAL_ERR_INVALID_CFG,
    HAL_ERR_I2C_NACK,
    HAL_ERR_I2C_TIMEOUT,
    HAL_ERR_POOL_FULL,
    HAL_ERR_VERIFY_FAIL,
} hal_status_t;

hal_status_t hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms);
```

Cost: 0 extra RAM (enum → int, which is returned anyway). Migration: add a `bool` wrapper for backward compatibility.

---

## 5. Central configuration system

**Problem:** `HAL_ENABLE_*` flags are scattered across `-D` in CMake, no single source of truth.

**Solution:** One `hal_config.cmake` or defconfig file:

```cmake
# hal_config.cmake - single place
option(HAL_ENABLE_DIGIPOT "Digital potentiometer support" OFF)
option(HAL_ENABLE_I2C     "I2C bus support" OFF)

# Automatic dependencies:
if(HAL_ENABLE_DIGIPOT)
    set(HAL_ENABLE_I2C ON CACHE BOOL "" FORCE)
endif()
```

For full scalability — integrate with Kconfiglib (Python, zero runtime dependencies).

---

## 6. Board description mechanism

**Problem:** HW configuration (I2C address, bus, e2e) is coded at runtime by the caller.

**Solution:** Even a simple header-based board description:

```c
// boards/my_board.h
#define BOARD_DIGIPOT_BUS      0
#define BOARD_DIGIPOT_ADDR     0x28
#define BOARD_DIGIPOT_E2E      50000
#define BOARD_DIGIPOT_MODE     HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL
```

Or JSON/YAML processed by CMake into a generated header — lightweight Device Tree alternative.

---

## 7. Replace static pool with linker sections

**Problem:** `s_pool[HAL_DIGIPOT_MAX_INSTANCES]` always occupies RAM regardless of actual usage.

**Solution A (simple):** Dynamic list with limit:
```c
static uint8_t s_pool_count;
// Allocate only as needed, fail when exceeded.
```

**Solution B (advanced, Zephyr-style):** Linker section + `DEVICE_DEFINE`:
```c
#define HAL_DIGIPOT_DEFINE(name, config) \
    static hal_digipot_impl_s _digipot_##name \
    __attribute__((section(".hal_digipot"))) = { .cfg = config };
```

Linker eliminates unused instances automatically.

---

## Priority summary

| # | Change | Difficulty | Gain |
|---|--------|-----------|------|
| 1 | Eliminate float | Low | High (CPU cycles) |
| 2 | Vtable/ops | Medium | High (scalability) |
| 3 | Separate driver files | Low | Medium (readability) |
| 4 | Error codes | Low | Medium (debuggability) |
| 5 | Central config | Medium | Medium (maintainability) |
| 6 | Board description | Medium | Medium (portability) |
| 7 | Linker-section pool | High | Low (RAM savings) |

Items 1–4 can be implemented incrementally without breaking the existing API.
