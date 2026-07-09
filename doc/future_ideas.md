# JaszczurHAL - Future Ideas

Simple backlog of future architecture and implementation work.

- High priority: shared network architecture for STM32 WiFi modem first.
  - Treat STM32 connectivity as the next major platform-unlock item, not as a
    small target-specific patch.
  - Reuse lessons and helper patterns from the existing `hal_modem_at`
    generic AT-command engine and `hal_simcom_a76xx` cellular driver. They
    already cover UART-backed AT command/response flow, URC dispatch,
    watchdog-friendly waits, response buffering and modem-family layering.
    The WiFi work still needs a separate ESP-AT/network contract; do not fork
    the cellular API shape blindly.
  - Use an offloaded WiFi module first. ESP8266MOD is acceptable for prototype
    work and for proving the architecture, but do not name the architecture
    after ESP8266. ESP8266EX is already marked by Espressif as not recommended
    for new designs, so keep the design replaceable by ESP32-C3/S3-class
    modules or other AT/offloaded modules.
  - Prefer role-based flags over chip-based API names:
    `HAL_ENABLE_WIFI_MODEM`, `HAL_ENABLE_ESP_AT`, and only optionally a
    narrower `HAL_ENABLE_ESP8266_AT` profile.
  - Add a target-neutral internal network interface layer, for example
    `hal_netif_ops`, underneath the public `hal_wifi`, `hal_net`, `hal_udp`,
    `hal_tcp` and BSD sockets APIs. Do not expose this as the primary public
    user API unless it proves necessary.
  - The shared layer should own behavior, state machines and tests:
    WiFi scan/connect/disconnect/status/RSSI/MAC/IP, DNS resolution, TCP/UDP
    open/send/recv/close, timeout semantics, reconnect handling, socket state
    and error mapping.
  - Backend-specific adapters should only bind that contract to transport:
    STM32G474 -> ESP-AT over `hal_uart` plus optional reset/enable GPIO;
    RP2040 -> current Pico W / CYW43 / Arduino-Pico network stack;
    future ESP32 -> native Arduino-ESP32 or ESP-IDF backend.
  - Place reusable ESP-AT logic under `src/hal/impl/shared/drivers/esp_at/`
    or a similarly named shared network/modem folder. Keep transport abstract:
    read/write/flush/reset/time/lock should come from a small adapter over HAL
    UART/GPIO/system primitives.
  - The ESP-AT parser must handle asynchronous URCs interleaved with command
    responses, command serialization with one modem lock, `CIPMUX` link IDs,
    partial receives, disconnect notifications and reset/resync after parser
    loss.
  - Initial STM32 milestone:
    1. `AT` probe, firmware/version query, reset and echo-off.
    2. STA mode, scan, connect, disconnect, status, RSSI, MAC and IP.
    3. `hal_net_resolve_ipv4`.
    4. TCP client sockets.
    5. UDP sockets.
    6. BSD sockets compatibility over the existing adapter.
    7. MQTT only after TCP behavior is stable.
  - Keep TLS, OTA and WireGuard out of the first milestone. Add TLS only when
    the selected module provides reliable TLS offload and the API can report
    meaningful errors.
  - Hardware notes for ESP8266-class modules: require solid 3.3 V power with
    current headroom for WiFi TX peaks, local decoupling, proper boot strap
    pins, and explicit `EN`/`RST` control where possible.

- Investigate occasional Fiesta CDC / serial-debug link drops during monitor
  sessions.
  - Not critical for runtime operation because it has not been observed with
    USB disconnected.
  - Symptoms are intermittent/random rather than reliably reproducible.
  - Observed trigger: starting/stopping the persistent serial monitor can
    make Pico-class boards disappear/reappear from the host-side CDC view,
    especially when the monitor is terminated brutally (for example by `kill`)
    instead of exiting cleanly.
  - Important refinement: this currently looks more like USB CDC disconnect /
    reconnect or host-side re-enumeration than a full MCU reset. The Fiesta app
    can keep running normally, visible on the display, while the USB serial
    connection drops or restarts from the host perspective.
  - Current suspicion: behaviour may depend on the USB hub/host path used for
    the connected module.
  - Likely blind alleys unless new evidence appears:
    1200-baud bootloader touch does not match the symptom if the board returns
    to the running app rather than UF2/upload mode; watchdog reset also does
    not match when the display/app loop keeps running continuously.
  - Diagnostic angles: distinguish physical USB connect/disconnect from serial
    port open/close, upload handoff and monitor reconnect; check for 1200-baud
    bootloader touch, DTR/HUPCL side effects, CDC TX backpressure, excessive
    debug logging, watchdog resets and power/ground disturbance through USB.
    Correlate device uptime/display refresh with host `dmesg -w` and
    `udevadm monitor --kernel --property --subsystem-match=tty` so MCU reset,
    CDC-only re-enumeration and hub/port reset are separated cleanly.

