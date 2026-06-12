# FreeRTOS Implementation Notes

This file captures the FreeRTOS design decisions agreed during the planning
discussion, plus the implementation plan. It is intended as a compact context
restore document for future agent/Codex sessions.

## Current Decision

FreeRTOS support should be enabled by an opt-in compile-time flag:

```c
#define HAL_ENABLE_FREERTOS
```

This flag must not introduce a new public HAL API layer such as
`hal_rtos_*`. Applications should be able to use native FreeRTOS APIs directly:

```c
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
```

The HAL should instead make its existing primitives FreeRTOS-aware internally:

- `hal_mutex_*`
- `hal_critical_section_*`
- `hal_delay_ms`
- `hal_idle`
- entry-point dispatch in `hal_app_entry.cpp`
- selected timer behavior, where appropriate

The desired user-facing result is:

- client code can use normal FreeRTOS APIs;
- client code can keep using JaszczurHAL APIs;
- runtime HAL APIs are safe to use from multiple FreeRTOS tasks, within the
  module-specific thread-safety contract;
- application code does not need to care whether the target is RP2040 or
  STM32G474.

## Target Strategy

### RP2040

The current RP2040 backend uses Earle Philhower's arduino-pico core. That core
already includes a FreeRTOS SMP integration and should be used for the first
FreeRTOS implementation.

For RP2040, `HAL_ENABLE_FREERTOS` should enable or validate the arduino-pico
FreeRTOS mode:

- arduino-pico board menu: `Operating System -> FreeRTOS SMP`
- effective compile define: `__FREERTOS`
- likely FQBN form: `rp2040:rp2040:rpipico:os=freertos`

The local/vendored `FreeRTOS-Kernel` must be ignored on RP2040 while the
arduino-pico backend is active. Trying to link a separate local kernel into the
Arduino runtime would risk duplicate scheduler/startup/kernel symbols and
conflicts with USB, LWIP, CYW43, newlib locks, and the core's own boot flow.

Important local facts from arduino-pico 5.4.0:

- FreeRTOS SMP is part of the core.
- FreeRTOS mode creates a task for `setup()` / `loop()` on core 0.
- If `setup1()` or `loop1()` exists, it creates a task for that path on core 1.
- `delay()` maps to `vTaskDelay()`.
- `yield()` maps to `taskYIELD()`.
- tick quantum is 1 ms.
- priorities are 0..7.
- the core also creates/supports USB and LWIP related tasks.
- docs warn that many Arduino libraries are not written for preemptive
  multithreading and should be serialized by the application or HAL.
  Shared HAL-native drivers are expected to be easier to make FreeRTOS-safe
  than Arduino-origin wrappers, but they still require module-level audit.

### STM32G474

For STM32G474, `HAL_ENABLE_FREERTOS` should pull in a local FreeRTOS kernel
dependency. The recommended dependency layout is:

```text
third_party/FreeRTOS-Kernel/
```

Use the Cortex-M4F GCC port:

```text
FreeRTOS-Kernel/portable/GCC/ARM_CM4F/
```

Use exactly one heap implementation, initially:

```text
FreeRTOS-Kernel/portable/MemMang/heap_4.c
```

The STM32 integration must provide:

- `FreeRTOSConfig.h`
- explicit CMake source list, no `GLOB_RECURSE` over FreeRTOS
- SVC/PendSV/SysTick ownership by FreeRTOS
- malloc-failed and stack-overflow hooks
- correct NVIC priority configuration
- consistent Cortex-M4F flags across HAL, app, startup, and FreeRTOS:
  `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`

For STM32G474, `configPRIO_BITS` should be 4.

### Future RP2040 Without Arduino

The first implementation should not replace arduino-pico on RP2040. However,
the design should leave room for a future bare-metal RP2040 backend that could
use the same local `third_party/FreeRTOS-Kernel` dependency as STM32.

That means:

- keep FreeRTOS kernel dependency separate from public HAL APIs;
- keep target startup, IRQ, tick, heap, and port details in backend-specific
  folders;
- make the public application contract target-neutral.

## Public Application Contract

The application-facing entry contract remains:

```c
void app_start(void);
void app_task0(void);
void app_task1(void); /* optional weak default */
```

Desired FreeRTOS semantics:

- `app_start()` runs once before application tasks begin.
- `app_task0()` runs repeatedly in one FreeRTOS task.
- `app_task1()` runs repeatedly in a second FreeRTOS task when intentionally
  enabled/defined.
- The application should not write `main()`, `setup()`, `loop()`, or `.ino`
  glue when using the HAL-provided entry path.

Current state to remember:

- `src/hal_app_entry.cpp` maps RP2040 to Arduino `setup()` and `loop()`.
  It emits `loop1()` only when `HAL_ENABLE_APP_TASK1` is defined.
- STM32 currently runs `app_task0()` and `app_task1()` cooperatively in one
  bare-metal loop only when `HAL_ENABLE_APP_TASK1` is defined.
- RP2040 examples currently generate a `.ino` that calls only `app_start()`
  and `app_task0()`.
- Defining `loop1()` on arduino-pico is not harmless. It starts the second
  core/task path even if the weak `app_task1()` does nothing.

The `app_task1()` policy is therefore explicit: `HAL_ENABLE_APP_TASK1` controls
secondary task dispatch. Avoid silently starting a second RP2040 core/task only
because a weak empty symbol exists.

