# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-04-14

### Added
- New `hal_i2c_slave` module — I2C peripheral (slave/target) mode with
  register-map interface:
  - `hal_i2c_slave_init[_bus]()` / `hal_i2c_slave_deinit[_bus]()`
  - `hal_i2c_slave_reg_write8[_bus]()` / `hal_i2c_slave_reg_write16[_bus]()`
  - `hal_i2c_slave_reg_read8[_bus]()` / `hal_i2c_slave_reg_read16[_bus]()`
  - `hal_i2c_slave_get_address[_bus]()`
  - Configurable map size via `HAL_I2C_SLAVE_REG_MAP_SIZE` (default 32)
  - Per-bus mutex for multicore-safe register access (Arduino backend)
  - Mock backend with `simulate_receive` / `simulate_request` helpers
  - New `HAL_DISABLE_I2C_SLAVE` feature flag
- New host test suite `tests/test_hal_i2c_slave.cpp` (21 tests) covering:
  - Init/deinit, address query, bus independence
  - 8-bit and 16-bit register read/write roundtrips
  - Big-endian 16-bit layout verification
  - Out-of-range boundary checks
  - Mock simulate receive (master-write) and request (master-read)
  - Register map clearing on re-init
- JaszczurHAL API docs updated with full `hal_i2c_slave` section.
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
