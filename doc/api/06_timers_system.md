# Timers, system, bits, math

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## `hal_status` - Shared status codes

```c
#include <hal/hal_status.h>

typedef enum {
    HAL_NONE = 0,
    HAL_OK = 1,
    HAL_EINVAL = -1,
    HAL_EBUSY = -2,
    HAL_ETIMEOUT = -3,
    HAL_EIO = -4,
    HAL_EUNSUPPORTED = -5,
    HAL_ENOENT = -6,
    HAL_EAGAIN = -7,
    HAL_EOVERFLOW = -8,
    HAL_ENOMEM = -9,
    HAL_IGNORED = -10,
    HAL_EEXIST = -11,
    HAL_EPERM = -12,
    HAL_EINTERNAL = -13,
    HAL_ECANCELED = -14,
    HAL_EPROTO = -15,
    HAL_EAUTH = -16,
    HAL_EBUS = -17,
    HAL_EHW = -18,
    HAL_ECONFIG = -19,
    HAL_ESTATE = -20,
    HAL_EUNINIT = -21,
    HAL_EDEPRECATED = -22,
    HAL_EUNKNOWN = -23,
} hal_status_t;

static inline const char *hal_status_to_string(hal_status_t status);
```

This is the common status vocabulary for new public APIs. Existing value,
handle and `bool` contracts remain compatibility wrappers when migrated;
fallible historical `void` operations may change in place to return
`hal_status_t` because callers can continue ignoring the result.

Use `HAL_OK` for success, `status < 0` for generic failure checks, and the
specific error codes for diagnostics at module/backend boundaries.
`hal_status_to_string()` returns stable symbolic names such as `"HAL_EIO"` and
`"HAL_STATUS_UNKNOWN"` for unrecognised numeric values. The `HAL_` prefix avoids
collisions with POSIX `errno` names used by the BSD sockets compatibility layer.

---

## `hal_timer` - Hardware alarms

```c
#include <hal/hal_timer.h>

typedef int32_t hal_alarm_id_t;
#define HAL_ALARM_INVALID (-1)
typedef int64_t (*hal_alarm_callback_t)(hal_alarm_id_t id, void *user_data);
typedef enum { ... } hal_timer_result_t;
typedef enum { HAL_TIMER_STATE_STOPPED, HAL_TIMER_STATE_RUNNING, HAL_TIMER_STATE_PAUSED } hal_timer_state_t;

// Layer 1: low-level alarms (one-shot + cancel)
hal_alarm_id_t hal_timer_add_alarm_us(uint32_t delay_us,
                                      hal_alarm_callback_t callback,
                                      void *user_data,
                                      bool fire_if_past);
hal_alarm_id_t hal_timer_add_alarm_us_ex(uint32_t delay_us,
                                         hal_alarm_callback_t callback,
                                         void *user_data,
                                         bool fire_if_past,
                                         hal_timer_result_t *out_result);
bool hal_timer_cancel_alarm(hal_alarm_id_t alarm_id);

// Layer 1 advanced: alarm pools (scale logical alarms beyond default pool)
typedef struct hal_timer_pool_impl_s *hal_timer_pool_t;
#define HAL_TIMER_POOL_DEFAULT ((hal_timer_pool_t)0)
hal_timer_pool_t hal_timer_pool_create(uint8_t hardware_alarm_num, uint16_t max_timers);
hal_timer_pool_t hal_timer_pool_create_auto(uint16_t max_timers);
void hal_timer_pool_destroy(hal_timer_pool_t pool);
hal_alarm_id_t hal_timer_pool_add_alarm_us(hal_timer_pool_t pool,
                                           uint32_t delay_us,
                                           hal_alarm_callback_t callback,
                                           void *user_data,
                                           bool fire_if_past);
hal_alarm_id_t hal_timer_pool_add_alarm_us_ex(hal_timer_pool_t pool,
                                              uint32_t delay_us,
                                              hal_alarm_callback_t callback,
                                              void *user_data,
                                              bool fire_if_past,
                                              hal_timer_result_t *out_result);
bool hal_timer_pool_cancel_alarm(hal_timer_pool_t pool, hal_alarm_id_t alarm_id);

// Layer 2: managed timers (one-shot or periodic)
typedef struct hal_timer_impl_s *hal_timer_t;
typedef void (*hal_timer_callback_t)(hal_timer_t timer, void *user_data);
hal_timer_result_t hal_timer_create(hal_timer_pool_t pool, uint32_t period_us,
                                    bool periodic, hal_timer_callback_t callback,
                                    void *user_data, hal_timer_t *out_timer);
hal_timer_result_t hal_timer_destroy(hal_timer_t timer);
hal_timer_result_t hal_timer_start(hal_timer_t timer);
hal_timer_result_t hal_timer_stop(hal_timer_t timer);
hal_timer_result_t hal_timer_pause(hal_timer_t timer);
hal_timer_result_t hal_timer_resume(hal_timer_t timer);
hal_timer_result_t hal_timer_set_period_us(hal_timer_t timer, uint32_t period_us,
                                           bool restart_if_running);
hal_timer_result_t hal_timer_get_period_us(hal_timer_t timer, uint32_t *out_period_us);
hal_timer_state_t  hal_timer_get_state(hal_timer_t timer);
hal_timer_result_t hal_timer_get_remaining_us(hal_timer_t timer, int64_t *out_remaining_us);
```

