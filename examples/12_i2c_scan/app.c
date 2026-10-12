/**
 * @file app.c
 * @brief Find I2C devices that acknowledge addresses 0x08 through 0x77.
 *
 * The bus, its pins and its rate come from device_tree.json. The lines need
 * external pull-ups to 3.3 V. See README.md for wiring and limits.
 */

#include <hal/core/hal_app.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL I2C scanner ===");
  deb("assembly %s: I2C bus %u at %lu Hz (external pull-ups to 3V3 required)",
      JH_HW_ASSEMBLY_NAME, (unsigned)JH_HW_BUS_I2C_INDEX,
      (unsigned long)JH_HW_BUS_I2C_FREQUENCY_HZ);

  const hal_status_t status =
      hal_i2c_init_bus(JH_HW_BUS_I2C_INDEX, JH_HW_BUS_I2C_PIN_SDA,
                       JH_HW_BUS_I2C_PIN_SCL, JH_HW_BUS_I2C_FREQUENCY_HZ);
  if (status != HAL_OK) {
    deb("I2C init failed: %s", hal_status_to_string(status));
  }
}

void app_task0(void) {
  uint8_t addresses[HAL_I2C_SCAN_ADDRESS_COUNT];
  size_t found = 0u;
  deb("scanning 0x08..0x77 ...");
  const hal_status_t status =
      hal_i2c_scan_bus(JH_HW_BUS_I2C_INDEX, addresses,
                       HAL_I2C_SCAN_ADDRESS_COUNT, &found, hal_watchdog_feed);
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