## HAL Runtime Semantics Under FreeRTOS

`HAL_ENABLE_FREERTOS` should mean:

1. Native FreeRTOS headers and symbols are available to the application.
2. HAL internals use RTOS-safe primitives where blocking or locking is needed.
3. Existing HAL APIs remain source-compatible.

It should not mean:

- a new public HAL wrapper around FreeRTOS;
- hiding FreeRTOS from the application;
- replacing arduino-pico FreeRTOS on RP2040 during the first implementation;
- promising that every init/deinit path is safe from arbitrary tasks.

### `hal_sync`

Current state:

- RP2040: normal builds use `pico/mutex`. `HAL_ENABLE_FREERTOS + __FREERTOS`
  builds use FreeRTOS mutexes for `hal_mutex_*`. Critical sections stay
  per-core, nesting-safe `save_and_disable_interrupts()` /
  `restore_interrupts()` hard interrupt masks in both modes.
- STM32G474: spinlock-style mutex and PRIMASK-based critical sections.

> Note on the STM32 spinlock mutex: it is NOT a practical hazard in the current
> design. STM32G474 is single-core, and the busy-wait `__atomic_test_and_set`
> path is compiled in ONLY when `HAL_ENABLE_FREERTOS` is undefined. Under the
> flag, `hal_mutex_*` selects the FreeRTOS path (priority-aware mutex/semaphore),
> so the spinlock never coexists with the preemptive scheduler that would make
> it dangerous (priority inversion). The path-selection rule is the safety
> mechanism; the spinlock remains a correct primitive for the non-RTOS,
> cooperative super-loop build.

FreeRTOS target state:

- General rule: we are not removing the existing synchronization mechanisms or
  delay handling. Instead, we are adding compile-time path selection using
  HAL_ENABLE_FREERTOS.
- In task context, `hal_mutex_*` should use FreeRTOS mutex/semaphore primitives
  (compile-time path selection using HAL_ENABLE_FREERTOS).
- Lock/unlock must not be used from ISR unless the API is explicitly extended
  or documented for ISR use.
- Lazy mutex creation patterns must be audited so two tasks cannot allocate or
  initialize the same singleton mutex at the same time.

#### Decision: two separate critical-section primitives

`hal_critical_section_*` must NOT be a thin alias for `taskENTER_CRITICAL()`.
The two have different masking strength and serve different needs:

- `hal_critical_section_*` stays a **hard, full-mask** primitive (PRIMASK on
  STM32, `save_and_disable_interrupts()` on RP2040). It masks *all* interrupts.
  This is what cycle-accurate bit-banging needs - e.g. OneWire timing, where the
  recent deadlock fix relies on no interrupt firing during the windowed delay.
  `taskENTER_CRITICAL()` only masks up to `configMAX_SYSCALL_INTERRUPT_PRIORITY`,
  so a higher-priority IRQ could still preempt and wreck the timing. Aliasing the
  two would silently reintroduce that class of bug under FreeRTOS.
- A **separate scheduler-lock** primitive (new API, e.g. `hal_sched_lock_*`)
  maps to `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` (or the FromISR variant)
  and is used where the goal is "don't let the scheduler switch tasks", not
  "freeze the hardware". Module code that only needs short mutual exclusion
  against other tasks should prefer a `hal_mutex_t` or this scheduler-lock, never
  the full hard mask.

Both `hal_critical_section_*` implementations are now nesting-safe (depth-counted,
state saved on the outermost enter and restored only on the outermost exit;
RP2040 counts depth per-core). This is required before any FreeRTOS work because
nested HAL calls inside a critical section must not re-enable interrupts early.

### `hal_delay_ms` and `hal_delay_us`

`hal_delay_ms()` in FreeRTOS task context should use:

```c
vTaskDelay(pdMS_TO_TICKS(ms));
```

Fallback behavior is still needed:

- before the scheduler starts;
- if FreeRTOS is not enabled;
- in contexts where blocking is illegal.

`hal_delay_us()` should remain a short busy-wait style primitive where required
by timing-sensitive code, for example OneWire timing. It must not depend on a
timer interrupt that cannot fire inside a critical section.

### Timers

Current RP2040 `hal_timer` uses pico SDK alarm pools and callbacks execute in
an interrupt-like context. STM32 timer support is still limited.

Do not casually change timer callback context. If FreeRTOS software timers are
introduced, document whether callbacks run:

- in ISR/hardware alarm context;
- in the FreeRTOS timer service task;
- in an application task after notification/queue dispatch.

The existing `hal_timer` contract should remain compatible until a deliberate
timer migration is designed.

### SmartTimers

`SmartTimers` is already a useful reference consumer:

- it uses `hal_millis()`;
- it serializes state through `hal_mutex_t`;
- callbacks are invoked outside the mutex.

If `hal_sync` becomes FreeRTOS-aware, `SmartTimers` should become usable from
multiple tasks without changing its public API.

## Thread-Safety Contract

Avoid over-promising. The practical FreeRTOS contract should be:

- initialization, creation, deinitialization, and destruction should still be
  treated as single-task setup/teardown unless a module explicitly documents
  otherwise;
- runtime APIs that already use `hal_sync` should become task-safe once
  `hal_sync` is FreeRTOS-aware;
- modules currently documented as not thread-safe must either be upgraded with
  locking or remain documented exceptions;