**Layer model:** use low-level alarms for minimal ISR scheduling; use managed timers when you need start/stop/pause/resume/status semantics and periodic behavior.
**Error model:** `_ex` functions return detailed `hal_timer_result_t` diagnostics (`INVALID_ARG`, `TIME_PASSED`, `POOL_FULL`, `NO_RESOURCE`, etc.) while legacy non-`_ex` variants preserve `HAL_ALARM_INVALID` compatibility.
**impl/rp2040:** Pico SDK alarm pools (`pico/time.h`) and callback scheduling (`alarm_pool_add_alarm_in_us`, cancel APIs). `add_alarm_in_us()` outcomes `<= 0` are treated as invalid and mapped to explicit result codes in `_ex`.
**impl/stm32g474:** TIM6 runs as a 1 MHz one-shot alarm scheduler. Long delays are chunked across 16-bit TIM6 periods, callback return values greater than zero reschedule the same alarm, and software pools provide the same public pool/cancel contract as RP2040.
**Thread safety:** RP2040 backend is thread-safe and multicore-safe for scheduling/canceling and managed-timer state transitions. STM32G474 protects its alarm slot scheduler with short PRIMASK critical sections; alarm callbacks execute from the TIM6 IRQ on hardware. Keep callbacks short and non-blocking. Mock backend is deterministic for tests but not synchronized for concurrent host threads.

---

### Examples

**Example: One-shot alarm (low-level)**
```c
#include <hal/hal_timer.h>
#include <hal/hal_system.h>

static bool alarm_fired = false;

static int64_t on_timeout(hal_alarm_id_t id, void *user_data) {
    alarm_fired = true;
    hal_deb("Alarm %d fired after timeout", id);
    return 0;  // do not reschedule
}

void example_alarm(void) {
    // Schedule a one-shot alarm for 500 ms from now
    hal_alarm_id_t alarm = hal_timer_add_alarm_us(500000,
                                                    on_timeout,
                                                    NULL,
                                                    false);

    if (alarm != HAL_ALARM_INVALID) {
        hal_deb("Alarm scheduled with ID: %d", alarm);
    }

    // Wait for it to fire
    while (!alarm_fired) {
        hal_delay_ms(10);
    }

    hal_deb("Alarm execution complete");
}
```

**Example: Managed periodic timer**
```c
#include <hal/hal_timer.h>

static uint32_t tick_count = 0;

static void periodic_callback(hal_timer_t timer, void *user_data) {
    tick_count++;
    if (tick_count % 10 == 0) {
        hal_deb("Timer fired %lu times", tick_count);
    }
}

void example_managed_timer(void) {
    hal_timer_t my_timer;

    // Create a periodic timer that fires every 1 second
    hal_timer_result_t result = hal_timer_create(
        HAL_TIMER_POOL_DEFAULT,
        1000000,           // 1,000,000 microseconds = 1 second
        true,              // periodic
        periodic_callback,
        NULL,
        &my_timer
    );

    if (result == HAL_TIMER_RESULT_OK) {
        hal_deb("Timer created successfully");

        // Start the timer
        hal_timer_start(my_timer);

        // Let it run for 5 seconds
        hal_delay_ms(5000);

        // Pause it temporarily
        hal_timer_pause(my_timer);
        hal_deb("Timer paused, ticks: %lu", tick_count);

        // Resume after 2 seconds
        hal_delay_ms(2000);
        hal_timer_resume(my_timer);

        // Stop and destroy
        hal_timer_stop(my_timer);
        hal_timer_destroy(my_timer);

        hal_deb("Final tick count: %lu", tick_count);
    } else {
        hal_derr("Failed to create timer: %d", result);
    }
}
```

**Example: Alarm pool for many timers**
```c
#include <hal/hal_timer.h>

static int64_t pool_callback(hal_alarm_id_t id, void *user_data) {
    uint32_t timer_num = (uint32_t)(uintptr_t)user_data;
    hal_deb("Pool alarm %d (user data: %lu) fired", id, timer_num);
    return 0;
}

void example_alarm_pool(void) {
    // Create a pool supporting up to 10 timers on a dedicated hardware alarm
    hal_timer_pool_t pool = hal_timer_pool_create_auto(10);

    if (pool == NULL) {
        hal_derr("Failed to create timer pool");
        return;
    }

    // Schedule multiple alarms using the pool
    hal_alarm_id_t alarms[5];
    for (int i = 0; i < 5; i++) {
        alarms[i] = hal_timer_pool_add_alarm_us(
            pool,
            (i + 1) * 300000,  // 300ms, 600ms, 900ms, 1200ms, 1500ms
            pool_callback,
            (void *)(uintptr_t)i,
            false
        );

        if (alarms[i] != HAL_ALARM_INVALID) {
            hal_deb("Scheduled alarm %d (user %d) for %d ms", alarms[i], i, (i+1)*300);
        }
    }

    // Wait for all to fire
    hal_delay_ms(2000);

    // Cleanup
    hal_timer_pool_destroy(pool);
}
```

