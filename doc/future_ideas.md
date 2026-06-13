# JaszczurHAL - Future Architecture Improvements

Architecture notes and remaining recommendations, ordered by practical value.

---

## Already addressed / no longer future work

### Fixed-point digital-potentiometer calculations

**Status:** implemented in `src/hal/impl/shared/digipot/`.

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

### Digipot driver split and ops table

**Status:** implemented.

The public digipot module now owns only handle-pool management, per-instance
locking, and backend selection. Chip-specific logic lives in shared
target-neutral drivers:

```text
src/hal/
  hal_digipot.h
  hal_digipot.cpp
  impl/shared/digipot/
    hal_digipot_ops.h
    digipot_mcp401x.cpp
    digipot_max5395.cpp
```

`hal_digipot.cpp` stores `const hal_digipot_ops_t *ops` per instance and
dispatches through the internal ops contract. MCP401x and MAX5395 keep their own
validation, init, set-resistance, step-count, I2C frame sequence and wiper math,
so adding another digipot chip should require a new shared chip implementation
and public enum/config extension rather than editing existing chip code.

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

### Shared Arduino-free MAX6675 thermocouple driver

**Status:** implemented in `src/hal/impl/shared/max6675/max6675_driver.*`.

The MAX6675 backend no longer uses the old Arduino `MAX6675` class,
`Arduino.h`, `digitalWrite()`, `digitalRead()`, or Arduino timing calls. The
shared driver bit-bangs the MAX6675 16-bit read through JaszczurHAL primitives:

- `hal_gpio_set_mode()` / `hal_gpio_write()` / `hal_gpio_read()`,
- `hal_delay_us()`,
- the per-instance `hal_thermocouple` mutex in each backend wrapper.

RP2040 now calls this shared driver from the existing Arduino backend wrapper,
and STM32G474 has its own `hal_thermocouple` wrapper for MAX6675. The public
MAX6675 config remains source-compatible (`sclk_pin`, `cs_pin`, `miso_pin`).

`HAL_ENABLE_MAX6675` now propagates only `HAL_ENABLE_THERMOCOUPLE`; it no longer
pulls in `HAL_ENABLE_SPI`, because the MAX6675 path does not use HAL SPI.

### Shared Arduino-free MCP9600/MCP9601 thermocouple driver

**Status:** implemented in `src/hal/impl/shared/mcp9600/mcp9600_driver.*`.

The MCP9600/MCP9601 backend no longer uses the old Arduino
`Adafruit_MCP9600` / `Adafruit_MCP9601` classes, `TwoWire`, `Wire`, or BusIO.
The shared driver keeps the working register logic from the Arduino backend and
uses JaszczurHAL primitives instead:

- `hal_i2c_write_read_bus()` for register-pointer reads with repeated start,
- `hal_i2c_begin_transmission_bus()` / `hal_i2c_write_bus()` /
  `hal_i2c_end_transmission_bus()` for writes,
- per-driver `hal_mutex_t` locking around read/modify/write sequences.

Both RP2040 and STM32G474 `hal_thermocouple` wrappers now use this shared
driver for MCP9600/MCP9601. The old Arduino driver folder was removed; upstream
attribution and the BSD notice live in the shared driver source.

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

## 6. C runtime (newlib) retarget layer for STM32G474 — Tier 1 implemented

**Status:** Tier 1 implemented on 2026-06-13 in
`src/hal/impl/stm32g474/port/runtime/stm32g474_syscalls.c`.

This was STM32 parity catch-up work rather than a nice-to-have. RP2040 inherits
a working C runtime from arduino-pico/pico-sdk; STM32G474 now has the matching
project-owned baseline for `printf`/stdio output, a bounded newlib heap, and
thread-safe newlib `malloc` under FreeRTOS task concurrency.

This section captures the conclusions from the FreeRTOS audit follow-up on the
STM32G474 firmware link warnings, the architectural analysis of where libc
syscalls belong, the functional retarget roadmap, and a survey of ready-made code
that could be adopted (6.9).

### 6.1 Trigger and root cause

Linking any STM32G474 firmware (e.g. `examples/29_freertos_smoke_stm32g474`,
`01_blink_stm32g474`) emitted a cluster of linker warnings:

```text
warning: _getpid is not implemented and will always fail
warning: _isatty / _kill / _lseek / _read / _write is not implemented ...
```

These are **not** missing-symbol errors. They originate from `libnosys`
(pulled in by `--specs=nosys.specs`): its fallback syscall stubs carry
`.gnu.warning.<symbol>` sections, so the linker emits the warning whenever
`libc_nano` references the symbol — even though `--gc-sections` often discards
the reference afterwards (hence the "does not take linker garbage collection
into account" note). No `-w`-style flag can suppress a `.gnu.warning` section.

### 6.2 Tier 1 implementation (applied)

The clean way to remove the warnings is to provide project-owned strong
definitions of those syscalls, so the linker resolves them from our object and
never pulls the warning-carrying `libnosys` members. The runtime retarget file is
now explicit:

```text
src/hal/impl/stm32g474/port/runtime/stm32g474_syscalls.c
```

Tier 1 now provides:

- `_write` for stdout/stderr through `g474_debug_uart_putc`, including
  `\n` -> `\r\n` translation;
- `_read` for stdin through `g474_debug_uart_getc_nonblock()`;
- `_sbrk` over linker symbols `end`, `_estack`, and `_Min_Stack_Size`, with an
  8-byte aligned bump pointer and a static heap/stack collision guard;
- `__malloc_lock` / `__malloc_unlock`, using `vTaskSuspendAll()` /
  `xTaskResumeAll()` under `HAL_ENABLE_FREERTOS`;
- UART-console semantics for `_close`, `_fstat`, `_isatty`, and `_lseek`;
- `_exit` and `_kill` as controlled system reset requests through `SCB_AIRCR`.

The runtime file is wired into STM32 firmware executable builds only. It is not
part of `libJaszczurHAL.a`, and it is not compiled into host sanity builds.

### 6.3 Architectural conclusion: syscalls are a runtime/BSP tier, not HAL

The original warning-only stubs were not the full fix; the deeper point is
**layering**. libc
syscalls (`_write`, `_read`, `_sbrk`, `_exit`, ...) sit at the very bottom of
the C runtime — below libc, called from `printf`, `assert`, `abort`, fault
handlers, and *before the scheduler starts*. They form a **runtime / BSP /
retarget tier** whose dependencies must point **only downward** onto bare-metal
primitives:

```text
  Application
      |
  libc (printf, malloc, time)
      |   <- syscalls: _write/_read/_sbrk/_exit       (RUNTIME / retarget tier)
      v
  bare-metal primitives in port/ : g474_debug_uart, linker symbols,
                                    SCB_AIRCR, systick, exception_info
      ^
      |   <- hal_serial / hal_debug                    (HAL tier, app-facing)
  Application (when it wants synchronized logging)
```

Key rule: **`_write` and `hal_serial` are siblings**, both standing on the same
bare-metal primitive (`g474_debug_uart`), neither standing on the other. Routing
`_write` through `hal_serial_*` would invert the dependency (bottom of the stack
calling the application-facing HAL module) and would break context-safety,
because `hal_serial` takes a FreeRTOS `hal_mutex_t`: that asserts/blocks when
`printf` is called from an ISR, a fault handler, a HAL critical section, or
before `vTaskStartScheduler()`. The lowest write primitive must be callable from
any context.

Confirming evidence in-tree: `g474_debug_uart.h` already documents that it is
"kept independent of the higher-level hal_uart/hal_serial so the fault-dump path
has no allocation or driver dependencies", and `hal_serial.cpp` itself is a
*consumer* of `g474_debug_uart_*`. The HAL layer wraps bare-metal upward; it must
never become a dependency of the runtime/bare-metal tier, nor be used to patch a
lower-level link warning.

Corollary on the missing layer: the bare-metal primitives already exist
(`g474_debug_uart`, linker symbols, `SCB_AIRCR`, systick, `exception_info`).
The named runtime/retarget tier now lives under `port/runtime/` and owns
syscalls while obeying the downward-only dependency rule.

### 6.4 Guard correctness

The retarget TU must be guarded by `JH_STM32G474_HW` (STM32G474 **and** an ARM
compile), not `HAL_TARGET_STM32G474`. The host "does-it-compile" build
(`build_stm32_host`) defines `HAL_TARGET_STM32G474` but runs on glibc, which owns
`_write`/`_sbrk`/etc.; defining ours there would clash. `JH_STM32G474_HW` is
precisely the bare-metal marker, matching the primitive (`g474_debug_uart`) the
tier stands on. The implemented runtime file uses this guard.

### 6.5 Functional retarget roadmap

**Tier 1 — implemented:**

- `_write` (fd 1/2) -> `g474_debug_uart_putc` with `\n`->`\r\n`. Makes `printf`,
  `puts`, `fprintf(stderr, ...)` work. Shares the UART *sink* with `hal_serial`,
  not its lock.
- `_read` (fd 0) -> a **new bare-metal** `g474_debug_uart_getc_nonblock()` added
  to the primitive (RX/PA3 is already wired). Enables `getchar`/`scanf`. This is
  the right way to "add a lower layer": extend the primitive downward, never
  reach sideways/up to `hal_serial`.
- `_sbrk` -> bump allocator over the linker symbols `end` / `_estack` /
  `_Min_Stack_Size` (0x800), returning `-1`/`ENOMEM` on exhaustion. Needed by
  `malloc` (e.g. cJSON) with a real heap/stack-collision guard.
- `__malloc_lock` / `__malloc_unlock` -> `vTaskSuspendAll()` / `xTaskResumeAll()`
  under `HAL_ENABLE_FREERTOS`. Without this, `malloc` from two tasks corrupts the
  newlib heap. The suspend/resume pair is nestable and safe before the scheduler
  starts; it is the canonical FreeRTOS pattern (do not port pico's mutex-based
  lock).
- char-device stubs `_isatty`=1 (fd 0/1/2), `_fstat`=`S_IFCHR`, `_lseek`=`ESPIPE`,
  `_close`=0 — for a UART console these *are* the correct semantics, not empty
  placeholders.
- `_exit` / `_kill` -> controlled reset via `SCB_AIRCR` (`VECTKEY|SYSRESETREQ`,
  a bare-metal register), instead of hanging.

**Tier 2 — useful, decide per need:**

- `_times` / `clock()` -> `stm32g474_systick_millis()` (bare-metal, clean).
- Wall-clock `_gettimeofday` would want `hal_rtc_get_epoch()`, but `hal_rtc` is a
  HAL module — using it would repeat the same inversion. Clean options: (a) add a
  bare-metal RTC register accessor in `port/` and base `_gettimeofday` on it, or
  (b) do not provide `_gettimeofday` in the runtime tier and leave wall-clock to
  the application via `hal_rtc`. Do **not** pull `hal_rtc` into the runtime tier.
- Per-task `errno`/stdio reentrancy: `FreeRTOSConfig.h` currently sets
  `configUSE_NEWLIB_REENTRANT 0`, so `errno`, `strtok`, `rand`, and stdio state
  are neither per-task nor protected. Setting it to `1` makes the GCC ARM_CM4F
  port swap `_impure_ptr` per context switch, at a cost of ~96-150 B RAM per task.
  A deliberate RAM-vs-correctness decision.

### 6.6 What to adopt from the RP2040 / pico-sdk path

Verdict: **little to nothing is portable 1:1, and RP2040 must not retarget at
all.**

- pico-sdk is not vendored in this repo (it arrives via arduino-cli, outside the
  tree), and project rules forbid bringing SDK ownership into `impl/shared`.
- pico-sdk's `_write`/`_sbrk`/locks are dual-core/SMP + pico-spinlock + pico
  `stdio_driver_t` specific; stripping that machinery leaves the generic
  libgloss/Cube pattern you would write for STM32 anyway. It is BSD-3-Clause, so
  legally adoptable with attribution, but not worth a literal port.
- On RP2040 you must **not** define these syscalls — arduino-pico/pico-sdk already
  owns them; overriding causes duplicate scheduler/USB/newlib-lock symbols. There
  is no shared syscall code across targets; only the *contract* "printf works" is
  shared, realized per-target on its own bare-metal.
- The one idea worth adopting later is the `stdio_driver_t` multiplexer pattern
  (register UART/USB drivers, `_write` dispatches) — relevant only once STM32 adds
  USB CDC; overkill for a single UART today.

### 6.7 Interleaving and thread-safety, within correct layering

`_write` and `hal_serial` share the **hardware sink**, not a lock.
`g474_debug_uart_putc` is byte-atomic (poll TXE, write TDR), so the worst case is
interleaved *lines*, not corrupted bytes. Raw `printf` is for debug/early-boot/
fault output and may interleave; synchronized application logging should use
`hal_derr` / `hal_serial`, which own the mutex. Pushing a blocking lock down into
the primitive would only recreate the context-safety problem — synchronization
belongs to the HAL tier, not the runtime primitive.

### 6.8 Implemented structure and wiring

```text
src/hal/impl/stm32g474/port/
  g474_debug_uart.{c,h}     # primitive (+ new getc_nonblock for _read)
  stm32g474_regs.h          # primitive (SCB_AIRCR, USART)
  system_stm32g474.c        # primitive (systick)
  exception_info.{c,h}      # primitive (fault capture + reset)
  runtime/
    stm32g474_syscalls.c    # retarget tier: depends downward only, guarded by JH_STM32G474_HW
```

The runtime file is linked into the **firmware** executable (the example
`add_executable`, and analogously any external firmware), never into the static
`libJaszczurHAL.a` and never into the host sanity build.

### 6.9 Ready-made code survey (what to adopt)

Adoption is constrained by license: JaszczurHAL bundles BSD/MIT code, so adopted
sources must be permissive (BSD/MIT/Apache), **never GPL**. The project's own
license is not stated explicitly in `library.properties`; this should be pinned
down before vendoring any third-party runtime code.

Why "never GPL", concretely: JaszczurHAL is a library statically linked into
arbitrary (including closed/commercial) firmware. GPL is strong copyleft, so
incorporating GPL code would force the *entire combined work* — the library plus
every downstream application that links it — to be released under GPL with full
corresponding source on distribution, which defeats the purpose of a reusable
HAL. On an MCU even LGPL is awkward, because its "allow the user to relink"
requirement assumes dynamic linking or shipped object files, whereas everything
here is static. This is also why the relevant ecosystem is already permissive
(newlib/libgloss use BSD-like licenses, ST Cube is BSD-3) — so they *can* be
linked into proprietary products. Precise rule: avoid plain GPL/LGPL **without a
linking/runtime exception**; code under "GPL + linking exception" (e.g. the GCC
runtime) is fine, so a "GPL" header still needs to be checked for an exception.

Candidates surveyed:

1. **STM32CubeIDE `syscalls.c` + `sysmem.c` — best fit, clean license.**
   BSD-3-Clause (STMicroelectronics). `syscalls.c` defines the full stub set
   (`_write/_read/_close/_fstat/_isatty/_lseek/_getpid/_kill/_exit/_open/...`);
   `_write` calls a weak `__io_putchar`, i.e. the routing is left to us — which is
   exactly our layering (`__io_putchar` -> `g474_debug_uart_putc`, downward to the
   primitive). `sysmem.c` provides a basic `_sbrk` over `end` but **without**
   heap/stack collision detection. Verdict: adopt the *pattern* (~30 lines), not
   the whole files; back `__io_putchar` with our primitive.

2. **Dave Nadler `heap_useNewlib_ST.c` — de-facto standard for newlib+FreeRTOS,
   but license-ambiguous and a bigger change.** Provides `_sbrk_r/_sbrk`,
   `__malloc_lock/unlock`, `__env_lock/unlock`, and routes `pvPortMalloc` ->
   newlib `malloc` (one shared pool, heap_3 style); requires
   `configUSE_NEWLIB_REENTRANT=1`. Two blockers: (a) the GitHub file header is
   BSD-style but nadler.com states "All Rights Reserved" and the repo has no
   LICENSE file — must be clarified before adoption; (b) it **replaces heap_4**
   with a unified newlib pool, a larger architectural change than our plan.

3. **uOS++ `_sbrk` (Liviu Ionescu; cnoviello "Mastering STM32" repo).** Has
   proper collision detection (`_Heap_Begin`/`_Heap_Limit`, 4-byte alignment).
   uOS++ is MIT, but the file header shows only a copyright line — confirm at the
   source before reuse.

4. **newlib `libgloss`** — the canonical reference stubs (newlib BSD-like
   license). Good as a pattern reference, not for literal vendoring.

5. **FreeRTOS itself** — only supplies the `configUSE_NEWLIB_REENTRANT` +
   `__getreent` mechanism; it ships **no** `_sbrk`/syscalls. The community points
   to Nadler for that.

Two strategies and what to take from each:

| | A. Minimal (= Tier 1, recommended) | B. Unified heap + reentrancy |
|---|---|---|
| Heap | heap_4 (FreeRTOS) + separate newlib `_sbrk` (cJSON/printf) | one pool: `pvPortMalloc` -> newlib `malloc` |
| Adopt | ST `syscalls.c`/`sysmem.c` *pattern* (BSD-3) + own `__malloc_lock`=`vTaskSuspendAll` (3 lines) | Nadler `heap_useNewlib` wholesale |
| `configUSE_NEWLIB_REENTRANT` | 0 (or 1 optionally) | **1 required** (RAM/task cost) |
| Change size | small, leaves heap_4 intact | large, replaces heap_4 |
| License | clean (BSD-3 ST + own code) | ambiguous (Nadler) |

**Implemented recommendation:** ready-made code exists, but the layered/minimal
approach was implemented without vendoring a third-party module:
- `_write`/`_read`/char-stubs/`_exit`: ST `syscalls.c` pattern, with
  `__io_putchar` -> `g474_debug_uart_putc` (stays in `port/runtime/`, downward);
- `_sbrk`: our own over `end`/`_estack`/`_Min_Stack_Size` (better collision guard
  than ST `sysmem.c`; detection pattern like uOS++);
- `__malloc_lock`/`__malloc_unlock`: write our own (`vTaskSuspendAll`/
  `xTaskResumeAll`) — 3 lines, zero license risk, no Nadler dependency.

Consider Nadler only if a unified pool + full reentrancy is deliberately wanted,
and only after the license is clarified. A practical research caveat (mbed-os
#9542; Nadler): `_sbrk` collision checks against the live MSP are unreliable, and
ST's USB stack calls `malloc` from an ISR — both argue for the **static** limit
(`_estack - _Min_Stack_Size`) proposed here rather than a live-SP comparison.

Sources: STM32CubeL4 `syscalls.c` (BSD-3-Clause); ST Community thread confirming
`sysmem.c` is BSD-3-Clause; DRNadler/FreeRTOS_helpers `heap_useNewlib_ST.c` and
nadler.com/embedded/newlibAndFreeRTOS.html; FreeRTOS docs on
`configUSE_NEWLIB_REENTRANT`; MCU on Eclipse FreeRTOS+newlib articles;
cnoviello/uOS++ `_sbrk.c`; ARMmbed/mbed-os issue #9542.

**Difficulty:** low (Tier 1) / medium (Tier 2 reentrancy + bare-metal RTC)
**Gain:** medium/high — working `printf`/stdio, a safe real heap, thread-safe
`malloc` under FreeRTOS, and an explicit, correctly-layered runtime tier

---

## Updated priority summary

| Priority | Change | Difficulty | Gain | Status |
|---|---|---:|---:|---|
| Done | Fixed-point digipot arithmetic | Low | High | Implemented |
| Done | Digipot driver split + ops table | Medium | High | Implemented |
| Done-ish | Central `HAL_ENABLE_*` config | Medium | Medium | Implemented, polish remains |
| Done | Arduino-free tools layer | Low/medium | Medium/high | Implemented |
| Done | STM32 newlib retarget Tier 1 (`_write`/`_read`/`_sbrk`/malloc-lock) — §6 | Low | High | Implemented |
| 1 | Status-returning `_ex` APIs | Medium | Medium/high | Add incrementally |
| 2 | Polish legacy utility API after Arduino decoupling | Low/medium | Medium | Add incrementally |
| 3 | Header-based board descriptions | Low/medium | Medium | Optional layer |
| 4 | Linker-section/static device model | High | Low now | Backlog |

STM32 parity note: the newlib retarget tier in §6 is now closed at Tier 1.
Further STM32 backend catch-up in module/runtime coverage still ranks ahead of
the optional polish items 3-5.

The safest implementation path is still incremental: preserve the current public
API, add tests around each behavior before refactoring dispatch, and avoid
changing build-system assumptions in the same patch as driver logic.