- callbacks and ISR contexts have a stricter contract than normal task code;
- Arduino-origin libraries remain suspect under preemptive scheduling and must
  be serialized or migrated to shared HAL-native drivers.

## Relevant Project Rules

Keep these constraints in mind during implementation:

- Preserve strict shared-vs-target boundaries.
- Do not place target register access, startup, ISR glue, SDK ownership,
  board policy, or FreeRTOS port/startup ownership in `impl/shared`.
- `impl/shared` is for target-neutral logic that depends on HAL contracts and
  behaves identically across targets.
- Treat `hal_config.h` as the source of truth for flag semantics.
- Sync [`src/HAL_FLAGS.txt`](../src/HAL_FLAGS.txt), [README.md](../README.md),
  and [JaszczurHAL_API.md](JaszczurHAL_API.md) after adding or changing
  `HAL_ENABLE_FREERTOS`.
- Keep optional modules optional. Guard declarations, implementation TUs,
  vendored includes, and source lists with the correct flags.
- Avoid new Arduino dependencies in portable utilities or shared driver logic.
- Do not edit vendored Arduino-origin code unless the task explicitly requires
  it.
- Zero tolerance for warnings in project-owned code.
- Full local signoff is `./runalltests.sh`; for meaningful changes it should
  pass before work is considered done.
- Hard progress rule: every completed FreeRTOS-related implementation,
  documentation, audit, or validation step must be recorded in the "Done"
  section of this document before the task is considered closed. Each entry
  should include the date, the stage/P-item, the concrete outcome, and the
  validation status.
- Audit context rule: any agent executing a FreeRTOS-related task must read
  [Thread-SafetyAudit.md](Thread-SafetyAudit.md) before planning or editing, and
  must explicitly take its findings, risk IDs, module classifications, and
  priority follow-ups into account while choosing scope, implementation order,
  and validation.

## Implementation Plan

Each stage should add real functionality while preserving current default
behavior when `HAL_ENABLE_FREERTOS` is not defined.

### Stage 0: Existing Module Thread-Safety Audit

Goal: identify the modules and API paths that are already safe for concurrent
runtime use, the paths that need locking fixes, and the paths that should remain
documented as single-task/single-context only.

Tasks:

- Review every module currently documented as thread-safe, multicore-safe, or
  not thread-safe.
- Classify each module's public API into:
  - setup/teardown paths that remain single-task;
  - runtime paths that are already protected by `hal_sync`;
  - runtime paths that need new locking;
  - ISR/callback paths with restricted API use;
  - Arduino-origin wrappers that need serialization or replacement.
- Pay special attention to lazy singleton mutex creation, static pools, shared
  bus ownership, callback context, and APIs that call blocking delays.
- Produce a short audit table in separate file
  ([Thread-SafetyAudit.md](Thread-SafetyAudit.md))
  before changing runtime behavior.

Functional value:

- prevents `HAL_ENABLE_FREERTOS` from becoming a broad promise without a module
  inventory;
- gives the implementation stages a concrete fix list;
- preserves current behavior while making the thread-safety gaps explicit.

Validation:

- no runtime validation required for the audit itself;
- documentation should identify each exception or follow-up module clearly;
- later stages should close or explicitly document every item discovered here.

### Stage 1: Flag and Documentation Skeleton

Goal: make the configuration model explicit without changing runtime behavior.

Tasks:

- Add `HAL_ENABLE_FREERTOS` to `hal_config.h` documentation/comments.
- Add it to `src/HAL_FLAGS.txt`.
- Add API/docs notes to [README.md](../README.md),
  [JaszczurHAL_API.md](JaszczurHAL_API.md), and
  [lib_compilation.md](lib_compilation.md).
- Document that RP2040 uses arduino-pico FreeRTOS and STM32 uses local
  `FreeRTOS-Kernel`.
- Add compile-time validation:
  - RP2040 + `HAL_ENABLE_FREERTOS` should require `__FREERTOS` or produce a
    clear error/instruction.
  - STM32 + `HAL_ENABLE_FREERTOS` should require the local kernel path to be
    present/configured.

Functional value:

- users get a clear supported flag and failure mode;
- no default behavior changes.

Validation:

- host build/tests;
- RP2040 non-FreeRTOS static library build remains unchanged;
- STM32 non-FreeRTOS static library build remains unchanged.

### Stage 2: RP2040 Build Integration

Goal: make `HAL_ENABLE_FREERTOS` usable on RP2040 through arduino-pico.

Tasks:

- Extend RP2040 example/build scripts to support an opt-in FreeRTOS build mode,
  likely by selecting FQBN `os=freertos` or passing the equivalent core option.
- Ensure static-library CMake sees `__FREERTOS` and the arduino-pico FreeRTOS
  include path.
- Do not compile local `third_party/FreeRTOS-Kernel` for RP2040.
- Add a small RP2040 FreeRTOS smoke example or build profile that includes
  `<FreeRTOS.h>` and `<task.h>`.

Functional value:

- RP2040 applications can include and call native FreeRTOS API.
- Current arduino-pico integration remains the runtime owner.

Validation:

- RP2040 normal build still passes.
- create a minimal FreeRTOS RP2040 example with `FreeRTOS.h` and `task.h`,
  compile it.

### Stage 3: FreeRTOS-Aware RP2040 HAL Primitives

Goal: make existing HAL runtime primitives safe in RP2040 FreeRTOS task context.