---

## `hal_system` - Timing, watchdog & system info

```c
#include <hal/hal_system.h>

// Time-conversion macros (also included by SmartTimers.h)
#define SECOND      1000UL
#define SECS(t)     ((unsigned long)((t) * SECOND))
#define MINS(t)     (SECS(t) * 60UL)
#define HOURS(t)    (MINS(t) * 60UL)

uint32_t hal_millis(void);
typedef void (*hal_millis_interval_callback_t)(void *user_data);
bool hal_millis_interval_elapsed(uint32_t now_ms, uint32_t *last_ms,
                                 uint32_t interval_ms);
bool hal_millis_interval_elapsed_now(uint32_t *last_ms, uint32_t interval_ms);
bool hal_millis_interval_call(uint32_t now_ms, uint32_t *last_ms,
                              uint32_t interval_ms,
                              hal_millis_interval_callback_t callback,
                              void *user_data);
bool hal_millis_interval_call_now(uint32_t *last_ms, uint32_t interval_ms,
                                  hal_millis_interval_callback_t callback,
                                  void *user_data);
uint32_t hal_micros(void);
uint64_t hal_micros64(void);          // 64-bit timestamp, no overflow
void     hal_delay_ms(uint32_t ms);
void     hal_delay_us(uint32_t us);
void     hal_watchdog_feed(void);
hal_status_t hal_watchdog_enable(uint32_t ms, bool pause_on_debug);
bool     hal_watchdog_caused_reboot(void);
void     hal_idle(void);
bool     hal_in_isr(void);            // true when called from an exception/IRQ handler
uint32_t hal_get_free_heap(void);     // available heap in bytes
hal_status_t hal_read_chip_temp_ex(float *out_celsius);
float    hal_read_chip_temp(void);    // on-die temperature in °C (±2 °C typical)
hal_status_t hal_enter_bootloader(void); // does not return on supported hardware
hal_status_t hal_u32_to_bytes_be(uint32_t val, uint8_t *buf);

typedef struct {
    const char *target_name;
    const char *backend_name;
    const char *mcu;
    const char *mcu_subtype;
    const char *cpu_arch;
    const char *rtos_name;
    uint8_t cpu_cores;
    bool is_hardware;
    bool has_fpu;
    bool has_rtos;
    uint32_t cpu_clock_hz;
    uint32_t peripheral_clock_hz;
    uint32_t flash_total_bytes;
    uint32_t flash_usable_bytes;
    uint32_t flash_reserved_bytes;
    uint32_t ram_total_bytes;
    uint32_t ram_usable_bytes;
    uint32_t heap_total_bytes;
    uint32_t heap_free_bytes;
    uint32_t stack_total_bytes;
    uint32_t uid_bytes;
} hal_system_architecture_t;

hal_status_t hal_system_get_current_architecture(hal_system_architecture_t *out);

// Device unique identifier (RP2040 flash unique id).
#define HAL_DEVICE_UID_BYTES        8u
#define HAL_DEVICE_UID_HEX_BUF_SIZE 17u  // 16 hex chars + NUL

hal_status_t hal_get_device_uid(uint8_t uid[HAL_DEVICE_UID_BYTES]);
hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen);
bool hal_get_device_uid_hex(char *buf, size_t buflen);

// Crash / fault diagnostics (full reference in the "Crash / fault diagnostics"
// block below).
void               hal_fault_subsystem_init(void);
hal_reset_reason_t hal_get_reset_reason(void);
const char        *hal_reset_reason_str(hal_reset_reason_t reason);
hal_status_t        hal_get_last_fault_ex(hal_fault_info_t *out);
bool               hal_get_last_fault(hal_fault_info_t *out);
void               hal_clear_last_fault(void);
bool               hal_last_boot_was_brownout(void);
void               hal_alive_mark(void);
hal_status_t        hal_stack_guard_init_ex(void);
bool               hal_stack_guard_init(void);
void               hal_stack_guard_check(void);

// Type-independent math helpers (macros)
#define hal_constrain(v, lo, hi) ...
#define hal_map(x, in_min, in_max, out_min, out_max) ...

// NONULL helper macro: if pointer is null, jump to local `error:` label
#define NONULL(x) do { if ((x) == NULL) { goto error; } } while (0)
// COUNTOF helper macro: calculating the `C-array` size
#define COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

```

`hal_system_get_current_architecture()` returns a by-value snapshot copied into the caller-provided output struct. String fields point to backend-owned static storage; numeric fields use `0` when the backend cannot report a meaningful value. The API does not allocate and the caller does not own or free the returned strings.

The status-first system calls distinguish invalid output pointers
(`HAL_EINVAL`), absent retained fault data (`HAL_ENOENT`) and services that the
active backend does not implement (`HAL_EUNSUPPORTED`). The historical
`hal_read_chip_temp()`, `hal_get_last_fault()` and `hal_stack_guard_init()`
functions are compatibility wrappers over their adjacent `_ex` operations.

