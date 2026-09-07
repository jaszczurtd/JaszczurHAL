/**
 * @file app.c
 * @brief Find I2C devices that acknowledge addresses 0x08 through 0x77.
 *
 * This application is wired for NUCLEO-G474RE: I2C1 SCL on PB8 and SDA on PB9,
 * with external pull-ups to 3.3 V. Check the hard-coded pins before using an
 * RP build listed in the project manifest. See README.md for wiring and limits.
 */

#include <hal/core/hal_app.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL G474 I2C scanner ===");
  deb("I2C1: SCL=PB8, SDA=PB9 (external pull-ups to 3V3 required)");

  const hal_status_t status = hal_i2c_init(25u, 24u, 100000u);
  if (status != HAL_OK) {
    deb("I2C init failed: %s", hal_status_to_string(status));
  }
}

void app_task0(void) {
  uint8_t addresses[HAL_I2C_SCAN_ADDRESS_COUNT];
  size_t found = 0u;
  deb("scanning 0x08..0x77 ...");
  const hal_status_t status = hal_i2c_scan(
      addresses, HAL_I2C_SCAN_ADDRESS_COUNT, &found, hal_watchdog_feed);
  if (status != HAL_OK) {
    deb("  scan failed: %s", hal_status_to_string(status));
  } else {
    for (size_t i = 0u; i < found; ++i) {
      deb("  device @ 0x%02X", (unsigned)addresses[i]);
    }
  }
  if (status == HAL_OK && found == 0u) {
    deb("  (no devices found)");
  }
  hal_delay_ms(2000);
}