Tasks:

- Update `hal_sync` for RP2040 under `__FREERTOS` to use FreeRTOS-aware locking
  or verify and document the arduino-pico mutex bridge behavior.
- Update `hal_critical_section_*` to use the correct FreeRTOS primitives under
  `__FREERTOS`.
- Make `hal_delay_ms()` explicitly task-aware in FreeRTOS mode if relying on
  Arduino `delay()` is not sufficient.
- Preserve `hal_delay_us()` busy-wait behavior for timing-sensitive code.
- Audit singleton/lazy mutex creation in HAL modules used on RP2040.

Functional value:

- existing HAL APIs become safer when called from multiple FreeRTOS tasks on
  RP2040.

Validation:

- host tests;
- RP2040 FreeRTOS compile smoke;
- targeted review of `SmartTimers`, I2C, SPI, EEPROM/KV, display, GPS, and
  network modules.

### Stage 4: STM32 FreeRTOS Kernel Integration

Goal: provide native FreeRTOS API availability for STM32G474.

Tasks:

- Add `third_party/FreeRTOS-Kernel` as a documented dependency or submodule.
- Update STM32 CMake to compile the explicit kernel source list when
  `HAL_ENABLE_FREERTOS` is defined:
  - `tasks.c`
  - `queue.c`
  - `list.c`
  - `timers.c`
  - `event_groups.c`
  - `stream_buffer.c`
  - `portable/GCC/ARM_CM4F/port.c`
  - `portable/MemMang/heap_4.c`
- Add STM32 FreeRTOS include dirs.
- Add STM32 `FreeRTOSConfig.h` with `configPRIO_BITS = 4`.
- Add malloc-failed and stack-overflow hooks.
- Ensure SVC/PendSV/SysTick vector ownership is correct.

Functional value:

- STM32 applications can include and call native FreeRTOS API.

Validation:

- STM32 static library builds with `HAL_ENABLE_FREERTOS`.
- A minimal FreeRTOS example created previously for RP2040 compiles also on STM32.

### Stage 5: STM32 HAL Primitives Under FreeRTOS

Goal: make existing STM32 HAL runtime APIs task-safe under FreeRTOS.

Tasks:

- Update STM32 `hal_sync` under `HAL_ENABLE_FREERTOS` to use FreeRTOS mutexes.
- Update critical-section handling for scheduler and ISR contexts.
- Update `hal_delay_ms()` to use `vTaskDelay()` when scheduler is running and
  task context is valid.
- Keep pre-scheduler fallback delays.
- Audit lazy singleton locks and static pools for task-safe runtime access.

Functional value:

- JaszczurHAL runtime APIs become usable from STM32 FreeRTOS tasks.

Validation:

- host tests where possible;
- STM32 FreeRTOS build;
- smoke example with two tasks both using a small set of HAL APIs.

### Stage 6: FreeRTOS Kernel Acquisition

Goal: make the STM32 FreeRTOS kernel dependency reproducible and available to
users/CI without tracking the kernel sources in this repository.

Tasks:

- Add `freertos_core_version.conf` next to `arduino_core_version.conf` as the
  single source of truth for the FreeRTOS kernel dependency:
  - `FREERTOS_KERNEL_REPO`
  - `FREERTOS_KERNEL_REF`
  - optional informational `FREERTOS_KERNEL_VERSION`
  - default `FREERTOS_KERNEL_DIR=third_party/FreeRTOS-Kernel`
- Initially pin the STM32 kernel to the same FreeRTOS compatibility point as
  the configured arduino-pico/RP2040 core. If the RP2040 core uses a snapshot
  such as `V11.1.0+`, prefer an exact upstream commit/ref over a moving branch
  and document the compatibility note in the config.
- Add `scripts/ensure_freertos_kernel.sh` as the shared dependency helper,
  preferably with a thin CMake wrapper, that:
  - reads `freertos_core_version.conf`;
  - does nothing unless `HAL_ENABLE_FREERTOS` is requested, or unless a setup
    command explicitly forces the fetch;
  - clones/checks out the kernel into `third_party/FreeRTOS-Kernel`;
  - verifies the required kernel, port, and heap paths;
  - honors `JH_FREERTOS_KERNEL_DIR` for user/CI-provided checkouts;
  - fails with a clear diagnostic when the checkout is missing, mismatched, or
    unavailable offline.
- Add `third_party/FreeRTOS-Kernel/` to `.gitignore` so the fetched kernel never
  becomes a tracked repo artifact.
- Wire the helper into:
  - `runmefirst.sh`;
  - `scripts/build_stm32_lib.sh` when `--freertos` is used or
    `EXTRA_HAL_DEFINES` contains `HAL_ENABLE_FREERTOS`;
  - the `examples` STM32 CMake path before FreeRTOS sources/include dirs are
    required.
- Keep `HAL_ENABLE_FREERTOS` as the only public FreeRTOS feature flag. Do not
  introduce `HAL_FREERTOS_ENABLED`.
- Remove `JH_STM32_FREERTOS` as a user-facing FreeRTOS switch. Presets and
  scripts should propagate FreeRTOS intent through
  `EXTRA_HAL_DEFINES=HAL_ENABLE_FREERTOS`; CMake should detect that define and
  call `jh_stm32g474_enable_freertos(...)`.
- Run the fetch/configure step before compilation starts, because CMake needs
  the FreeRTOS source list and include paths during configuration.