The millis-interval helpers provide a minimal non-blocking scheduler pattern
for loop-driven firmware without arming hardware timers. They implement the
wrap-safe arithmetic `now_ms - last_ms >= interval_ms` and update `last_ms`
only when the interval elapsed. `*_now` variants fetch `now_ms` internally via
`hal_millis()`. `hal_millis_interval_call*()` invokes a callback (when non-NULL)
after a successful elapsed check and returns `true` for that iteration.

**impl/rp2040:** Time is read through native pico-sdk only - `hal_millis()` uses `to_ms_since_boot(get_absolute_time())`, `hal_micros()`/`hal_micros64()` use `time_us_64()` (no Arduino `millis()`/`micros()`). All RP2040 / pico-sdk bindings (`watchdog_*`, `tight_loop_contents()`, `rp2040.getFreeHeap()`, `analogReadTemp()`, `reset_usb_boot()`, `pico_get_unique_board_id()`), the Cortex-M `IPSR` read used by `hal_in_isr()`, the UID hex formatter, and the backend architecture metadata live in the SoC driver `src/hal/impl/rp2040/drivers/rp2040/rp2040_system.{h,cpp}` and are reached through `rp2040_system_*` wrappers. In `HAL_ENABLE_FREERTOS + __FREERTOS` builds, `hal_delay_ms()` uses `vTaskDelay(pdMS_TO_TICKS(ms))` only when the scheduler is running, the caller is in task context, and the current core is not inside `hal_critical_section_*`; otherwise it falls back to pico `sleep_ms()` (when interrupts are enabled) or `busy_wait_ms()` (in ISR / HAL-critical context). `hal_idle()` yields in valid FreeRTOS task context and falls back to `tight_loop_contents()` otherwise. `hal_delay_us()` uses `busy_wait_us()` (never `sleep_us()`/`delayMicroseconds()`, which arm an alarm interrupt and would deadlock inside a critical section). The architecture snapshot reports total flash from `PICO_FLASH_SIZE_BYTES` and, when the final Arduino-pico layout exposes `FS_START`, computes usable firmware flash as the XIP region before the filesystem/EEPROM partition. `stack_total_bytes` reports the core-0 stack reservation only (`PICO_STACK_SIZE` / `HAL_RP2040_STACK_SIZE`); `heap_total_bytes` stays `0` because Arduino-pico does not expose a stable total-heap capacity. The "watchdog caused reboot" status is latched once during C++ static init (before `setup()`) so a later `hal_watchdog_enable()` cannot lose the bit; see the driver header for the scratch[4] magic rationale.
**impl/stm32g474:** Bare-metal backend with the same dispatch/wrapper pattern as the RP2040 path; host sanity builds of this backend report `is_hardware = false` when `JH_STM32G474_HW` is not active. All SoC bindings (SysTick/FreeRTOS tick time, DWT cycle fallback delays, IWDG planned, `__WFI()`, ADC1 channel 16 for on-die temp, `UID_BASE` for the device id, backend architecture metadata, and stack/RAM span helpers) live in `src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.{h,cpp}` behind `stm32g474_system_*` wrappers. `hal_in_isr()` reads `IPSR` on ARM targets and returns `false` on host builds (same rule as Arduino, gated by `__arm__`). In `HAL_ENABLE_FREERTOS` builds, `hal_delay_ms()` uses `vTaskDelay(pdMS_TO_TICKS(ms))` only when the scheduler is running, the caller is in task context, and the core is outside `hal_critical_section_*`; otherwise it falls back to a DWT cycle busy-wait. `hal_idle()` yields in valid FreeRTOS task context and uses a no-op fallback otherwise. `hal_delay_us()` remains a DWT cycle busy-wait on hardware FreeRTOS builds. The architecture snapshot reports `flash_total_bytes` from `HAL_STM32_FLASH_SIZE` and subtracts `HAL_STM32_FLASH_EEPROM_SIZE` plus `HAL_STM32_FLASH_LITTLEFS_SIZE` into `flash_reserved_bytes`; `flash_usable_bytes` is the remainder. On hardware builds, `ram_total_bytes`/`ram_usable_bytes` and `stack_total_bytes` are derived from the STM32 linker layout symbols exported by `stm32_lib/STM32G474RETx_FLASH.ld`; host fallback uses the backend defaults.
**impl/.mock:** time driven by mock helpers; `hal_watchdog_caused_reboot`, `hal_get_free_heap`, chip temperature, and the device UID are injectable. `hal_enter_bootloader()` sets an observable flag instead of rebooting. `hal_in_isr()` returns the value set by `hal_mock_set_in_isr(bool)`.
**Thread safety:** RP2040 backend: time/watchdog APIs are safe to call from both cores. In RP2040 and STM32G474 FreeRTOS modes, `hal_delay_ms()` yields or blocks the calling task only in legal task context and busy-waits in pre-scheduler/ISR/HAL-critical contexts; `hal_delay_us()` always blocks only the calling core. Mock backend uses shared unsynchronized state and is intended for single-threaded tests.
**Note:** `COUNTOF(arr)` works only with statically-allocated arrays (not pointers).
**Note:** `NONULL(x)` is a null-pointer guard for functions that use a shared
`error:` cleanup path. Uses `NULL` (safe in both C and C++ translation units).
If `x == NULL`, it performs `goto error;`. The surrounding function must define
an `error:` label.

