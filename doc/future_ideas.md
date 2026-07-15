# JaszczurHAL - Future Ideas

Unresolved architecture, implementation and hardware-validation work. Finished
items are intentionally removed instead of being retained as history; completed
work belongs in `doc/CHANGELOG.md`.

## High priority

### Implement shared offloaded networking for STM32G474

- Treat STM32 connectivity as a platform feature, not as a small
  target-specific patch. The currently missing STM32 backends are `wifi`,
  `udp`, `tcp`, `mqtt`, `ota` and `wireguard`.
- Start with an offloaded WiFi module. ESP8266MOD is acceptable for proving the
  architecture, but keep the design replaceable by ESP32-C3/S3-class or other
  AT/offloaded modules.
- Reuse transport and parser lessons from `hal_modem_at` and
  `hal_simcom_a76xx`, but define a separate ESP-AT/network contract instead of
  copying the cellular API shape.
- Prefer role-based flags such as `HAL_ENABLE_WIFI_MODEM` and
  `HAL_ENABLE_ESP_AT`; use a chip-specific profile only where controller
  differences require it.
- Add a target-neutral internal network interface layer underneath public
  `hal_wifi`, `hal_net`, `hal_udp`, `hal_tcp` and BSD sockets APIs. Keep it
  internal unless a public use case emerges.
- Put reusable ESP-AT behavior under a shared driver/network folder. Bind its
  read/write/flush/reset/time/lock transport to HAL UART/GPIO/system APIs.
- The shared layer must own and test WiFi scan/connect/disconnect/status,
  RSSI/MAC/IP, DNS, TCP/UDP lifecycle, timeouts, partial receives, reconnects,
  socket state and error mapping.
- The ESP-AT parser must support asynchronous URCs interleaved with command
  responses, serialized commands, `CIPMUX` link IDs, disconnect notifications
  and reset/resynchronization after parser loss.
- Initial STM32 milestone:
  1. `AT` probe, firmware query, reset and echo-off.
  2. STA scan/connect/disconnect, status, RSSI, MAC and IP.
  3. `hal_net_resolve_ipv4`.
  4. TCP client sockets.
  5. UDP sockets.
  6. BSD sockets compatibility.
  7. MQTT after TCP behavior is stable.
- Keep TLS, OTA and WireGuard out of the first milestone. Add TLS only when the
  module provides reliable offload and meaningful error reporting. Define a
  separate STM32 flash/update strategy before enabling OTA.
- Hardware integration must document 3.3 V current headroom, local decoupling,
  boot straps and explicit `EN`/`RST` control for ESP8266-class modules.

### Validate and harden the STM32G474 backend on hardware

- Run on-silicon validation on Nucleo-G474RE for all register-level backends,
  especially GPIO IRQ routing, TIM6 alarm jitter/latency, PWM mapping, SPI/I2C
  pin validation, RTC, CAN/FDCAN, display and RGB LED timing.
- Add repeatable hardware smoke tests for GPIO, UART, I2C, SPI, ADC, CAN,
  timers, RTC and display.
- Expand STM32-targeted regression tests beyond the current system, timer,
  I2C-slave and PWM-clock host coverage, prioritizing GPIO IRQ, I2C master,
  SPI, CAN/FDCAN and RTC integration seams.

### Continue FreeRTOS hardening

- Complete the remaining per-module synchronization/ownership audit, including
  timer callback contexts, Arduino-origin wrapper internals and documented
  single-owner modules.
- Create a canonical thread-safety matrix (the previously referenced audit file
  never existed), or fold the full matrix into an existing canonical document.
- Document callback task/ISR contexts and teardown ownership wherever the
  public contract is still ambiguous.
- Run RP2040 and STM32G474 hardware smoke tests before considering FreeRTOS a
  default runtime.

### Complete CAN v2 hardware and interrupt work

- Add interrupt-driven RX and TX-completion paths.
- Add callback/filter ownership only when required by the interrupt model.
- Add a transceiver enable/standby abstraction.
- Validate STM32G474 native FDCAN, CAN FD timing, filters, state transitions and
  error counters on real hardware.