Functional value:

- fresh user and CI checkouts can build STM32 FreeRTOS targets without a manual
  `git clone`, while the kernel remains outside the tracked repository;
- the FreeRTOS kernel version is pinned in one visible place;
- `HAL_ENABLE_FREERTOS` remains the single semantic opt-in flag across code,
  scripts, examples, and docs.

Validation:

- non-FreeRTOS builds do not fetch or require `third_party/FreeRTOS-Kernel`;
- fresh checkout with `HAL_ENABLE_FREERTOS` fetches the configured kernel and
  builds the STM32 static library;
- `examples` STM32 FreeRTOS smoke target builds from a clean `third_party`;
- `JH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel` still works without touching
  `third_party`;
- offline/missing-kernel failure remains explicit and actionable;
- `git status --ignored` shows the fetched kernel is ignored.

### Stage 7: FreeRTOS Entry-Point Mode

Goal: make `app_start`, `app_task0`, and `app_task1` transparent across targets.

Tasks:

- Define stack size and priority defaults for `app_task0` and `app_task1`.
- Allow overrides through compile definitions, for example:
  - `HAL_FREERTOS_TASK0_STACK`
  - `HAL_FREERTOS_TASK1_STACK`
  - `HAL_FREERTOS_TASK0_PRIORITY`
  - `HAL_FREERTOS_TASK1_PRIORITY`
- STM32: `hal_app_entry.cpp` should create tasks and start the scheduler when
  both `HAL_PROVIDE_APP_ENTRY` and `HAL_ENABLE_FREERTOS` are defined.
- RP2040: preserve arduino-pico scheduler ownership. Decide whether task1 is
  represented through `loop1()` or a HAL-created task, but keep it gated by
  `HAL_ENABLE_APP_TASK1` so an empty weak default cannot start the second path.
- Keep the final `app_task1()` opt-in policy documented.

Functional value:

- portable applications can use the same app contract on RP2040 and STM32 in
  FreeRTOS mode.

Validation:

- minimal dual-task example builds for both targets;
- current non-FreeRTOS examples remain unchanged.

### Stage 8: Module Audit and Hardening

Goal: make the thread-safety promise real module by module.

Tasks:

- Audit all modules documented as thread-safe or not thread-safe.
- Add missing locks to runtime paths where needed.
- Keep init/deinit contracts conservative unless deliberately upgraded.
- Pay special attention to:
  - I2C/SPI bus locking
  - EEPROM/KV
  - display
  - GPS/UART
  - modem/AT sessions
  - LittleFS/SDFS/SD logger
  - timers and callbacks
  - WireGuard/MQTT/WiFi Arduino-origin code
- Update per-module API docs with FreeRTOS-specific notes.

Functional value:

- the FreeRTOS support becomes more than "it compiles"; it becomes usable for
  real multi-task applications.

Validation:

- host tests;
- `./runalltests.sh`;
- RP2040 and STM32 example builds;
- hardware smoke tests where available.

### Stage 9: Hardware Smoke Layer

Goal: verify timing and concurrency on physical boards.

Tasks:

- Add simple hardware smoke apps/tests for:
  - GPIO
  - RGB LED
  - I2C
  - SPI
  - UART
  - timers/delay
- Run each in single-task and two-task FreeRTOS configurations.
- Confirm RP2040 RGB LED invariant: after PIO init the pin must remain in PIO
  function and must not be reconfigured to SIO GPIO.
- Validate STM32 RGB LED cycle-timed GPIO path under scheduler load.

Functional value:

- catches electrical/timing failures that host/static gates cannot catch.

Validation:

- hardware logs or checklist entries per board.

## Open Questions

- Answered 2026-06-11: `app_task1()` dispatch requires `HAL_ENABLE_APP_TASK1`.
  On RP2040 this gates `loop1()` emission and prevents accidental core/task
  startup.
- Should HAL mutexes be normal or recursive under FreeRTOS? Current code should
  be audited before choosing.
- Should `hal_timer` remain hardware-alarm based in FreeRTOS mode, or should a
  separate task-context timer mode be added later?
- Answered/planned for Stage 6: STM32 should pin the same FreeRTOS kernel
  compatibility point as the configured arduino-pico/RP2040 core in
  `freertos_core_version.conf`. Prefer an exact upstream commit/ref over a
  moving branch; if the RP2040 core exposes only a snapshot-style version such
  as `V11.1.0+`, record the exact chosen ref and compatibility note in the
  config.
- How should CI validate `HAL_ENABLE_FREERTOS` without requiring hardware?
  Answer: add a host-side FreeRTOS test build using the kernel's POSIX port,
  `portable/ThirdParty/GCC/Posix`, with `portable/MemMang/heap_4.c`. It runs a
  real FreeRTOS scheduler as host threads, so the FreeRTOS-aware `hal_sync` path
  (priority mutex, scheduler-lock), the lazy-mutex audit fixes, and `SmartTimers`
  used from two tasks can all be regression-tested in `ctest` without a board.
  This complements - does not replace - the mock backend: mock tests cover the
  non-RTOS path, the POSIX-port tests cover the `HAL_ENABLE_FREERTOS` path. The
  existing `hal_critical_section` nesting tests already run on the mock backend.

## Prioritized Recommendations (review follow-ups)

Follow-ups raised during design review, ordered by priority. Items already
resolved are listed under "Done" for traceability.