### Examples

**Example: Architecture snapshot**
```c
#include <hal/hal_system.h>

void example_architecture_snapshot(void) {
    hal_system_architecture_t arch = {0};
    hal_status_t status = hal_system_get_current_architecture(&arch);
    if (status != HAL_OK) {
        hal_derr("arch snapshot failed: %s", hal_status_to_string(status));
        return;
    }

    hal_deb("target=%s backend=%s mcu=%s cpu=%s rtos=%s",
            arch.target_name,
            arch.backend_name,
            arch.mcu,
            arch.cpu_arch,
            arch.rtos_name);
    hal_deb("flash total=%lu usable=%lu reserved=%lu ram=%lu heap_free=%lu",
            (unsigned long)arch.flash_total_bytes,
            (unsigned long)arch.flash_usable_bytes,
            (unsigned long)arch.flash_reserved_bytes,
            (unsigned long)arch.ram_total_bytes,
            (unsigned long)arch.heap_free_bytes);
}
```

**Example: System timing and watchdog**
```c
#include <hal/hal_system.h>
#include <hal/hal_serial.h>

void example_system_timing(void) {
    // Get current time
    uint32_t start_ms = hal_millis();
    uint32_t start_us = hal_micros();

    // Busy-wait for 500 ms with microsecond precision
    hal_delay_us(500000);

    uint32_t elapsed_ms = hal_millis() - start_ms;
    uint32_t elapsed_us = hal_micros() - start_us;

    hal_deb("Elapsed: %lu ms, %lu us", elapsed_ms, elapsed_us);

    // Use time conversion macros
    uint32_t one_minute = MINS(1);   // 60000 ms
    uint32_t five_secs = SECS(5);    // 5000 ms
    uint32_t one_hour = HOURS(1);    // 3600000 ms

    // Setup watchdog: reset if not fed for 5 seconds
    hal_status_t watchdog_status = hal_watchdog_enable(5000, false);
    if (watchdog_status != HAL_OK) {
        hal_derr("watchdog unavailable: %s",
                 hal_status_to_string(watchdog_status));
        return;
    }

    // Main loop with watchdog feeding
    uint32_t loop_count = 0;
    while (loop_count < 100) {
        // Do work...
        hal_delay_ms(100);

        // Feed watchdog every 1 second
        if (loop_count % 10 == 0) {
            hal_watchdog_feed();
        }

        loop_count++;
    }

    hal_deb("Watchdog feeding complete");
}
```

**Example: Non-blocking loop interval callback**
```c
#include <hal/hal_system.h>

static uint32_t last_publish_ms = 0;

static void publish_cb(void *user_data) {
    (void)user_data;
    callback();
}

void app_task0(void) {
    uint32_t now = hal_millis();

    // Canonical wrap-safe non-blocking interval pattern.
    (void)hal_millis_interval_call(now,
                                   &last_publish_ms,
                                   PUBLISH_INTERVAL,
                                   publish_cb,
                                   NULL);
}
```

Equivalent explicit form:

```c
uint32_t now = hal_millis();
if (hal_millis_interval_elapsed(now, &last_publish_ms, PUBLISH_INTERVAL)) {
    callback();
}
```

**Example: Device UID and reset diagnostics**
```c
#include <hal/hal_system.h>
#include <hal/hal_serial.h>

void example_device_uid_and_reset(void) {
    // Very first: initialize fault diagnostics
    hal_fault_subsystem_init();
    hal_stack_guard_init();

    // Get device unique identifier
    uint8_t uid[HAL_DEVICE_UID_BYTES];
    if (hal_get_device_uid(uid) != HAL_OK) {
        return;
    }

    char uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE];
    if (hal_get_device_uid_hex(uid_hex, sizeof(uid_hex))) {
        hal_deb("Device UID: %s", uid_hex);
    }

    // Check reset reason
    hal_reset_reason_t reset_reason = hal_get_reset_reason();
    hal_deb("Reset reason: %s", hal_reset_reason_str(reset_reason));

    // Check for previous fault
    hal_fault_info_t fault_info;
    if (hal_get_last_fault(&fault_info) && fault_info.valid) {
        hal_deb("Previous fault detected:");
        hal_deb("  PC:  0x%08lx", fault_info.pc);
        hal_deb("  LR:  0x%08lx", fault_info.lr);
        hal_deb("  PSR: 0x%08lx", fault_info.psr);
        hal_clear_last_fault();  // Clear for next boot
    }

    // Check for brownout
    if (hal_last_boot_was_brownout()) {
        hal_derr("Brown-out suspected on previous boot!");
    }

    // System info
    uint32_t free_heap = hal_get_free_heap();
    float chip_temp = 0.0f;
    hal_status_t temp_status = hal_read_chip_temp_ex(&chip_temp);
    uint32_t core_id = hal_get_core_id();

    if (temp_status == HAL_OK) {
        hal_deb("Free heap: %lu bytes, Chip temp: %.1f°C, Core: %lu",
                free_heap, chip_temp, core_id);
    }

    // Mark alive for brownout detection
    hal_alive_mark();
}
```

