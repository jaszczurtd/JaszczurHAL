# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-04-21

### Added
- `hal_i2c_write_byte(address, data, *outWriteOk)` /
  `hal_i2c_write_byte_bus(bus, address, data, *outWriteOk)` — one-shot
  convenience helper that performs the common "beginTransmission +
  write one byte + endTransmission" sequence. The internal I2C mutex
  is acquired and released automatically; the optional `outWriteOk`
  pointer receives the queue-byte status, and the return value is the
  `endTransmission` error code. Extracted from ECU PCF8574/Adjustometer
  call sites that previously open-coded the three-step pattern.
- `hal_i2c_read_byte(address, *outReadOk)` /
  `hal_i2c_read_byte_bus(bus, address, *outReadOk)` — symmetric read
  counterpart: requests and returns a single byte from a slave. The
  optional `outReadOk` pointer lets callers distinguish a valid `0x00`
  value from a failed transaction (request_from short read or
  `hal_i2c_read()` returning -1). Extracted from ECU PCF8574 read path.
- Tests: 6 new `hal_i2c_write_byte` tests (begin/write/end sequence,
  busy-bus NACK path, NULL out-flag, lock-depth balance, per-bus
  routing, transaction-count increment) and 6 new `hal_i2c_read_byte`
  tests (injected-byte round-trip, zero value vs. failure disambiguation,
  NULL out-flag, lock-depth balance, per-bus routing, transaction-count
  increment), plus 1 test verifying that `hal_i2c_read_byte()` holds
  the mutex during the actual byte read. Total I2C test count: 30.
- `hal_i2c_bus_clear(sda_pin, scl_pin)` /
  `hal_i2c_bus_clear_bus(bus, sda_pin, scl_pin)` — I2C bus clear
  procedure per I2C specification §3.1.16: toggles SCL up to 9 times
  at GPIO level to release a slave holding SDA low, then generates a
  STOP condition. Must be called before `hal_i2c_init()`.
  Arduino implementation uses native Arduino GPIO primitives.
- Mock: `hal_mock_i2c_get_bus_clear_count()` /
  `hal_mock_i2c_get_bus_clear_count_bus(bus)` — return the number of
  `hal_i2c_bus_clear()` calls since the last `hal_i2c_init()`.
- Tests: 3 new I2C bus clear tests (count, reset-on-init, bus
  independence). Total I2C test count: 17.
- `hal_serial_available()` — return the number of bytes available for
  reading from the serial port (wraps `Serial.available()`).
- `hal_serial_read()` — read one byte from the serial port; returns
  0–255 or -1 when empty (wraps `Serial.read()`).
- `float_to_u32()` / `u32_to_float()` — `static inline` bitcast helpers
  (float ↔ uint32_t via memcpy) in `tools_api.h`.
- Mock: `hal_mock_serial_inject_rx(data, len)` — inject bytes into the
  mock serial RX buffer for testing `hal_serial_available/read`.
- `hal_i2c_slave_get_transaction_count()` and
  `hal_i2c_slave_get_transaction_count_bus(uint8_t bus)` — return the
  number of completed I2C bus transactions (master reads and writes)
  since initialisation. Incremented inside the Wire `onReceive` /
  `onRequest` callbacks, so the counter reflects genuine bus activity.
  Resets to 0 on `hal_i2c_slave_init*()`. Wraps at `UINT32_MAX`.
  Thread-safe (atomic access).
- `hal_i2c_get_transaction_count()` and
  `hal_i2c_get_transaction_count_bus(uint8_t bus)` — symmetric API
  for the I2C master (controller) side. Incremented on every
  `hal_i2c_end_transmission*()` (write) and `hal_i2c_request_from*()`
  (read). Resets to 0 on `hal_i2c_init*()`. Wraps at `UINT32_MAX`.

### Changed
- `hal_pwm_freq_create()` no longer starts the PWM slice immediately.
  The GPIO function and slice enable are deferred until the first
  `hal_pwm_freq_write()` call, preventing a power-on glitch on pins
  with inverted logic (0 % duty = actuator ON).

### Fixed
- Mock `hal_i2c_end_transmission_bus()` now returns 2 (NACK on address)
  when the mock busy flag is set. Previously it always returned 0,
  making it impossible to test I2C error paths.
- `hal_i2c_read_byte()` / `hal_i2c_read_byte_bus()` now hold the internal
  I2C mutex across the full one-shot `request + read` sequence, making
  the helper atomic on a given bus (symmetric with `hal_i2c_write_byte()`).
- Mock: added `hal_mock_i2c_get_read_byte_lock_depth()` /
  `hal_mock_i2c_get_read_byte_lock_depth_bus()` to expose lock-depth
  captured at the byte-read point inside one-shot `read_byte` helpers
  for mutex-behavior tests.

### Changed
- `hal_i2c_slave_reg_write8[_bus]()` and
  `hal_i2c_slave_reg_write16[_bus]()` return `void` again (reverts the
  short-lived return-value approach: writes always target the local
  register-map buffer and therefore always succeed, making return values
  meaningless for detecting real bus activity).

## [1.4.0] - 2026-04-14

### Added
- New `HAL_TOOLS_ADC_MAXVALUE` macro in `tools_sensor_config.h`, derived from
  `HAL_TOOLS_ADC_BITS` (default 12).

### Changed
- `getAverageValueFrom()` now performs a dummy read before the sampling loop
  to avoid RP2040 ADC mux cross-talk (residual charge from the previous channel).
- `ntcToTemp()` uses `HAL_TOOLS_ADC_MAXVALUE` instead of hardcoded `4095.0`,
  enabling correct operation on ADC systems with non-12-bit resolution.
- PID controller: clamping anti-windup — integral accumulation is now skipped
  when the output is saturated in the direction of the error, preventing
  windup at output limits. Existing hard clamp via `setMaxIntegral()` retained
  as secondary safeguard.

## [1.3.0] - 2026-04-10

### Added
- New table-based soft timer APIs in hal_soft_timer:
  - hal_soft_timer_table_entry_t
  - hal_soft_timer_setup_table(...)
  - hal_soft_timer_tick_table(...)
- New CAN payload helper in hal_can:
  - hal_can_encode_temp_i8(...)
- New host test suite tests/test_hal_soft_timer.cpp covering:
  - Core hal_soft_timer wrapper behavior
  - Table setup/tick behavior
  - Idle callback and inter-entry delay behavior
  - Invalid input handling
- New test_hal_can coverage for hal_can_encode_temp_i8(...) positive/negative values,
  range saturation, and fractional truncation.
- New README quick example for table-based soft timer setup/tick usage.

### Changed
- hal_soft_timer_setup_table(...) and hal_soft_timer_tick_table(...) now return bool.
- Both table helpers now validate input arguments:
  - Return false when table is NULL
  - Return false when count is 0
  - Emit hal_derr(...) log on invalid input
- JaszczurHAL API docs updated to include table APIs, return contract, and validation behavior.
- JaszczurHAL docs updated to include the shared CAN temperature-byte helper.
- Test build configuration updated:
  - Added src/hal/hal_soft_timer.cpp to host test utility sources
  - Registered test_hal_soft_timer in tests/CMakeLists.txt