### P0 - do before enabling the flag anywhere

- **Make the OneWire / timing path keep the hard full-mask under FreeRTOS.**
  Guaranteed by the two-primitive decision above, but must be explicitly
  validated: confirm OneWire (and any other cycle-timed bit-bang) calls
  `hal_critical_section_*` (full mask), never the scheduler-lock. Add a
  hardware smoke check under scheduler load (Stage 9).

### P1 - prerequisite for correct FreeRTOS runtime behavior

- **Resolve the lazy singleton-mutex race (`ensureMutex`).** `SmartTimers` and
  any module using check-then-create lazy mutexes can have two tasks both see
  `_mutex == NULL` and create two different mutexes (leak + no mutual exclusion).
  Recommended fix, consistent with the "init = single-task" contract: create the
  mutex eagerly in the constructor / init path, before the object is shared
  across tasks - removes the race without locking the lazy path. Cheaper and
  safer than double-checked locking. SmartTimers was closed in Stage 5; broader
  singleton/bus mutex cleanup remains module-hardening work.

- **Gate `hal_delay_ms()` on scheduler state.** In FreeRTOS mode use
  `vTaskDelay(pdMS_TO_TICKS(ms))` only when
  `xTaskGetSchedulerState() == taskSCHEDULER_RUNNING`; otherwise fall back to the
  busy/Arduino delay (pre-scheduler init, ISR/critical context). Without the
  gate, a delay called before `vTaskStartScheduler()` misbehaves. Closed for
  RP2040 in Stage 3 and STM32G474 in Stage 5.

### P2 - decide during implementation

- **Normal vs recursive HAL mutex under FreeRTOS.** Audit current call sites for
  re-entrant lock acquisition before choosing. Default to non-recursive unless an
  audited path needs recursion; recursion hides lock-ordering bugs. RP2040 Stage
  3 uses a normal non-recursive FreeRTOS mutex, matching the previous pico mutex
  contract.

- **`hal_timer` callback context under FreeRTOS.** Keep hardware-alarm based for
  now; if a task-context timer mode is added later, document the callback context
  explicitly (ISR/alarm vs timer-service task vs app task).

### Done

- 2026-06-12: Stage 6 FreeRTOS kernel acquisition completed.
  Added `freertos_core_version.conf` as the single source of truth for the
  STM32 FreeRTOS-Kernel dependency, pinned to the exact FreeRTOS-Kernel
  submodule commit used by arduino-pico 5.4.0 (`V11.1.0+` compatibility point).
  Added `scripts/ensure_freertos_kernel.sh`, which is a no-op unless FreeRTOS
  is requested or setup forces it, fetches/verifies the checkout, honors
  `JH_FREERTOS_KERNEL_DIR`, checks the Cortex-M4F port and `heap_4` paths, and
  gives explicit offline/mismatch diagnostics. `third_party/FreeRTOS-Kernel/`
  is ignored. `runmefirst.sh`, `scripts/build_stm32_lib.sh`, and the STM32
  examples CMake path run the helper before configuration needs FreeRTOS
  sources. `JH_STM32_FREERTOS` was removed as the user-facing switch; STM32
  FreeRTOS intent now flows through `EXTRA_HAL_DEFINES=HAL_ENABLE_FREERTOS`.
  Per [Thread-SafetyAudit.md](Thread-SafetyAudit.md), Stage 6 changes
  dependency acquisition only; lazy singleton mutex hardening remains Stage 8.
  Validation: `bash -n scripts/ensure_freertos_kernel.sh`,
  `bash -n scripts/build_stm32_lib.sh`, `bash -n runmefirst.sh`,
  `git diff --check`, no-op helper run without FreeRTOS intent, normal STM32
  configure/build without FreeRTOS, default helper fetch into ignored
  `third_party/FreeRTOS-Kernel`, `git check-ignore -v
  third_party/FreeRTOS-Kernel`, legacy `-DJH_STM32_FREERTOS=ON` configure
  failure with the migration diagnostic, `JH_FREERTOS_KERNEL_DIR=/tmp/...`
  external-checkout configure, `./scripts/build_stm32_lib.sh --clean
  --freertos -o /tmp/jh_stage6_build_stm32_freertos -j 4`,
  `29_freertos_smoke_stm32g474` build, and `./runalltests.sh -j4` passed all
  7 gates. STM32 ELF links still emit the existing newlib/nosys syscall
  warnings from the bare-metal toolchain.