**Example: Check if running in interrupt**
```c
#include <hal/hal_system.h>

static volatile uint32_t isr_counter = 0;

static void my_isr_callback(void) {
    isr_counter++;

    // This is running in ISR context
    if (hal_in_isr()) {
        hal_deb("ISR callback #%lu", isr_counter);
    } else {
        hal_deb("ERROR: Expected ISR context but not in ISR!");
    }
}

void example_isr_detection(void) {
    // In normal task/main context, this is false
    if (!hal_in_isr()) {
        hal_deb("Running in task context (not ISR)");
    }

    // Simulate interrupt trigger (will call my_isr_callback)
    // In real code, this would be triggered by actual hardware interrupt
    my_isr_callback();
}
```

---
```c
void hal_mock_set_millis(uint32_t ms);
void hal_mock_advance_millis(uint32_t ms);
void hal_mock_set_micros(uint32_t us);
void hal_mock_advance_micros(uint32_t us);
bool hal_mock_watchdog_was_fed(void);
void hal_mock_watchdog_reset_flag(void);
void hal_mock_set_caused_reboot(bool val);
void hal_mock_set_free_heap(uint32_t bytes);  // default: 256 KB
void hal_mock_set_chip_temp(float celsius);   // default: 25.0 °C
bool hal_mock_bootloader_was_requested(void);
void hal_mock_bootloader_reset_flag(void);
void hal_mock_set_device_uid(const uint8_t uid[8]);  // override UID
void hal_mock_reset_device_uid(void);                // restore default E661A4D1234567AB
void hal_mock_set_in_isr(bool in_isr);               // forces hal_in_isr() return value for tests
```

**Device UID details:**
- `hal_get_device_uid(uid)` fills an exactly 8-byte output buffer and returns
  `HAL_EINVAL` for `NULL`.
- `hal_get_device_uid_hex_ex(buf, buflen)` writes 16 uppercase hex characters
  followed by a NUL terminator (17 bytes total). It reports `HAL_EINVAL` for
  `NULL` buffers and `HAL_EOVERFLOW` when
  `buflen < HAL_DEVICE_UID_HEX_BUF_SIZE`.
- `hal_get_device_uid_hex(buf, buflen)` is the legacy `bool` wrapper over the
  status-returning API.
- On RP2040 hardware the source is the 64-bit unique identifier stored in
  the external QSPI flash chip, read via `pico_get_unique_board_id()`.
  This identifier is persistent across reboots, unique per device, and the
  same value that arduino-pico exposes as USB iSerialNumber.
- In the mock backend the default value is deterministic
  (`0xE6 0x61 0xA4 0xD1 0x23 0x45 0x67 0xAB` -> `"E661A4D1234567AB"`) so
  tests that compare the UID string can hard-code the expected value.
  Use `hal_mock_set_device_uid()` to simulate a second board.

**Crash / fault diagnostics:**
```c
typedef enum {
    HAL_RESET_REASON_UNKNOWN = 0,
    HAL_RESET_REASON_POWER_ON,
    HAL_RESET_REASON_RUN_PIN,
    HAL_RESET_REASON_SOFT,
    HAL_RESET_REASON_WATCHDOG,
    HAL_RESET_REASON_DEBUG,
    HAL_RESET_REASON_GLITCH,
    HAL_RESET_REASON_BROWNOUT,
    HAL_RESET_REASON_HARDFAULT,
    HAL_RESET_REASON_STACK_OVERFLOW
} hal_reset_reason_t;

typedef struct {
    bool     valid;   // true if pc/lr/psr below are meaningful
    uint32_t pc;      // stacked PC at fault
    uint32_t lr;      // stacked LR at fault
    uint32_t psr;     // stacked xPSR at fault
} hal_fault_info_t;

void               hal_fault_subsystem_init(void);
hal_reset_reason_t hal_get_reset_reason(void);
const char        *hal_reset_reason_str(hal_reset_reason_t reason);
hal_status_t        hal_get_last_fault_ex(hal_fault_info_t *out);
bool               hal_get_last_fault(hal_fault_info_t *out);
void               hal_clear_last_fault(void);
bool               hal_last_boot_was_brownout(void);
void               hal_alive_mark(void);
hal_status_t        hal_stack_guard_init_ex(void);
bool               hal_stack_guard_init(void);
void               hal_stack_guard_check(void);
```

`hal_fault_subsystem_init()` must be called once, as early as possible in
boot, before any code that could fault. It latches the silicon reset-reason
flags, snapshots any HardFault info left over from the previous boot into
RAM, and clears the volatile flag bits so the next event is detected fresh.

**Typical wiring (application setup / loop):**
```c
hal_fault_subsystem_init();                   // very first call in setup
hal_stack_guard_init();                       // install stack-bottom canary
log("reset: %s", hal_reset_reason_str(hal_get_reset_reason()));
hal_fault_info_t f;
if (hal_get_last_fault(&f) && f.valid) {
    log("previous fault: PC=0x%08lx LR=0x%08lx PSR=0x%08lx", f.pc, f.lr, f.psr);
}
if (hal_last_boot_was_brownout()) {
    log("suspected brown-out on previous boot");
}
// ... in main loop:
hal_alive_mark();                             // refresh brown-out heuristic marker
hal_stack_guard_check();                      // validate canary
```

