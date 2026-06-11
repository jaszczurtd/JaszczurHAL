# Thread-Safety Audit

Stage 0 audit for the FreeRTOS enablement plan.

Date: 2026-06-11

## Scope

This audit classifies current HAL modules before changing runtime behavior for
`HAL_ENABLE_FREERTOS`. It is intentionally conservative:

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
| TS-A01 | Lazy singleton mutex creation is common. Many modules use `if (!mutex) { hal_critical_section_enter(); if (!mutex) mutex = hal_mutex_create(); ... }`. | On STM32 bare-metal this is acceptable today. On RP2040, `hal_critical_section_*` is per-core, so it does not serialize creation across core 0/core 1. Under FreeRTOS/SMP this can leak a mutex and leave callers serializing on different locks. | Prefer eager creation in module init/setup paths. For modules without explicit init, add a small target-safe once primitive or module-local startup hook before enabling FreeRTOS runtime claims. |
| TS-A02 | `hal_critical_section_*` is a hard full interrupt mask, not a scheduler lock. | Using it for task mutual exclusion would harm latency and still would not be a cross-core lock on RP2040. | Keep it for timing/ISR-shared data only. Add a separate scheduler-lock primitive later if needed. |
| TS-A03 | `hal_delay_ms()` currently delegates to Arduino `delay()` on RP2040 and busy/system delay on STM32. | In FreeRTOS mode, calling `vTaskDelay()` before the scheduler or from illegal contexts can misbehave; busy delays inside tasks waste CPU. | Gate on scheduler state. Use `vTaskDelay(pdMS_TO_TICKS(ms))` only when scheduler is running and context is task-safe; otherwise fall back. |
| TS-A04 | Arduino-origin connectivity/storage libraries are serialized by HAL mutexes but may contain hidden global state or blocking behavior. | A wrapper mutex protects HAL calls, not necessarily callbacks, background tasks, LWIP/CYW43/USB interactions, or third-party internal globals. | Keep them documented as serialized wrappers; migrate protocol/device logic to shared HAL-native drivers where practical. |
| TS-A05 | Timer and interrupt callback contexts are mixed. | Code that is safe in task context may deadlock or corrupt state from ISR/alarm callbacks. | Do not change callback context silently. Document ISR/alarm vs task-service callback for every timer path before FreeRTOS timer integration. |

Stage 3 update (2026-06-11): TS-A03 is closed for the RP2040 Arduino backend
under `HAL_ENABLE_FREERTOS + __FREERTOS`: `hal_delay_ms()` uses `vTaskDelay()`
only when the scheduler is running, the caller is in task context, and the
current core is outside `hal_critical_section_*`; otherwise it busy-waits.
`hal_idle()` yields only in the same safe task context. STM32G474 FreeRTOS delay
handling remains a later-stage item.

## Lazy Mutex Inventory

These locations require review before a broad "FreeRTOS task-safe" claim:

| Area | Lazy/eager pattern | Stage 0 disposition |
|------|--------------------|---------------------|
| `SmartTimers` | Per-instance `_mutex` allocated lazily by `ensureMutex()`. | Fix first: create eagerly in constructor or `begin()` before sharing; current `ensureMutex()` can race in two tasks. |
| `multicoreWatchdog` | `watchdogTickMutex` is created in `setupWatchdog()`, guarded by boot-time contract. | Accept if `setupWatchdog()` remains single-task before scheduler/multicore sharing. |
| `hal_i2c`, `hal_spi` | Per-bus mutex allocated lazily in backend `*_ensure_mutex()`. | Prefer eager allocation in `*_init*()` and require bus init before runtime use; RP2040 lazy path is cross-core suspect. |
| `hal_adc`, `hal_pwm_freq`, `hal_rgb_led`, `hal_timer` | Singleton mutex allocated lazily on first runtime use. | Move to init/start path where one exists; otherwise add a safe once primitive. |
| `hal_serial` | Debug/TX mutexes are created in `hal_serial_init()` with lazy fallback. ISR fast path avoids mutexes. | Require `hal_serial_init()` before multitask logging; audit lazy fallback for FreeRTOS/SMP. |
| `hal_eeprom`, `hal_kv`, `hal_littlefs`, `hal_sdlogger` | Singleton mutexes allocated lazily. | Treat init/begin as the eager allocation point before tasks start. |
| `hal_wifi`, `hal_udp`, `hal_mqtt`, `hal_ota`, `hal_wireguard` | Singleton mutexes allocated lazily around Arduino-origin wrappers. | Keep wrapper serialized; make mutex creation eager before scheduler and document third-party limitations. |
| `hal_display`, `hal_external_adc`, `hal_gps`, `hal_onewire` | Shared-driver singleton/bus mutexes allocated lazily. | Prefer explicit init/reset creating the mutex before runtime use; preserve full-mask timing in OneWire. |
| Handle drivers (`hal_can`, RTC, thermocouple, DS18B20, digipot, PGA2311, modem, MCP9600/MAX6675/MCP2515 internals) | Per-handle mutex allocated during create/init. | Good runtime pattern; keep create/destroy single-task. |

## Module Classification