- 2026-06-12: Stage 5 STM32 HAL primitives under FreeRTOS completed.
  STM32G474 `hal_mutex_*` now selects a normal non-recursive FreeRTOS mutex
  under `HAL_ENABLE_FREERTOS`, rejects ISR lock/unlock use, and keeps the
  non-FreeRTOS build on the existing single-core atomic spinlock.
  `hal_critical_section_*` remains the nesting-safe PRIMASK hard full-mask and
  exposes an internal active-state check so delay/idle can avoid blocking in
  critical context. `hal_delay_ms()` now uses
  `vTaskDelay(pdMS_TO_TICKS(ms))` only when the scheduler is running, the caller
  is in task context, and the core is outside HAL critical sections; otherwise
  it falls back to an interrupt-independent DWT cycle busy-wait. `hal_idle()`
  yields only in the same legal task context, and `hal_delay_us()` remains a
  DWT cycle busy-wait on hardware FreeRTOS builds. `stm32g474_systick_*()` uses
  the normal or FromISR FreeRTOS tick getter according to IPSR context.
  SmartTimers now creates its per-instance mutex eagerly and keeps an atomic
  create-once fallback, closing the TS-A01 SmartTimers lazy-mutex race; broader
  singleton/bus lazy mutex cleanup remains Stage 8/module-hardening work.
  Documentation was synced in [Thread-SafetyAudit.md](Thread-SafetyAudit.md),
  [JaszczurHAL_API.md](JaszczurHAL_API.md), [lib_compilation.md](lib_compilation.md),
  [README.md](../README.md), [examples/README.md](../examples/README.md),
  [`src/HAL_FLAGS.txt`](../src/HAL_FLAGS.txt), `hal_config.h`, and
  `hal_sync.h`. Validation: `git diff --check`,
  `bash -n scripts/build_stm32_lib.sh`, normal STM32 static-library build, and
  `./runalltests.sh -j4` passed all 7 gates. A FreeRTOS build without
  `third_party/FreeRTOS-Kernel` failed fast with the expected missing-kernel
  diagnostic. A temporary `/tmp/jh_stage5_FreeRTOS-Kernel` checkout
  successfully built
  `./scripts/build_stm32_lib.sh --clean --freertos --freertos-kernel
  /tmp/jh_stage5_FreeRTOS-Kernel -o /tmp/jh_stage5_build_stm32_freertos -j 4`
  and `29_freertos_smoke_stm32g474` using the same kernel path.
- 2026-06-11: Stage 4 STM32 FreeRTOS kernel integration completed.
  STM32G474 builds now have a documented local
  `third_party/FreeRTOS-Kernel` dependency (or `JH_FREERTOS_KERNEL_DIR`
  override), a shared CMake helper with an explicit source list for
  `tasks.c`, `queue.c`, `list.c`, `timers.c`, `event_groups.c`,
  `stream_buffer.c`, `portable/GCC/ARM_CM4F/port.c`, and
  `portable/MemMang/heap_4.c`, plus STM32 include dirs for the kernel,
  ARM_CM4F port, and target config. Added
  `src/hal/impl/stm32g474/freertos/FreeRTOSConfig.h` with
  `configPRIO_BITS = 4`, NVIC priority macros, malloc-failed and
  stack-overflow hooks, and Cortex-M handler aliases so FreeRTOS owns
  SVC/PendSV/SysTick in FreeRTOS builds. `system_stm32g474.c` no longer
  installs/configures the HAL SysTick timebase under `HAL_ENABLE_FREERTOS`,
  and `startup_stm32g474.c` expects the FreeRTOS port handlers instead of weak
  default handlers in that mode. `scripts/build_stm32_lib.sh --freertos`,
  `JH_STM32_FREERTOS`, the `stm32g474-freertos` examples preset, and
  `examples/29_freertos_smoke` now cover the STM32 native FreeRTOS compile
  path. Per [Thread-SafetyAudit.md](Thread-SafetyAudit.md), this stage is
  kernel/API availability only: STM32 `hal_mutex_*`, `hal_delay_ms()`, and
  `hal_idle()` remain Stage 5 runtime primitive work. Validation:
  `git diff --check`, `bash -n scripts/build_stm32_lib.sh`,
  `./scripts/build_stm32_lib.sh --help`, STM32 normal static-library
  configure/build,
  normal STM32 `01_blink_stm32g474` example build, expected fail-fast configure
  without `third_party/FreeRTOS-Kernel`, `./runalltests.sh -j4` passed all
  7 gates, and a temporary `/tmp` FreeRTOS-Kernel checkout successfully built
  both `./scripts/build_stm32_lib.sh --clean --freertos --freertos-kernel
  /tmp/jh_stage4_FreeRTOS-Kernel -o /tmp/jh_stage4_build_stm32_freertos -j 4`
  and `29_freertos_smoke_stm32g474`.
- 2026-06-11: Stage 3 RP2040 FreeRTOS-aware HAL primitives completed.
  `hal_mutex_*` now selects a normal non-recursive FreeRTOS mutex
  (`xSemaphoreCreateMutex`) under `HAL_ENABLE_FREERTOS` + `__FREERTOS`,
  rejects ISR use, and keeps the normal RP2040 build on pico `mutex_t`.
  `hal_delay_ms()` now uses `vTaskDelay(pdMS_TO_TICKS(ms))` only when the
  scheduler is running, the caller is in task context, and the current core is
  outside `hal_critical_section_*`; otherwise it falls back to pico busy-wait.
  `hal_idle()` yields in the same safe task context, and `hal_delay_us()`
  remains interrupt-independent busy-wait. `hal_critical_section_*` was
  deliberately kept as the existing nesting-safe, per-core hard interrupt mask,
  not a FreeRTOS scheduler lock. `examples/29_freertos_smoke` now creates a
  second FreeRTOS task and exercises HAL mutex/delay/idle behavior. Per
  [Thread-SafetyAudit.md](Thread-SafetyAudit.md), TS-A03 is closed for RP2040,
  while TS-A01 lazy singleton mutex creation, TS-A04 Arduino-origin wrappers,
  and TS-A05 timer callback context remain module-hardening follow-ups.
  Validation: `git diff --check`,
  `./scripts/build_arduino_lib.sh --clean --freertos`,
  `cmake --preset rp2040-freertos -S examples`,
  `cmake --build build_examples_rp2040_freertos --target 29_freertos_smoke_rp2040`,
  and `./runalltests.sh` passed all 7 gates.