**impl/rp2040 (RP2040):** Implemented by the SoC-specific driver
`src/hal/impl/rp2040/drivers/rp2040/rp2040_fault.{h,cpp}`. The HAL layer
forwards to thin `rp2040_fault_*` wrappers. Retained state lives in
`watchdog_hw->scratch[0..3]` (scratch `[4..7]` is reserved by pico-sdk for
`WATCHDOG_NON_REBOOT_MAGIC` / `watchdog_reboot()` arguments). The HardFault
handler is naked ASM that captures the exception frame (PC/LR/PSR - Cortex-M0+
has no CFSR/HFSR/MMFAR/BFAR), stores it into scratch with a `'JHD'` signature,
then triggers `watchdog_reboot(0, 0, 0)`. The stack canary is placed at
`__StackLimit` and the address is laundered through inline asm to avoid the
GCC `-Warray-bounds` false positive caused by arduino-pico declaring
`__StackLimit` as `char[1]`. `HAL_RESET_REASON_BROWNOUT` is not reported by
silicon (POR and BOR share one flag) - `hal_last_boot_was_brownout()` is a
heuristic that returns true when the silicon reported POR but the retained
alive marker survived (suggesting V<sub>DD</sub> dipped below the BOR
threshold without losing scratch).
**impl/stm32g474:** Implemented backend behind the same dispatch pattern as RP2040: `src/hal/impl/stm32g474/hal_system.cpp` forwards to `stm32g474_fault_*` wrappers, while the SoC logic lives in `src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.{h,cpp}`. Reset reason is classified from `RCC->CSR` flags (`IWDGRSTF`/`WWDGRSTF`, `SFTRSTF`, `PINRSTF`, `BORRSTF`, `LPWRRSTF`, `OBLRSTF`), with reset flags cleared via `RMVF` after latching. Captured fault state is taken from the retained Cortex-M4 exception record (`exception_info` `.noinit` handoff) and surfaced through `hal_get_last_fault()` as `{pc, lr, psr}` with reset reason `HARDFAULT`. `hal_stack_guard_*` is also active on STM32G474: it places a canary at the linker-provided stack limit, stores a retained `STACK_OVERFLOW` marker on corruption, and forces a system reset so the next boot reports `HAL_RESET_REASON_STACK_OVERFLOW`.
**impl/.mock:** All state is injectable; see the mock helpers below. The
mock `hal_fault_subsystem_init()` does NOT reset the staged reset-reason /
fault-info so tests can pre-populate state and observe behaviour across an
init call. Use `hal_mock_fault_diagnostics_reset()` to clear it explicitly.

**Mock helpers:**
```c
void hal_mock_set_reset_reason(hal_reset_reason_t reason);
void hal_mock_set_last_fault(const hal_fault_info_t *info);  // NULL clears
void hal_mock_set_brownout_suspected(bool v);
bool hal_mock_alive_was_marked(void);
void hal_mock_alive_reset_flag(void);
bool hal_mock_fault_subsystem_was_inited(void);
bool hal_mock_stack_guard_is_armed(void);
bool hal_mock_stack_guard_check_was_triggered(void);
void hal_mock_fault_diagnostics_reset(void);
```

---

## `hal_bits` - Bit helpers

```c
#include <hal/hal_bits.h>

#define is_set(x, mask)      ...
#define set_bit(var, mask)   ...
#define clr_bit(var, mask)   ...
#define bitSet(var, bit)     ...
#define bitClear(var, bit)   ...
#define bitRead(var, bit)    ...
#define set_bit_v(reg, mask) ...
#define clr_bit_v(reg, mask) ...
```

**Note:** All helpers are macros (type-width independent). Avoid passing expressions with side effects (`i++`, stateful function calls), because arguments may be evaluated more than once. `bitSet/bitClear/bitRead` remain guarded with `#ifndef` for Arduino compatibility.
**Thread safety:** Stateless helpers; thread-safe by themselves. When multiple contexts touch the same variable/register, synchronization is the caller's responsibility.

### Examples

**Example: Bit manipulation with masks**
```c
#include <hal/hal_bits.h>

void example_bit_manipulation(void) {
    uint8_t status_reg = 0x00;
    uint8_t mode_mask = 0x0F;      // Lower 4 bits
    uint8_t enabled_mask = 0x80;   // Bit 7

    // Check if bits are set
    if (is_set(status_reg, enabled_mask)) {
        hal_deb("Enabled bit is set");
    }

    // Set multiple bits
    set_bit(status_reg, enabled_mask);  // status_reg |= 0x80
    hal_deb("After set: 0x%02x", status_reg);

    // Set individual bits by index
    bitSet(status_reg, 3);  // Set bit 3
    bitSet(status_reg, 2);  // Set bit 2
    hal_deb("After bitSet: 0x%02x", status_reg);

    // Read individual bit
    uint8_t bit_value = bitRead(status_reg, 7);
    hal_deb("Bit 7 value: %u", bit_value);

    // Clear specific bits
    clr_bit(status_reg, enabled_mask);
    hal_deb("After clear: 0x%02x", status_reg);

    // Clear by bit index
    bitClear(status_reg, 3);
    bitClear(status_reg, 2);
    hal_deb("After bitClear: 0x%02x", status_reg);
}
```

