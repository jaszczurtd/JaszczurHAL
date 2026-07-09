/**
 * @file app.cpp
 * @brief ADP5360 PMIC example.
 */

#include <hal/hal_adp5360.h>
#include <hal/hal_app.h>
#include <hal/hal_i2c.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_I2C_SDA 4u
#define EXAMPLE_I2C_SCL 5u
#else
/* STM32 pin id = port * 16 + pin: PB7/PB6 I2C1. */
#define EXAMPLE_I2C_SDA 23u
#define EXAMPLE_I2C_SCL 22u
#endif

static hal_adp5360_t s_pmic;
static bool s_pmic_ready = false;

static void log_status(const char *op, hal_status_t status) {
  if (status == HAL_OK) {
    deb("%s: OK", op);
  } else {
    derr("%s failed: %s (%d)", op, hal_status_to_string(status), (int)status);
  }
}

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL ADP5360 PMIC example ===");

  hal_i2c_init(EXAMPLE_I2C_SDA, EXAMPLE_I2C_SCL, HAL_I2C_CLOCK_STANDARD_HZ);

  hal_adp5360_config_t cfg = hal_adp5360_default_config();
  cfg.i2c_addr = HAL_ADP5360_I2C_ADDR_DEFAULT;
  cfg.charger.i_fast_charge_ua = 10000u;
  cfg.charger.i_input_limit_ua = 50000u;
  cfg.fuel_gauge.battery_capacity_mah = 100u;

  hal_status_t status = hal_adp5360_init_ex(&s_pmic, &cfg);
  s_pmic_ready = status == HAL_OK;
  log_status("ADP5360 init", status);

  if (s_pmic_ready) {
    status = hal_adp5360_regulator_set_voltage(
        &s_pmic, HAL_ADP5360_REGULATOR_BUCK, 1200000, 1200000);
    log_status("BUCK set 1.2V", status);
  }
}

void app_task0(void) {
  if (!s_pmic_ready) {
    hal_delay_ms(1000u);
    return;
  }

  uint8_t soc = 0u;
  uint32_t vbat_uv = 0u;
  bool battery_present = false;
  hal_adp5360_charger_status_t charger_status =
      HAL_ADP5360_CHARGER_STATUS_UNKNOWN;

  hal_status_t status = hal_adp5360_fuel_gauge_get_soc_pct(&s_pmic, &soc);
  if (status != HAL_OK) {
    derr("SOC read failed: %s", hal_status_to_string(status));
  }

  status = hal_adp5360_fuel_gauge_get_voltage_uv(&s_pmic, &vbat_uv);
  if (status != HAL_OK) {
    derr("VBAT read failed: %s", hal_status_to_string(status));
  }

  status = hal_adp5360_charger_get_battery_present(&s_pmic, &battery_present);
  if (status != HAL_OK) {
    derr("battery-present read failed: %s", hal_status_to_string(status));
  }

  status = hal_adp5360_charger_get_status(&s_pmic, &charger_status);
  if (status != HAL_OK) {
    derr("charger-status read failed: %s", hal_status_to_string(status));
  }

  deb("ADP5360 soc=%u%% vbat=%luuV present=%d charger=%d", (unsigned)soc,
      (unsigned long)vbat_uv, battery_present ? 1 : 0, (int)charger_status);
  hal_delay_ms(2000u);
}
