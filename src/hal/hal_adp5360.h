#pragma once

#include "hal_config.h"
#include "hal_status.h"
#include "hal_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_ADP5360

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_ADP5360_I2C_ADDR_DEFAULT 0x46u
#define HAL_ADP5360_DEVICE_ID 0x10u

typedef enum {
  HAL_ADP5360_WATCHDOG_8S = 0,
  HAL_ADP5360_WATCHDOG_16S = 1,
  HAL_ADP5360_WATCHDOG_32S = 2,
  HAL_ADP5360_WATCHDOG_64S = 3,
} hal_adp5360_watchdog_time_t;

typedef enum {
  HAL_ADP5360_FG_MODE_SLEEP = 0,
  HAL_ADP5360_FG_MODE_ACTIVE = 1,
} hal_adp5360_fg_mode_t;

typedef enum {
  HAL_ADP5360_CHARGER_ONLINE_OFFLINE = 0,
  HAL_ADP5360_CHARGER_ONLINE_FIXED = 1,
} hal_adp5360_charger_online_t;

typedef enum {
  HAL_ADP5360_CHARGER_STATUS_UNKNOWN = 0,
  HAL_ADP5360_CHARGER_STATUS_NOT_CHARGING,
  HAL_ADP5360_CHARGER_STATUS_CHARGING,
  HAL_ADP5360_CHARGER_STATUS_FULL,
  HAL_ADP5360_CHARGER_STATUS_DISCHARGING,
} hal_adp5360_charger_status_t;

typedef enum {
  HAL_ADP5360_CHARGE_TYPE_UNKNOWN = 0,
  HAL_ADP5360_CHARGE_TYPE_NONE,
  HAL_ADP5360_CHARGE_TYPE_TRICKLE,
  HAL_ADP5360_CHARGE_TYPE_FAST,
  HAL_ADP5360_CHARGE_TYPE_LONGLIFE,
} hal_adp5360_charge_type_t;

typedef enum {
  HAL_ADP5360_CHARGER_HEALTH_UNKNOWN = 0,
  HAL_ADP5360_CHARGER_HEALTH_GOOD,
  HAL_ADP5360_CHARGER_HEALTH_COLD,
  HAL_ADP5360_CHARGER_HEALTH_COOL,
  HAL_ADP5360_CHARGER_HEALTH_WARM,
  HAL_ADP5360_CHARGER_HEALTH_HOT,
  HAL_ADP5360_CHARGER_HEALTH_WATCHDOG_TIMER_EXPIRE,
  HAL_ADP5360_CHARGER_HEALTH_OVERVOLTAGE,
  HAL_ADP5360_CHARGER_HEALTH_OVERHEAT,
  HAL_ADP5360_CHARGER_HEALTH_SAFETY_TIMER_EXPIRE,
  HAL_ADP5360_CHARGER_HEALTH_NO_BATTERY,
} hal_adp5360_charger_health_t;

typedef enum {
  HAL_ADP5360_REGULATOR_BUCK = 0,
  HAL_ADP5360_REGULATOR_BUCKBOOST = 1,
} hal_adp5360_regulator_t;

typedef enum {
  HAL_ADP5360_BUCK_MODE_AUTO = 0,
  HAL_ADP5360_BUCK_MODE_PWM = 1,
} hal_adp5360_buck_mode_t;

typedef struct {
  uint16_t v_soc_0_mv;
  uint16_t v_soc_5_mv;
  uint16_t v_soc_11_mv;
  uint16_t v_soc_19_mv;
  uint16_t v_soc_28_mv;
  uint16_t v_soc_41_mv;
  uint16_t v_soc_55_mv;
  uint16_t v_soc_69_mv;
  uint16_t v_soc_84_mv;
  uint16_t v_soc_100_mv;
} hal_adp5360_fuel_gauge_curve_t;

typedef struct {
  uint16_t battery_capacity_mah;
  uint8_t battery_capacity_age;
  uint8_t battery_capacity_temp;
  bool battery_capacity_temp_enable;
  bool battery_capacity_age_enable;
  uint8_t soc_low_threshold;
  uint8_t sleep_mode_current_threshold;
  uint8_t sleep_mode_update_rate;
  bool fuel_gauge_operation_sleep;
  bool fuel_gauge_enable;
  hal_adp5360_fuel_gauge_curve_t curve;
} hal_adp5360_fuel_gauge_config_t;