- Continue using Zephyr CAN as a reference checklist for timing, states,
  filters, error counters and transceiver handling.

## Bus and device follow-up

### Add optional 10-bit I2C master addressing

- Keep the existing `uint8_t` 7-bit public ABI intact. Widen internal atomic
  transfer helpers to `(uint16_t address, bool address_10bit)` and make current
  APIs delegate with `address_10bit=false`.
- Add only the modern atomic entry points needed by real 10-bit devices, for
  example `hal_i2c_write_read10_ex()`, `hal_i2c_read_bytes10_ex()` and a raw
  command write. Do not duplicate the entire API.
- Leave the legacy buffered Arduino-Wire path and 10-bit slave mode out of the
  first implementation.
- STM32G474: implement `CR2.ADD10`, full `SADD[9:0]` handling and `HEAD10R` for
  reads; add the missing register definitions.
- RP2040: implement a custom hardware path using `IC_10BITADDR_MASTER` and the
  full `IC_TAR`, because Pico SDK timeout helpers accept only `uint8_t` 7-bit
  addresses.
- Mock: widen stored addresses and expectation matching.
- Add mock tests for 10-bit wire/address semantics and validate ACK behavior on
  both hardware targets with a real 10-bit device.
- Suggested order: STM32G474 + mock + tests, then RP2040.

### Add optional ADP5360 GPIO event support

- Add INT/PGOOD/reset-status GPIO handling and callback registration only when
  an application needs interrupt-driven PMIC events.

## Display follow-up

- Support `pitch > width` in `hal_display_write_raw_ex()` by splitting padded
  buffers into row-sized backend transfers. The descriptor already represents
  larger pitch, but hardware backends currently return `HAL_EUNSUPPORTED`.
- Validate SSD16xx and UC81xx on representative physical panels, including
  vendor-provided full/partial LUT profiles, BUSY polarity and temperature
  behavior.
- Add ILI9xxx GRAM readback and tearing-effect/vblank handling only after the
  SPI/MIPI-DBI read and callback contracts are defined.
- Evaluate TFT bulk-write/DMA paths only if measurements show that throughput
  or CPU cost justifies the extra backend complexity.
- Reconsider DSI/LTDC/MCUX/QEMU/SDL/Renesas-style display backends only when a
  supported target requires one; they do not currently fit the shared
  RP2040/STM32G474 HAL.

## Tooling and compatibility follow-up

### Investigate Fiesta CDC/serial monitor link drops

- Determine whether intermittent monitor-session failures are CDC-only
  re-enumeration, USB hub/port reset or an MCU reset. The application can keep
  running on the display while the host serial view disappears.
- Reproduce with clean monitor shutdown and brutal termination, different USB
  hub paths, DTR/HUPCL settings, debug load and CDC TX backpressure.
- Correlate device uptime with `dmesg -w` and
  `udevadm monitor --kernel --property --subsystem-match=tty`.
- Treat 1200-baud bootloader touch and watchdog reset as secondary hypotheses
  unless the board enters upload mode or application uptime resets.

### Split the legacy utility umbrella where it improves consumers

- Keep `JaszczurHAL.h` and `hal/hal.h` as the primary portable includes and
  `tools.h` as an optional compatibility umbrella.
- Consider focused headers for numeric, string, debug-alias and network helper
  groups without changing existing includes.
- Move new convenience functions into the appropriate HAL module instead of
  expanding the legacy tools surface.

### Add an optional firmware board/device description layer

- Keep device pin maps, bus IDs, addresses and defaults outside the HAL core.
- Make this distinct from the existing `vscode/targets` build-board registry,
  which selects toolchains/FQBNs but does not describe attached devices.
- Start with plain reusable headers; let small applications continue passing
  config structs directly.
- Add JSON/YAML generation only after a concrete multi-board requirement
  justifies it.

## Conditional architecture work

### Revisit static pools only when RAM pressure is measured

- Keep compile-time maximum-instance controls while optional modules remain
  small and dead-code elimination is effective.
- Consider linker-section device registration only after verifying memory
  pressure and compatibility with Arduino-Pico, STM32 linker scripts,
  host/mock tests and dead-code elimination.
