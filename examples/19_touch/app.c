/**
 * @file app.c
 * @brief Read TSC2007 and STMPE610 touch measurements and print the results.
 */

#include <hal/core/hal_app.h>
#include <hal/i2c/hal_i2c.h>
#include <hal/input/hal_stmpe610.h>
#include <hal/input/hal_tsc2007.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <stdint.h>

static hal_tsc2007_t s_tsc2007;
static hal_stmpe610_t s_stmpe610;
static bool s_tsc2007_ready = false;
static bool s_stmpe610_ready = false;
static uint32_t s_last_read_ms = 0u;

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL touch controllers ===");

  /* Bus, pins, clock and addresses come from device_tree.json. */
  hal_i2c_init_bus(JH_HW_BUS_I2C_INDEX, JH_HW_BUS_I2C_PIN_SDA,
                   JH_HW_BUS_I2C_PIN_SCL, JH_HW_BUS_I2C_FREQUENCY_HZ);

  hal_tsc2007_config_t tsc_cfg = hal_tsc2007_default_config();
  tsc_cfg.i2c_bus = JH_HW_NODE_TSC2007_BUS_INDEX;
  tsc_cfg.i2c_addr = JH_HW_NODE_TSC2007_ADDRESS;
  s_tsc2007_ready = hal_tsc2007_init(&s_tsc2007, &tsc_cfg);
  if (s_tsc2007_ready) {
    deb("TSC2007 ready at 0x%02X", (unsigned)JH_HW_NODE_TSC2007_ADDRESS);
  } else {
    derr("TSC2007 init failed; continuing with STMPE610");
  }

  const hal_stmpe610_config_t stmpe_cfg = hal_stmpe610_i2c_config(
      JH_HW_NODE_STMPE610_BUS_INDEX, JH_HW_NODE_STMPE610_ADDRESS);
  s_stmpe610_ready = hal_stmpe610_init(&s_stmpe610, &stmpe_cfg);
  if (s_stmpe610_ready) {
    deb("STMPE610 ready at 0x%02X", (unsigned)JH_HW_NODE_STMPE610_ADDRESS);
  } else {
    derr("STMPE610 init failed; TSC2007 state is unchanged");
  }
}

void app_task0(void) {
  const uint32_t now = hal_millis();
  if ((now - s_last_read_ms) < 100u) {
    hal_delay_ms(10u);
    return;
  }
  s_last_read_ms = now;

  if (s_tsc2007_ready) {
    uint16_t x = 0u;
    uint16_t y = 0u;
    uint16_t z1 = 0u;
    uint16_t z2 = 0u;
    if (hal_tsc2007_read_touch(&s_tsc2007, &x, &y, &z1, &z2)) {
      deb("TSC2007 x=%u y=%u z1=%u z2=%u", (unsigned)x, (unsigned)y,
          (unsigned)z1, (unsigned)z2);
    }
  }

  if (s_stmpe610_ready && hal_stmpe610_touched(&s_stmpe610)) {
    const hal_stmpe610_point_t point = hal_stmpe610_get_point(&s_stmpe610);
    deb("STMPE610 x=%d y=%d z=%d", point.x, point.y, point.z);
  }

  if (!s_tsc2007_ready && !s_stmpe610_ready) {
    hal_delay_ms(900u);
  } else {
    hal_delay_ms(10u);
  }
}