typedef struct {
  bool vsys_5v;
  uint32_t v_adpichg_uv;
  uint32_t i_input_limit_ua;
  uint32_t v_term_uv;
  uint32_t i_fast_charge_ua;
  uint32_t i_trickle_charge_ua;
  uint32_t i_term_ua;
  uint32_t v_weak_uv;
  uint32_t v_recharge_uv;
  uint8_t v_trickle_dead_idx;
  bool disable_recharge;
  uint8_t trickle_charge_timer_idx;
  bool enable_charger_timer;
  bool enable_end_timer;
  bool enable_adaptive_charge;
  bool enable_eoc;
  bool enable_ldo;
  bool off_isofet;
  bool jeita_cool_ilim;
  bool enable_jeita;
  uint8_t thermistor_current_idx;
  bool enable_thermistor;
  uint32_t v_temp_cold_uv;
  uint32_t v_temp_cool_uv;
  uint32_t v_temp_warm_uv;
  uint32_t v_temp_hot_uv;
  bool enable_battery_protection;
  bool enable_charge_low_battery;
  bool enable_isofet_overcurrent_off;
  bool discharge_overcurrent_hiccup;
  bool charge_overcurrent_hiccup;
  uint32_t battery_undervoltage_uv;
  uint8_t battery_undervoltage_hysteresis_idx;
  uint8_t battery_undervoltage_deglitch_idx;
  uint32_t battery_discharge_overcurrent_ua;
  uint8_t battery_discharge_overcurrent_deglitch_idx;
  uint32_t battery_charge_overvoltage_uv;
  uint8_t battery_charge_overvoltage_hysteresis_idx;
  uint8_t battery_charge_overvoltage_deglitch_idx;
  uint32_t battery_charge_overcurrent_ua;
  uint8_t battery_charge_overcurrent_deglitch_idx;
} hal_adp5360_charger_config_t;

typedef struct {
  int8_t delay_idx;
  int8_t soft_start_idx;
  int8_t current_limit_idx;
  bool enable_stop_pulse;
} hal_adp5360_regulator_config_t;

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t manual_reset_pin;
  bool has_manual_reset_pin;
  uint8_t buckboost_enable_pin;
  bool has_buckboost_enable_pin;
  hal_adp5360_watchdog_time_t watchdog_time;
  bool enable_vout1_reset;
  bool enable_vout2_reset;
  bool reset_time_1p6s;
  bool enable_watchdog;
  bool enable_manual_reset_shipment;
  bool init_charger;
  bool init_fuel_gauge;
  bool init_buck;
  bool init_buckboost;
  hal_adp5360_charger_config_t charger;
  hal_adp5360_fuel_gauge_config_t fuel_gauge;
  hal_adp5360_regulator_config_t buck;
  hal_adp5360_regulator_config_t buckboost;
} hal_adp5360_config_t;

typedef struct {
  hal_adp5360_config_t cfg;
  hal_mutex_t mutex;
  bool initialized;
  uint32_t charger_v_adpichg_uv;
  uint32_t charger_i_input_limit_ua;
  uint32_t charger_i_fast_ua;
  uint32_t charger_i_trickle_ua;
  uint32_t charger_i_term_ua;
  bool charger_enabled;
} hal_adp5360_t;

hal_adp5360_config_t hal_adp5360_default_config(void);
hal_adp5360_charger_config_t hal_adp5360_default_charger_config(void);
hal_adp5360_fuel_gauge_config_t hal_adp5360_default_fuel_gauge_config(void);
hal_adp5360_regulator_config_t
hal_adp5360_default_regulator_config(hal_adp5360_regulator_t regulator);

hal_status_t hal_adp5360_init_ex(hal_adp5360_t *dev,
                                 const hal_adp5360_config_t *cfg);
bool hal_adp5360_init(hal_adp5360_t *dev, const hal_adp5360_config_t *cfg);
void hal_adp5360_deinit(hal_adp5360_t *dev);

hal_status_t hal_adp5360_reg_read(hal_adp5360_t *dev, uint8_t reg,
                                  uint8_t *out_value);
hal_status_t hal_adp5360_reg_write(hal_adp5360_t *dev, uint8_t reg,
                                   uint8_t value);
hal_status_t hal_adp5360_reg_burst_read(hal_adp5360_t *dev, uint8_t reg,
                                        uint8_t *out, size_t len);
hal_status_t hal_adp5360_reg_burst_write(hal_adp5360_t *dev, uint8_t reg,
                                         const uint8_t *data, size_t len);
hal_status_t hal_adp5360_reg_update(hal_adp5360_t *dev, uint8_t reg,
                                    uint8_t mask, uint8_t value);

hal_status_t hal_adp5360_shipment_mode_enable(hal_adp5360_t *dev);
hal_status_t hal_adp5360_shipment_mode_disable(hal_adp5360_t *dev);
hal_status_t hal_adp5360_software_reset(hal_adp5360_t *dev);
hal_status_t hal_adp5360_hardware_reset(hal_adp5360_t *dev);
hal_status_t hal_adp5360_set_fuel_gauge_mode(hal_adp5360_t *dev,
                                             hal_adp5360_fg_mode_t mode);

hal_status_t hal_adp5360_charger_enable(hal_adp5360_t *dev, bool enable);
hal_status_t
hal_adp5360_charger_get_online(hal_adp5360_t *dev,
                               hal_adp5360_charger_online_t *out_online);
hal_status_t hal_adp5360_charger_get_battery_present(hal_adp5360_t *dev,
                                                     bool *out_present);
