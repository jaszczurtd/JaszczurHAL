/**
 * @file app.c
 * @brief STM32G474 (Nucleo-G474RE) I2C bus scanner - hardware verification of
 *        the real hal_i2c backend.
 *
 * Probes every 7-bit address 0x08..0x77 and prints those that ACK. This is the
 * simplest way to prove the bare-metal I2C1 master works on real silicon.
 *
 * Wiring (I2C1): SCL = PB8, SDA = PB9, plus pull-ups to 3V3. See README.md.
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