- 2026-06-11: Stage 2 RP2040 build integration completed. RP2040 static-library
  builds now have an opt-in `./scripts/build_arduino_lib.sh --freertos` mode that
  selects arduino-pico FreeRTOS SMP, defines `HAL_ENABLE_FREERTOS`, exposes
  `__FREERTOS`, and adds the core FreeRTOS include path without compiling any
  local `third_party/FreeRTOS-Kernel` sources. RP2040 examples now have an
  opt-in `JH_RP2040_FREERTOS=ON` / `rp2040-freertos` preset path that appends
  `os=freertos` to the FQBN and compiles
  `examples/29_freertos_smoke` with native `<FreeRTOS.h>` and `<task.h>`.
  Documentation was synced in [README.md](../README.md),
  [JaszczurHAL_API.md](JaszczurHAL_API.md),
  [lib_compilation.md](lib_compilation.md), `examples/README.md`, and
  [`src/HAL_FLAGS.txt`](../src/HAL_FLAGS.txt). Per
  [Thread-SafetyAudit.md](Thread-SafetyAudit.md), this stage deliberately only
  wires build/header availability; `hal_sync`, delay, timers, Arduino wrappers,
  and runtime task-safety semantics remain unchanged for later stages.
  Validation: `bash -n scripts/build_arduino_lib.sh`,
  `./scripts/build_arduino_lib.sh --help`,
  `./scripts/build_arduino_lib.sh --clean --freertos`,
  `cmake --preset rp2040-freertos -S examples`,
  `cmake --build build_examples_rp2040_freertos --target 29_freertos_smoke_rp2040`,
  and `./runalltests.sh` passed all 7 gates.
- 2026-06-11: Stage 1 flag and documentation skeleton completed.
  `HAL_ENABLE_FREERTOS` is now documented in `hal_config.h`,
  [`src/HAL_FLAGS.txt`](../src/HAL_FLAGS.txt), [README.md](../README.md),
  [JaszczurHAL_API.md](JaszczurHAL_API.md), and
  [lib_compilation.md](lib_compilation.md). Compile-time validation now rejects
  RP2040 builds that define the flag without arduino-pico `__FREERTOS`, rejects
  STM32G474 builds without visible `<FreeRTOS.h>` / `FreeRTOSConfig.h`, and
  rejects unsupported targets such as mock. Per [Thread-SafetyAudit.md](Thread-SafetyAudit.md),
  this is intentionally a configuration/documentation skeleton only: no
  `hal_sync`, delay, timer, or runtime task-safety behavior changed. Validation:
  explicit compile-error checks for RP2040/STM32/mock, markdown link audit,
  `git diff --check`, and `./runalltests.sh` passed all 7 gates.
- 2026-06-11: Audit context rule added. Agents working on FreeRTOS-related tasks
  must read [Thread-SafetyAudit.md](Thread-SafetyAudit.md) and account for its
  findings before planning or editing. Validation: markdown link audit and
  `git diff --check` passed.
- 2026-06-11: FreeRTOS planning documentation relocated under `doc/`:
  [FreeRTOS_imp.md](FreeRTOS_imp.md) and
  [Thread-SafetyAudit.md](Thread-SafetyAudit.md) now live together with the
  other project docs. Root [README.md](../README.md) and affected cross-doc
  links were updated. Validation: all non-README Markdown documents are under
  `doc/`, markdown link audit and `git diff --check` passed.
- 2026-06-11: Stage 0 thread-safety audit completed as documentation-only
  inventory in [Thread-SafetyAudit.md](Thread-SafetyAudit.md). It classifies setup/teardown,
  mutex-protected runtime paths, callback/ISR restrictions, Arduino-origin
  wrappers, lazy mutex risks, and priority follow-ups before enabling
  `HAL_ENABLE_FREERTOS`. Validation: `./runalltests.sh` passed all 7 gates.
- 2026-06-11: P0 `app_task1()` opt-in implemented. `HAL_ENABLE_APP_TASK1` now
  gates `loop1()` emission on RP2040 and cooperative `app_task1()` dispatch on
  STM32/mock; docs synced in `hal_app.h`, `examples/README.md`,
  [README.md](../README.md), [JaszczurHAL_API.md](JaszczurHAL_API.md),
  `hal_config.h`, and `HAL_FLAGS.txt`. Validation:
  `./runalltests.sh` passed all 7 gates.
- 2026-06-11: Hard progress rule added. Future FreeRTOS-related completed work
  must be recorded in this "Done" section with date, stage/P-item, outcome, and
  validation status. Validation: `./runalltests.sh` passed all 7 gates.
- RP2040 `hal_critical_section_*` made nesting-safe and per-core; regression
  test added on the mock backend (`tests/test_hal_critical_section.cpp`).
- `SmartTimers` Rule of Three closed (copy ctor/assignment deleted); compile-time
  guard added in `tests/test_SmartTimers.cpp`.
- STM32 spinlock concern reframed: not a practical hazard given single-core +
  `HAL_ENABLE_FREERTOS` path selection (see `hal_sync` note above).
- Two-primitive critical-section decision recorded (hard full-mask vs
  scheduler-lock).
- Host-side FreeRTOS CI validation answered via the POSIX kernel port.