hal_status_t
hal_adp5360_charger_get_status(hal_adp5360_t *dev,
                               hal_adp5360_charger_status_t *out_status);
hal_status_t
hal_adp5360_charger_get_charge_type(hal_adp5360_t *dev,
                                    hal_adp5360_charge_type_t *out_type);
hal_status_t
hal_adp5360_charger_get_health(hal_adp5360_t *dev,
                               hal_adp5360_charger_health_t *out_health);
hal_status_t hal_adp5360_charger_set_fast_current(hal_adp5360_t *dev,
                                                  uint32_t ua);
hal_status_t hal_adp5360_charger_set_trickle_current(hal_adp5360_t *dev,
                                                     uint32_t ua);
hal_status_t hal_adp5360_charger_set_termination_current(hal_adp5360_t *dev,
                                                         uint32_t ua);
hal_status_t hal_adp5360_charger_set_input_current_limit(hal_adp5360_t *dev,
                                                         uint32_t ua);
hal_status_t hal_adp5360_charger_set_input_voltage_limit(hal_adp5360_t *dev,
                                                         uint32_t uv);
hal_status_t
hal_adp5360_charger_clear_fault(hal_adp5360_t *dev,
                                hal_adp5360_charger_health_t health);

hal_status_t hal_adp5360_fuel_gauge_enable(hal_adp5360_t *dev, bool enable);
hal_status_t hal_adp5360_fuel_gauge_get_cycle_count(hal_adp5360_t *dev,
                                                    uint16_t *out_count);
hal_status_t hal_adp5360_fuel_gauge_get_voltage_uv(hal_adp5360_t *dev,
                                                   uint32_t *out_uv);
hal_status_t hal_adp5360_fuel_gauge_get_status(hal_adp5360_t *dev,
                                               uint8_t *out_status);
hal_status_t hal_adp5360_fuel_gauge_get_design_capacity_mah(hal_adp5360_t *dev,
                                                            uint16_t *out_mah);
hal_status_t hal_adp5360_fuel_gauge_get_soc_pct(hal_adp5360_t *dev,
                                                uint8_t *out_pct);
hal_status_t hal_adp5360_fuel_gauge_get_soc_alarm_pct(hal_adp5360_t *dev,
                                                      uint8_t *out_pct);
hal_status_t
hal_adp5360_fuel_gauge_get_thermistor_voltage_uv(hal_adp5360_t *dev,
                                                 uint32_t *out_uv);
hal_status_t hal_adp5360_fuel_gauge_set_soc_alarm_pct(hal_adp5360_t *dev,
                                                      uint8_t pct);
hal_status_t hal_adp5360_fuel_gauge_set_design_capacity_mah(hal_adp5360_t *dev,
                                                            uint16_t mah);

uint16_t hal_adp5360_regulator_count_voltages(hal_adp5360_regulator_t reg);
hal_status_t hal_adp5360_regulator_list_voltage(hal_adp5360_regulator_t reg,
                                                uint16_t index,
                                                int32_t *out_uv);
uint16_t
hal_adp5360_regulator_count_current_limits(hal_adp5360_regulator_t reg);
hal_status_t
hal_adp5360_regulator_list_current_limit(hal_adp5360_regulator_t reg,
                                         uint16_t index, int32_t *out_ua);
hal_status_t hal_adp5360_regulator_set_voltage(hal_adp5360_t *dev,
                                               hal_adp5360_regulator_t reg,
                                               int32_t min_uv, int32_t max_uv);
hal_status_t hal_adp5360_regulator_get_voltage(hal_adp5360_t *dev,
                                               hal_adp5360_regulator_t reg,
                                               int32_t *out_uv);
hal_status_t
hal_adp5360_regulator_set_current_limit(hal_adp5360_t *dev,
                                        hal_adp5360_regulator_t reg,
                                        int32_t min_ua, int32_t max_ua);
hal_status_t hal_adp5360_regulator_get_current_limit(
    hal_adp5360_t *dev, hal_adp5360_regulator_t reg, int32_t *out_ua);
hal_status_t hal_adp5360_regulator_enable(hal_adp5360_t *dev,
                                          hal_adp5360_regulator_t reg);
hal_status_t hal_adp5360_regulator_disable(hal_adp5360_t *dev,
                                           hal_adp5360_regulator_t reg);
hal_status_t hal_adp5360_regulator_set_buck_mode(hal_adp5360_t *dev,
                                                 hal_adp5360_buck_mode_t mode);
hal_status_t
hal_adp5360_regulator_get_buck_mode(hal_adp5360_t *dev,
                                    hal_adp5360_buck_mode_t *out_mode);
hal_status_t hal_adp5360_regulator_set_active_discharge(
    hal_adp5360_t *dev, hal_adp5360_regulator_t reg, bool active);
hal_status_t hal_adp5360_regulator_get_active_discharge(
    hal_adp5360_t *dev, hal_adp5360_regulator_t reg, bool *out_active);

#endif /* HAL_ENABLE_ADP5360 */

#ifdef __cplusplus
}
#endif