**Example: Register bit manipulation (volatile)**
```c
#include <hal/hal_bits.h>

// Simulated hardware register (volatile)
static volatile uint32_t *hw_control_reg = NULL;  // Would be: (uint32_t*)0x40000000

void example_register_bits(void) {
    if (hw_control_reg == NULL) return;

    // Set control bits in volatile register
    uint32_t enable_bit = 0x00000001;
    uint32_t mode_mask = 0x00000030;

    // Set bit in register (atomic operation)
    set_bit_v(hw_control_reg, enable_bit);

    // Clear bits in register
    clr_bit_v(hw_control_reg, mode_mask);

    hal_deb("Register updated");
}
```

---


## `hal_math` - Platform-independent math helpers

```c
#include <hal/hal_math.h>

// Clamp to [lo, hi] - type-independent macro
#define hal_constrain(v, lo, hi) ...
// Re-map value from one range to another - type-independent macro
// When in_min == in_max, returns out_min (safe: no division by zero).
#define hal_map(x, in_min, in_max, out_min, out_max) ...
```

**Note:** Macros are available in both C and C++ and are re-exported via
`hal/hal_system.h`. `hal_constrain` is also re-exported as `pid_clamp` for
backward compatibility.
Macro arguments may be evaluated more than once, so avoid side effects in
arguments (for example `i++` or function calls that modify state).
**Note:** `hal_map` returns `out_min` when `in_min == in_max` to avoid
integer division by zero. This matches the behaviour of `mapfloat()`.
**Thread safety:** Thread-safe. Helpers are pure expressions (no shared state).

### Examples

**Example: Clamping values**
```c
#include <hal/hal_math.h>
#include <hal/hal_system.h>

void example_constrain(void) {
    // Clamp integer to range
    int speed = 150;
    int clamped_speed = hal_constrain(speed, 0, 100);
    hal_deb("Speed %d clamped to %d", speed, clamped_speed);  // Output: 100

    // Clamp float value
    float voltage = -0.5f;
    float safe_voltage = hal_constrain(voltage, 0.0f, 3.3f);
    hal_deb("Voltage %.1f clamped to %.1f", voltage, safe_voltage);  // Output: 0.0

    // ADC reading clamping
    uint16_t raw_adc = 4100;  // 12-bit ADC max is 4095
    uint16_t clamped_adc = hal_constrain(raw_adc, 0, 4095);
    hal_deb("ADC %u clamped to %u", raw_adc, clamped_adc);  // Output: 4095
}
```

**Example: Remapping/scaling values**
```c
#include <hal/hal_math.h>

void example_map(void) {
    // Map ADC reading (0-4095) to voltage (0.0-3.3V)
    uint16_t adc_value = 2048;  // Midpoint
    int voltage_mv = hal_map(adc_value, 0, 4095, 0, 3300);  // Millivolts
    hal_deb("ADC %u -> %d mV", adc_value, voltage_mv);  // Output: 1650 mV

    // Map 0-255 PWM range to 0-100% duty cycle
    uint8_t pwm = 200;
    uint8_t percent = hal_map(pwm, 0, 255, 0, 100);
    hal_deb("PWM %u -> %u%%", pwm, percent);  // Output: 78%

    // Map temperature sensor reading to usable range
    uint16_t temp_raw = 512;
    int temp_celsius = hal_map(temp_raw, 0, 1023, -40, 125);  // -40 to +125°C
    hal_deb("Raw temp %u -> %d°C", temp_raw, temp_celsius);  // Output: 10°C

    // Inverse mapping: map 100-0% to PWM 0-255
    uint8_t brightness_percent = 75;
    uint8_t pwm_value = hal_map(brightness_percent, 0, 100, 0, 255);
    hal_deb("Brightness %u%% -> PWM %u", brightness_percent, pwm_value);  // Output: 191
}
```

**Example: Joystick/potentiometer deadzone**
```c
#include <hal/hal_math.h>

void example_joystick_with_deadzone(void) {
    uint16_t joystick_x = 500;  // Raw ADC reading (0-1023)
    uint16_t deadzone_low = 450;
    uint16_t deadzone_high = 550;

    // If in deadzone, return center; otherwise map to -100 to +100
    int16_t mapped_x;
    if (joystick_x >= deadzone_low && joystick_x <= deadzone_high) {
        mapped_x = 0;  // Deadzone
    } else if (joystick_x < deadzone_low) {
        // Left side: 0 to deadzone_low -> -100 to 0
        mapped_x = hal_map(joystick_x, 0, deadzone_low, -100, 0);
    } else {
        // Right side: deadzone_high to 1023 -> 0 to +100
        mapped_x = hal_map(joystick_x, deadzone_high, 1023, 0, 100);
    }

    hal_deb("Joystick raw %u -> mapped %d", joystick_x, mapped_x);
}
```

---


---

*Next: [Cryptography](07_crypto.md)*