- Continue FreeRTOS hardening.
  - Harden module-level synchronization and ownership.
  - Document callback contexts, task contexts and ISR/task boundaries.
  - Run hardware smoke tests before considering FreeRTOS as a default runtime.

- Expand status-returning APIs without breaking existing wrappers.
  - Shared `hal_status_t` exists; add `_ex` functions returning it where useful.
  - Keep current `bool` / `NULL` APIs as compatibility wrappers.
  - Start with statuses that can be reported accurately today.
  - Some historical `_ex` APIs are not status-returning. Treat
    `hal_wifi_ping_ex()`, `hal_rgb_led_init_ex()` and
    `hal_display_init_ssd1306_i2c_ex()` as naming debt/suffix collisions, not
    as completed `hal_status_t` work.
  - Improve backend-specific error mapping only where the backend can report
    it honestly; do not invent precision just to fill enum cases.
  - Progress:
    - DONE: digipot init/set-resistance now expose
      `hal_digipot_init_ex()` and `hal_digipot_set_resistance_ex()` while
      preserving the legacy handle/`bool` wrappers. The MCP401x/MAX5395 shared
      drivers report invalid config/range, pool exhaustion, I2C bus failures
      and MCP401x read-back mismatches through `hal_status_t`.
    - DONE: DAC init/raw-write/millivolt-write now expose
      `hal_dac_init_ex()`, `hal_dac_write_ex()` and
      `hal_dac_write_millivolts_ex()` while preserving existing `bool`/`void`
      wrappers. Mock/STM32G474 report invalid channels and uninitialized
      writes; RP2040 reports `HAL_EUNSUPPORTED`.
    - DONE: PCNT init/read/reset/read-and-reset now expose
      `hal_pcnt_init_ex()`, `hal_pcnt_read_ex()`, `hal_pcnt_reset_ex()` and
      `hal_pcnt_read_and_reset_ex()` while preserving legacy wrappers. Current
      backends report invalid channels/edges and uninitialized channel access.
    - DONE: system device UID hex formatting now exposes
      `hal_get_device_uid_hex_ex()` while preserving the legacy `bool` wrapper.
      It reports `HAL_EINVAL` for NULL buffers and `HAL_EOVERFLOW` for
      insufficient output buffers.
    - DONE: I2C master now exposes status-returning `_ex` variants for bus
      init, clock changes, end-transmission, one-byte write/read helpers,
      write-read/read-bytes transfers, legacy request-from count reporting and
      bus-clear while preserving existing `void`/`uint8_t`/`bool`
      compatibility wrappers. Mock/RP2040/STM32G474 map invalid buses,
      invalid buffers, uninitialized buses, bus errors and timeouts to
      `hal_status_t` where the backend can report them.
    - DONE: UART now exposes status-returning `_ex` variants for pin changes,
      begin, one-byte read, write, println, flush and error-counter reads while
      preserving legacy `bool`/`void`/`int`/`size_t` wrappers. Mock/STM32G474
      report invalid arguments, empty reads and mock capture overflow; RP2040
      also reports uninitialized UART use and pin changes rejected while the
      port is running.
    - DONE: several shared device drivers already use `hal_status_t` APIs and
      should not be counted as remaining transport/API backlog: simple I/O
      expanders and helpers (`PCA9654E`, `PCF8574`, `MCP23017`, `HC595`,
      `MCP3221`, `MCP4725`), `BH1750`, `TSC2007`, `STMPE610` and the PN532
      transport classes.
  - Current quick audit should be refreshed before large status-conversion
    work. Treat the remaining lists below as qualitative priority buckets, not
    as an exact count.
  - Highest-priority shared transport APIs: SPI/DMA candidates:
    `hal_spi_init()`, `hal_spi_begin_transaction()`,
    `hal_spi_transfer()`, `hal_spi_transfer16()`,
    `hal_spi_transfer_buffer()`, `hal_spi_transfer_txrx()`,
    `hal_spi_write()`, `hal_spi_write_dma()`,
    `hal_spi_write_dma_async_start()` and `hal_spi_write_dma_async_wait()`.
  - Serial candidates: matching `hal_swserial_*` setup, read, write, println
    and flush paths. `hal_swserial` is already a shared implementation used by
    RP2040, STM32G474 and mock builds; this is status/API polish, not a
    missing-backend task.
  - Storage candidates: `hal_kv_init()`, `hal_kv_set_u32()`,
    `hal_kv_get_u32()`, `hal_kv_set_blob()`, `hal_kv_get_blob()`,
    `hal_kv_delete()`, `hal_kv_gc()`, `hal_kv_commit()`,
    `hal_littlefs_begin()`, `hal_littlefs_format()`,
    `hal_littlefs_remove()`, `hal_sdlogger_init()`,
    `hal_sdlogger_crash_init()` and append/close/report paths that currently
    return `void`.
  - Network candidates: `hal_wifi_set_mode()`, `hal_wifi_disconnect()`,
    `hal_wifi_set_hostname()`, `hal_wifi_begin_station()`,
    `hal_wifi_get_local_ip()`, `hal_wifi_get_dns_ip()`,
    `hal_wifi_get_mac()`, status-returning ping API around the historical
    `hal_wifi_ping()` / int-returning `hal_wifi_ping_ex()`,
    `hal_wifi_scan_networks()`, `hal_wifi_get_scan_result()`,
    `hal_net_resolve_ipv4()`,
    `hal_tcp_socket_connect()`, TCP send/recv/bind/listen/accept paths,
    UDP bind/sendto/recvfrom/begin-packet/write/end-packet paths, MQTT
    connect/publish/subscribe/config paths and WireGuard begin/peer/handshake
    paths.
  - Use existing tests as a validation if changes do not break anything.
  - Display candidates: `hal_display_init_ssd1306_i2c_ex()` currently has an
    `_ex` suffix but still returns `bool`; migrate it and the configure,
    drawing, pixel-write, DMA, text and text-preparation paths as a coherent
    display status pass.
  - Sensor/time/output candidates: RTC get/set/control paths, DHT read/sample,
    DS18B20 request/take-latest, external ADC init/read, thermocouple read and
    configuration paths, IR decoder init/control/readout, DMA PWM audio
    start/pause/resume, PGA2311 set/get conversion helpers and RGB LED
    init/set paths. Be careful with `hal_rgb_led_init_ex()`: it already exists
    as a pixel-type overload and returns `void`, so a status migration needs a
    new name or a deliberate compatibility plan.
  - Lower priority: functions that are intentionally predicates, cheap cached
    getters, lifecycle `destroy/deinit/stop` calls, or compatibility wrappers
    over already status-returning APIs.

