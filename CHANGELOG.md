# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-04-15

### Added
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