| Module / area | Setup / teardown | Runtime classification | Callback / ISR notes | Follow-up |
|---------------|------------------|------------------------|----------------------|-----------|
| `hal_app_entry`, `hal_app.h` | `HAL_PROVIDE_APP_ENTRY` supplies entrypoint. `HAL_ENABLE_APP_TASK1` is explicit opt-in for secondary dispatch. | No shared state. FreeRTOS entry mode not implemented yet. | RP2040 `loop1()` starts the core-1 path; now gated. | Stage 6 creates FreeRTOS tasks using same `HAL_ENABLE_APP_TASK1` gate. |
| `hal_sync` | Mutex create/destroy are lifecycle operations. | Non-RTOS RP2040 uses pico mutex; STM32 uses spinlock; mock uses `std::mutex`. | `hal_mutex_*` is not ISR-safe. `hal_critical_section_*` is hard full-mask and nesting-safe. | Add FreeRTOS path using priority-aware mutex/semaphore. Do not alias critical sections to `taskENTER_CRITICAL()`. |
| `hal_system` delay/time/idle/watchdog | Watchdog enable/reset policy is setup/runtime-specific. | Time queries are safe. RP2040 FreeRTOS builds now gate `hal_delay_ms` / `hal_idle` on scheduler state, ISR state, and HAL critical-section depth; `hal_delay_us` remains busy-wait/timing-safe. | `hal_in_isr()` exists for ISR-sensitive callers. | STM32G474 still needs FreeRTOS-aware delay/idle in its later runtime stage; keep `hal_delay_us` interrupt-independent. |
| `hal_gpio` / `hal_pcnt` | Pin setup and interrupt attach are setup-time. | Reads/writes are pass-through; same-pin concurrent writes need caller policy. PCNT runtime state depends on backend. | GPIO callbacks run in ISR context and must be ISR-safe. RP2040 PCNT uses GPIO edge ISR. | Document FreeRTOS ISR restrictions; no blocking HAL calls from GPIO ISR. |
| `hal_pwm`, `hal_pwm_freq`, `hal_dac`, `hal_adc` | Init/create/destroy or global configuration remain single-task. | `hal_adc` and `hal_pwm_freq_write` are mutex-protected; simple PWM/DAC writes are backend pass-throughs. | No user callbacks. | Fix lazy singleton creation for ADC/PWM frequency before RTOS claims. |
| `hal_timer`, `hal_timer_ext`, `hal_soft_timer` | Timer create/destroy/config remain lifecycle work. | `hal_timer`/`hal_timer_ext` use mutexes for managed state; `hal_soft_timer` inherits `SmartTimers`. | Low-level RP2040 alarms execute in interrupt-like context; `SmartTimers` callbacks run from caller's task/loop and outside the mutex. | Do not switch to FreeRTOS software timers without documenting callback context. Fix `SmartTimers` lazy mutex first. |
| `hal_serial` debug output | `hal_serial_init()` should run before multitask logging. | Normal debug/print paths use mutexes; ISR path queues records without mutex/lazy init. | `hal_debug_loop()` drains from task context; no-op in ISR. | Require/eagerly perform mutex init before scheduler; preserve ISR fast path. |
| `hal_uart`, `hal_swserial`, `hal_time` | Single-owner setup. | Currently documented not thread-safe; caller must serialize or use one owner task. | UART callbacks/transport use must not assume reentrancy. | Keep documented exception until a transport ownership model is designed. |
| `hal_i2c` master | `hal_i2c_init*` and deinit reconfigure shared bus objects; single-task setup/teardown. | Transfer helpers are bus-mutex protected; manual begin/write/end holds the bus across transaction. | No user ISR callback in master mode. | Make per-bus mutex creation eager/safe; audit read paths that intentionally release lock after request. |
| `hal_spi` master | `hal_spi_init/deinit` single-task. | Explicit `hal_spi_lock/unlock` protects multi-step operations; `begin_transaction` mirrors Arduino and does not lock by itself. | No user callbacks. | Ensure drivers consistently hold bus lock around begin/transfer/end. Make mutex creation eager/safe. |
| `hal_i2c_slave` | Init/deinit single-task. | Register read/write API is mutex-protected. | Arduino `onReceive` / `onRequest` callbacks touch register-map state; ISR/task safety is backend-specific. | Re-audit callback locking under FreeRTOS and document allowed APIs from callbacks. |
| `hal_eeprom`, `hal_kv` | `hal_eeprom_init` and `hal_kv_init` are setup-time. | Runtime calls are serialized by EEPROM/I2C or KV singleton mutexes. | No user callbacks. | Eagerly allocate singleton mutexes in init; avoid lazy creation in tasks. |
| `hal_littlefs`, `hal_sdlogger` | Mount/begin/close are lifecycle operations. | Public wrapper calls are serialized by singleton mutexes. | Logger/crash paths may run during fault/reset-sensitive conditions. | Keep Arduino-origin filesystem wrapper conservative; eager mutex creation before scheduler. |
| `hal_display` / TFT / SSD1306 | Display init and driver selection are setup-time. | Public drawing operations serialize on singleton display mutex; bus drivers add I2C/SPI protection. | No internal user callbacks. | Fix lazy display mutex; beware long drawing transactions blocking other tasks. |
| `hal_onewire`, `hal_ds18b20` | Bus/device create/destroy are lifecycle work. | Public DS18B20 operations use handle and shared bus mutexes. | Timing slots use hard critical sections and busy microsecond delays. | Preserve hard full-mask path under FreeRTOS; hardware smoke under scheduler load. |
| RTC (`hal_rtc`, PCF8563, DS3231) | Create/destroy/init single-task. | Per-handle mutex serializes runtime RTC calls; I2C bus lock protects transport. | No user callbacks. | Good per-handle pattern; keep lifecycle conservative. |
| Thermocouple (`hal_thermocouple`, MCP9600, MAX6675) | Create/destroy/init single-task. | Per-handle/device mutexes serialize runtime calls; transport locks protect I2C/GPIO bit-bang. | MAX6675 bit-bang timing should not be called from ISR. | Good per-handle pattern; verify timing paths under scheduler load. |
| `hal_external_adc` / ADS1X15 | Init modifies singleton state; setup-time. | Runtime calls use singleton mutex plus I2C bus lock. | Conversion wait uses delay/idle behavior. | Eager singleton mutex; make delay/idle FreeRTOS-aware. |
| `hal_digipot`, MCP401X, MAX5395 | Create/destroy single-task. | Per-instance mutex serializes facade runtime calls; I2C bus lock protects transport. | No callbacks. | Good reference pattern. |
| `hal_pga2311` | Create/destroy single-task. | Per-instance mutex serializes runtime calls; SPI lock protects transport. | No callbacks. | Good reference pattern; verify SPI lock coverage remains complete. |
| `hal_can`, shared MCP2515 | Channel create/destroy single-task. | Per-handle and driver mutexes serialize send/receive/config. | INT pin ISR is caller-provided; callbacks from drain helpers run in caller context. | Keep ISR handler minimal; document no blocking work in GPIO ISR. |
| `hal_gps` shared parser | Transport init/setup single-task. | Parser feed/accessors use singleton mutex. | No ISR callback; feed usually from UART polling task/loop. | Eager parser mutex or safe once; decide UART ownership under RTOS. |
| `hal_modem_at`, `hal_simcom_a76xx` | Create/destroy/connect session setup single-task unless documented. | Per-handle modem mutex serializes AT sessions and A76xx wrappers. | Tick callback runs with engine mutex held and must not call modem APIs; MQTT message callback path must avoid lock recursion. | Keep callbacks documented; consider a modem owner task for FreeRTOS projects. |
| `hal_wifi`, `hal_udp`, `hal_mqtt`, `hal_ota`, `hal_wireguard` | Begin/configure/connect lifecycle should be single-task. | HAL wrappers use singleton mutexes around Arduino-origin libraries. | MQTT callbacks dispatch outside module mutex; OTA callbacks dispatch from `hal_ota_handle()`. | Treat as serialized but not fully audited third-party code. Eager mutexes; migrate long-term to HAL-native transports where possible. |
| `hal_crypto`, `hal_bits`, `hal_math`, serialization helpers | No lifecycle. | Stateless; safe when buffers/objects are not shared unsafely by callers. | No callbacks. | No FreeRTOS-specific work expected. |
| `pidController`, `hal_pid_controller` | Instance setup/reset single-owner. | Documented not thread-safe; callers must serialize or use one instance per task/control loop. | No callbacks. | Keep documented exception unless locking is added intentionally. |
| `SmartTimers` | Constructor/begin configure timer. | Intended per-instance mutex protection, but current lazy `_mutex` creation is race-prone in two tasks. | Callback runs outside mutex in caller context. | Fix before using as FreeRTOS-safe foundation for `hal_soft_timer` or `multicoreWatchdog`. |
| `multicoreWatchdog` | `setupWatchdog()` is boot-time single-task. | Runtime update paths lock around shared `SmartTimers::tick()`. | Reboot diagnostic callback runs during setup after watchdog reboot handling. | Accept with setup-time mutex creation; inherits `SmartTimers` fix. |
| `draw7Segment` | No own lifecycle. | Delegates to `hal_display`; safe only where display backend is safe. | No callbacks. | No separate FreeRTOS work beyond display. |

## Priority Follow-Ups (After FreeRTOS implementation done)

1. Fix `SmartTimers` mutex creation by making the mutex available before the
   object is shared across tasks.
2. Add or choose a safe once/eager-init pattern for singleton/bus mutexes before
   claiming FreeRTOS task safety on RP2040 SMP.
3. Implement FreeRTOS-aware `hal_sync` paths while preserving hard full-mask
   critical sections.
4. Gate `hal_delay_ms()` on scheduler state and context. (Closed for RP2040 in
   Stage 3; STM32G474 remains later-stage work.)
5. Re-audit Arduino-origin wrappers after RP2040 FreeRTOS builds compile, with
   special attention to callbacks and hidden background tasks.
6. Preserve and hardware-test timing-sensitive OneWire/RGB/MAX6675 paths under
   scheduler load.

## Stage 0 Result

Stage 0 is documentation-only and does not change runtime behavior. The audit
identifies the concrete modules that can inherit FreeRTOS task safety from a
future `hal_sync` backend, the modules that remain single-owner exceptions, and
the lazy-mutex/timing/callback follow-ups that must be closed before enabling a
general `HAL_ENABLE_FREERTOS` runtime contract.
