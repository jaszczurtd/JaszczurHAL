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

#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_system.h>
#include <tools_c.h>

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL G474 I2C scanner ===");
  deb("I2C1: SCL=PB8, SDA=PB9 (external pull-ups to 3V3 required)");

  hal_i2c_init(25u, 24u, 100000u);
}

void app_task0(void) {
  deb("scanning 0x08..0x77 ...");
  int found = 0;
  for (uint8_t addr = 0x08u; addr <= 0x77u; ++addr) {
    /* hal_i2c_is_busy() returns false when the device ACKs (present). */
    if (!hal_i2c_is_busy(addr)) {
      deb("  device @ 0x%02X", (unsigned)addr);
      ++found;
    }
  }
  if (found == 0) {
    deb("  (no devices found)");
  }
  hal_delay_ms(2000);
}