- Continue CAN API v2 follow-up work.
  - Add interrupt-driven RX/TX completion paths.
  - Consider callback-style filter ownership only when interrupts need it.
  - Add transceiver enable/standby abstraction.
  - Add stronger mock/loopback coverage for filters, CAN FD shape and states.
  - Validate the STM32G474 native FDCAN backend on hardware.
  - Keep Zephyr CAN as a reference checklist for timing, states, filters,
    error counters and transceiver handling.

- ADP5360 shared I2C PMIC driver.
  - DONE: added a Zephyr-inspired shared HAL-only ADP5360 driver under
    `src/hal/impl/shared/drivers/adp5360/`, public `hal_adp5360.h`, host
    tests, API docs and `examples/54_adp5360_pmic`.
  - Remaining follow-up: optional INT/PGOOD/reset-status GPIO callback support
    if an application needs interrupt-driven PMIC events.

- Add optional 10-bit I2C master addressing.
  - The whole public I2C API takes `uint8_t address` (7-bit). A 10-bit
    address spans `0x000`-`0x3FF`, so it needs a wider value (`uint16_t`)
    plus an explicit addressing-mode marker: the value alone cannot be
    distinguished from a 7-bit address.
  - Prefer an additive, non-breaking design over widening the existing ABI.
    Widen only the internal transfer helpers to `(uint16_t addr,
    bool addr_10bit)`; keep every current public function delegating with
    `addr_10bit=false` so existing code and shared drivers are untouched.
  - Expose a narrow set of new 10-bit entry points for the atomic modern
    paths that real 10-bit devices use, e.g. `hal_i2c_write_read10_ex()`,
    `hal_i2c_read_bytes10_ex()` and a raw-command write. Do not double the
    whole API surface.
  - Scope out (document as intentional): the legacy buffered Arduino-Wire
    path (`hal_i2c_begin_transmission()` / `cur_addr` are `uint8_t`, and
    Wire semantics are 7-bit anyway) and `hal_i2c_slave` 10-bit target mode
    (separate follow-up).
  - Backend feasibility:
    - STM32G474 (register-level): straightforward. In `i2c_hw_write/read/
      write_read/ack`, program `CR2` with the `ADD10` bit set, place the full
      address in `SADD[9:0]` without the `<<1` shift, and handle `HEAD10R`
      for the read phase. Add the missing `ADD10`/`HEAD10R` bit defines to
      the register map.
    - RP2040: the backend calls Pico SDK `i2c_read_timeout_us()` /
      `i2c_write_timeout_us()`, whose `uint8_t addr` signatures program a
      7-bit `IC_TAR`. 10-bit master needs `IC_CON.IC_10BITADDR_MASTER` and a
      full 10-bit `IC_TAR`, so add a small custom path instead of relying on
      those SDK helpers. This is the most work of the three.
    - Mock: trivial. Widen the stored address and extend expectation matching.
  - Validation: 10-bit addresses use the `11110xx` prefix on the wire; the
    mode helper must not confuse them with reserved 7-bit ranges. Add
    mock-based unit tests for 10-bit transactions; real ACK behavior on
    STM32/RP2040 needs an actual 10-bit device on the bus.
  - Suggested order: land STM32 + mock + tests first (lowest risk), then the
    RP2040 SDK path.

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
  - Display-driver migration notes:
    - The most valuable Zephyr display idea is the generic raw write contract:
      pixel format, buffer descriptor with `pitch`, `width`, `height` and
      `buf_size`, capabilities reporting and `write(x, y, desc, buf)` style
      area updates. The first additive JaszczurHAL type layer now exists as
      `hal_display_pixel_format_t` and `hal_display_buffer_desc_t`; next add
      capabilities reporting and a status-returning area-write API before
      importing more panel controllers.
    - Keep the current JaszczurHAL GFX/text/high-level display API. Treat the
      Zephyr-style API as a lower-level raw blit/capabilities layer underneath
      or beside it.
    - First adapt existing JaszczurHAL display backends (`ILI9341`, `ST7735`,
      `ST7789`, `ST7796S`, `SSD1306`) to the raw write/capabilities layer.
      This should preserve existing examples and public wrappers.
    - Reuse Zephyr's proven handling for `pitch > width` by splitting writes
      into row-sized transfers, and keep RGB565 byte order explicit.
    - Extend the SSD1306-family driver next: Zephyr has useful reference
      support for `SSD1309`, `SSD1315`, `SH1106` and `CH1115`, including
      command addressing differences, segment/page offsets, orientation,
      contrast, suspend/resume and I2C/SPI bus splitting.
    - After the raw API exists, consider new panel families in this order:
      `GC9A01`, `SSD1331`/`SSD135x`, `ST7567`, then e-paper controllers such
      as `SSD16xx`/`UC81xx`.
    - Defer DSI/LTDC/MCUX/QEMU/SDL/Renesas-style Zephyr display backends for
      now; they do not map cleanly to the current shared RP2040/STM32G474 HAL
      goals.
    - Treat ILI9xxx GRAM readback and tearing-effect/vblank handling as later
      optional work. They need stronger SPI/MIPI-DBI read and callback
      contracts than the current display layer exposes.
    - Make display raw APIs status-returning from the start (`hal_status_t`)
      and guard shared display state with the existing display mutex pattern
      based on `jh_hal_mutex_create_once`.

- Continue STM32 backend catch-up.
  - Focus on modules that still lack real STM32G474 backends:
    `mqtt`, `ota`, `udp`, `wifi` and `wireguard`.
  - `hal_swserial` is no longer a STM32 backend gap; it lives in the shared
    driver layer over HAL GPIO/time/sync and is used by the STM32 GPS path.
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
