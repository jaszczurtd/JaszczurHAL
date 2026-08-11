/*
 * ADP5360 register flow, charger/fuel-gauge/regulator conversions and init
 * sequencing are based on the Zephyr ADP5360 drivers authored by Analog
 * Devices, Inc. and Nordic Semiconductor ASA contributors (Apache-2.0). This
 * implementation removes Zephyr device-tree, subsystem and kernel dependencies
 * and uses only JaszczurHAL I2C, GPIO, timing and synchronization primitives.
 */

#include "hal/core/hal_target.h"
#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_ADP5360) && defined(HAL_ENABLE_I2C)

#include "hal/power/hal_adp5360.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/system/hal_system.h"

#include <stddef.h>
#include <stdint.h>

#define ADP5360_REG_DEVICE_ID 0x00u
#define ADP5360_REG_CHARGER_VBUS_ILIM 0x02u
#define ADP5360_REG_CHARGER_TERM_CFG 0x03u
#define ADP5360_REG_CHARGER_CURR_CFG 0x04u
#define ADP5360_REG_CHARGER_VOLT_CFG 0x05u
#define ADP5360_REG_CHARGER_TMR_CFG 0x06u
#define ADP5360_REG_CHARGER_FUNC_CFG 0x07u
#define ADP5360_REG_CHARGER_STATUS_1 0x08u
#define ADP5360_REG_CHARGER_STATUS_2 0x09u
#define ADP5360_REG_CHARGER_THERM_CTRL 0x0Au
#define ADP5360_REG_BATTERY_THERM_60C 0x0Bu
#define ADP5360_REG_BATTERY_THERM_45C 0x0Cu
#define ADP5360_REG_BATTERY_THERM_10C 0x0Du
#define ADP5360_REG_BATTERY_THERM_0C 0x0Eu
#define ADP5360_REG_V_THERM 0x0Fu
#define ADP5360_REG_BATPROT_CFG 0x11u
#define ADP5360_REG_BATPROT_UV_CFG 0x12u
#define ADP5360_REG_BATPROT_DISCHG_OC_CFG 0x13u
#define ADP5360_REG_BATPROT_OV_CFG 0x14u
#define ADP5360_REG_BATPROT_CHG_OC_CFG 0x15u
#define ADP5360_REG_V_SOC_0 0x16u
#define ADP5360_REG_BAT_CAP 0x20u
#define ADP5360_REG_BAT_SOC 0x21u
#define ADP5360_REG_BAT_SOC_ACM_CTL 0x22u
#define ADP5360_REG_BAT_SOC_ACM_H 0x23u
#define ADP5360_REG_BAT_SOC_ACM_L 0x24u
#define ADP5360_REG_VBAT_READ_H 0x25u
#define ADP5360_REG_VBAT_READ_L 0x26u
#define ADP5360_REG_FUEL_GAUGE_MODE 0x27u
#define ADP5360_REG_SOC_RESET 0x28u
#define ADP5360_REG_BUCK_CFG 0x29u
#define ADP5360_REG_BUCK_OUTPUT 0x2Au
#define ADP5360_REG_BUCKBST_CFG 0x2Bu
#define ADP5360_REG_BUCKBST_OUTPUT 0x2Cu
#define ADP5360_REG_SUPERVISORY_CFG 0x2Du
#define ADP5360_REG_FAULT_STATUS 0x2Eu
#define ADP5360_REG_PGOOD_STATUS 0x2Fu
#define ADP5360_REG_INT_STATUS1 0x34u
#define ADP5360_REG_INT_STATUS2 0x35u
#define ADP5360_REG_SHIPMENT 0x36u

#define BIT_U8(n) ((uint8_t)(1u << (n)))
#define GENMASK_U8(h, l) ((uint8_t)((0xFFu >> (7u - (h))) & (0xFFu << (l))))

#define ADP5360_MFD_VOUT1_RST_MASK BIT_U8(7)
#define ADP5360_MFD_VOUT2_RST_MASK BIT_U8(6)
#define ADP5360_MFD_RESET_TIME_MASK BIT_U8(5)
#define ADP5360_MFD_WATCHDOG_TIME_MASK GENMASK_U8(4, 3)
#define ADP5360_MFD_ENABLE_WATCHDOG_MASK BIT_U8(2)
#define ADP5360_MFD_ENABLE_SHIPMENT_ON_MR_MASK BIT_U8(1)
#define ADP5360_MFD_SOC_RESET_MASK BIT_U8(7)
#define ADP5360_MFD_FG_MODE_MASK BIT_U8(1)
#define ADP5360_STATUS_MANUAL_RESET_INT_MASK BIT_U8(7)

#define ADP5360_ILIM_MASK GENMASK_U8(2, 0)
#define ADP5360_VSYS_5V_MASK BIT_U8(3)
#define ADP5360_VADPICHG_MASK GENMASK_U8(7, 5)
#define ADP5360_ITRK_MASK GENMASK_U8(1, 0)
#define ADP5360_VTERM_MASK GENMASK_U8(7, 2)
#define ADP5360_ICHG_MASK GENMASK_U8(4, 0)
#define ADP5360_IEND_MASK GENMASK_U8(7, 5)
#define ADP5360_VWEAK_MASK GENMASK_U8(2, 0)
#define ADP5360_VTRK_DEAD_MASK GENMASK_U8(4, 3)
#define ADP5360_VRCH_MASK GENMASK_U8(6, 5)
#define ADP5360_DIS_RECHARGE_MASK BIT_U8(7)
#define ADP5360_TIMER_PERIOD_MASK GENMASK_U8(1, 0)
#define ADP5360_EN_CHG_TIMER BIT_U8(2)
#define ADP5360_EN_T_END BIT_U8(3)
#define ADP5360_FUNC_EN_CHG BIT_U8(0)
#define ADP5360_FUNC_EN_ADPICHG BIT_U8(1)
#define ADP5360_FUNC_EN_EOC BIT_U8(2)
#define ADP5360_FUNC_EN_LDO BIT_U8(3)
#define ADP5360_FUNC_OFF_ISOFET BIT_U8(4)
#define ADP5360_FUNC_ILIM_JEITA_COOL BIT_U8(6)
#define ADP5360_FUNC_EN_JEITA BIT_U8(7)
#define ADP5360_STATUS_1_CHARGER_MODE_MASK GENMASK_U8(2, 0)
#define ADP5360_STATUS_2_BAT_CHG_MASK GENMASK_U8(2, 0)
#define ADP5360_STATUS_2_THERM_MASK GENMASK_U8(7, 5)
#define ADP5360_THERM_CTRL_ITHR GENMASK_U8(7, 6)
#define ADP5360_THERM_CTRL_EN BIT_U8(0)
#define ADP5360_THERM_THRESHOLD_MASK GENMASK_U8(7, 0)
#define ADP5360_BATPROT_CFG_EN_BATPROTECT BIT_U8(0)
#define ADP5360_BATPROT_CFG_EN_CHGLB BIT_U8(1)
#define ADP5360_BATPROT_CFG_OC_CHG_HICCUP BIT_U8(2)
#define ADP5360_BATPROT_CFG_OC_DISCHG_HICCUP BIT_U8(3)
#define ADP5360_BATPROT_CFG_ISOFET_OVCHG BIT_U8(4)
#define ADP5360_BATPROT_UV_DEGLITCH_MASK GENMASK_U8(1, 0)
#define ADP5360_BATPROT_UV_HYSTERESIS_MASK GENMASK_U8(3, 2)
#define ADP5360_BATPROT_UV_DISCHARGE_MASK GENMASK_U8(7, 4)
#define ADP5360_BATPROT_ODCHG_DEGLITCH_MASK GENMASK_U8(3, 1)
#define ADP5360_BATPROT_ODCHG_OC_DISCH_MASK GENMASK_U8(7, 5)
#define ADP5360_BATPROT_OV_DEGLITCH BIT_U8(0)
#define ADP5360_BATPROT_OV_HYSTERESIS_MASK GENMASK_U8(2, 1)
#define ADP5360_BATPROT_OV_CHARGE_MASK GENMASK_U8(7, 3)
#define ADP5360_BATPROT_OCHG_DEGLITCH_MASK GENMASK_U8(4, 3)
#define ADP5360_BATPROT_OCHG_OC_CHG_MASK GENMASK_U8(7, 5)
#define ADP5360_FAULT_TEMP_SHUTDOWN BIT_U8(0)
#define ADP5360_FAULT_WATCHDOG_TIMEOUT BIT_U8(2)
#define ADP5360_FAULT_BAT_CHG_OVERVOLT BIT_U8(4)

#define ADP5360_FG_SOC_SHIFT 4
#define ADP5360_FG_VBAT_SHIFT 3
#define ADP5360_FG_MV_TO_UV 1000u
#define ADP5360_FG_SOC_FULL 100u
#define ADP5360_FG_BAT_CAP_MAX 510u
#define ADP5360_FG_BAT_CAP_AGE_MASK GENMASK_U8(7, 6)
#define ADP5360_FG_BAT_CAP_TEMP_MASK GENMASK_U8(5, 4)
#define ADP5360_FG_BAT_CAP_TEMP_EN_MASK BIT_U8(1)
#define ADP5360_FG_BAT_CAP_AGE_EN_MASK BIT_U8(0)
#define ADP5360_FG_SOC_LOW_THRESHOLD_MASK GENMASK_U8(7, 6)
#define ADP5360_FG_SLEEP_CURRENT_MASK GENMASK_U8(5, 4)
#define ADP5360_FG_SLEEP_UPDATE_MASK GENMASK_U8(3, 2)
#define ADP5360_FG_OPERATION_MODE_MASK BIT_U8(1)
#define ADP5360_FG_ENABLE_MASK BIT_U8(0)

#define ADP5360_SUPERVISORY_BUCK_RST_MASK BIT_U8(7)
#define ADP5360_SUPERVISORY_BUCKBST_RST_MASK BIT_U8(6)
#define ADP5360_BUCK_CFG_SS_MSK GENMASK_U8(7, 6)
#define ADP5360_BUCK_CFG_BST_ILIM_MSK GENMASK_U8(5, 3)
#define ADP5360_BUCK_CFG_BUCK_ILIM_MSK GENMASK_U8(5, 4)
#define ADP5360_BUCK_CFG_BUCK_MODE_MSK BIT_U8(3)
#define ADP5360_BUCK_CFG_STP_MSK BIT_U8(2)
#define ADP5360_BUCK_CFG_DISCHG_MSK BIT_U8(1)
#define ADP5360_BUCK_CFG_EN_MSK BIT_U8(0)
#define ADP5360_BUCK_OUTPUT_VOUT_MSK GENMASK_U8(5, 0)
#define ADP5360_BUCK_OUTPUT_DLY_MSK GENMASK_U8(7, 6)

typedef struct {
  int32_t min;
  int32_t step;
  uint16_t start;
  uint16_t end;
} adp5360_range_t;

typedef struct {
  uint8_t cfg_reg;
  uint8_t out_reg;
  const adp5360_range_t *v_ranges;
  uint8_t v_range_count;
  const adp5360_range_t *i_ranges;
  uint8_t i_range_count;
  bool is_buckboost;
} adp5360_reg_desc_t;

static const adp5360_range_t range_v_bus_i_limit[] = {
    {50000, 50000, 0x0u, 0x5u},
    {400000, 100000, 0x6u, 0x7u},
};
static const adp5360_range_t range_v_bus_adaptive[] = {
    {4400000, 100000, 0x2u, 0x7u},
};
static const adp5360_range_t range_v_term[] = {
    {3560000, 20000, 0x0u, 0x37u},
    {4660000, 0, 0x38u, 0x3Fu},
};
static const adp5360_range_t range_i_term[] = {
    {5000, 2500, 0x1u, 0x02u},
    {12500, 5000, 0x3u, 0x7u},
};
static const adp5360_range_t range_i_trickle[] = {
    {1000, 1500, 0x0u, 0x1u},
    {5000, 5000, 0x2u, 0x3u},
};
static const adp5360_range_t range_i_fast[] = {
    {10000, 10000, 0x0u, 0x1Fu},
};
static const adp5360_range_t range_v_recharge[] = {
    {120000, 60000, 0x1u, 0x3u},
};
static const adp5360_range_t range_v_weak[] = {
    {2700000, 100000, 0x0u, 0x7u},
};
static const adp5360_range_t range_v_temp_hot[] = {
    {0, 2000, 0x0u, 0xFFu},
};
static const adp5360_range_t range_v_temp_cold[] = {
    {0, 10000, 0x0u, 0xFFu},
};
static const adp5360_range_t range_v_bat_discharge[] = {
    {2050000, 50000, 0x0u, 0xFu},
};
static const adp5360_range_t range_i_bat_discharge[] = {
    {50000, 50000, 0x0u, 0x3u},
    {300000, 100000, 0x4u, 0x7u},
};
static const adp5360_range_t range_v_bat_charge_ov[] = {
    {3550000, 50000, 0x0u, 0x19u},
    {4800000, 0, 0x1Au, 0x1Fu},
};
static const adp5360_range_t range_i_bat_charge[] = {
    {25000, 25000, 0x0u, 0x1u},
    {100000, 50000, 0x2u, 0x5u},
    {300000, 100000, 0x6u, 0x7u},
};
static const adp5360_range_t range_v_soc[] = {
    {2500, 8, 0x0u, 0xFFu},
};
static const adp5360_range_t range_buck_v[] = {
    {600000, 50000, 0x0u, 0x3Fu},
};
static const adp5360_range_t range_buck_i[] = {
    {100000, 100000, 0x0u, 0x3u},
};
static const adp5360_range_t range_buckboost_v[] = {
    {1800000, 100000, 0x0u, 0x0Bu},
    {2950000, 50000, 0xCu, 0x3Fu},
};
static const adp5360_range_t range_buckboost_i[] = {
    {100000, 100000, 0x0u, 0x7u},
};

#define ARRAY_COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const adp5360_reg_desc_t desc_buck = {ADP5360_REG_BUCK_CFG,
                                             ADP5360_REG_BUCK_OUTPUT,
                                             range_buck_v,
                                             ARRAY_COUNT(range_buck_v),
                                             range_buck_i,
                                             ARRAY_COUNT(range_buck_i),
                                             false};
static const adp5360_reg_desc_t desc_buckboost = {
    ADP5360_REG_BUCKBST_CFG,
    ADP5360_REG_BUCKBST_OUTPUT,
    range_buckboost_v,
    ARRAY_COUNT(range_buckboost_v),
    range_buckboost_i,
    ARRAY_COUNT(range_buckboost_i),
    true};

static uint8_t bit_shift(uint8_t mask) {
  uint8_t s = 0u;
  while (s < 8u && ((mask & BIT_U8(s)) == 0u)) {
    s++;
  }
  return s;
}

static uint8_t field_prep(uint8_t mask, uint32_t value) {
  return (uint8_t)((value << bit_shift(mask)) & mask);
}

static uint8_t field_get(uint8_t mask, uint8_t value) {
  return (uint8_t)((value & mask) >> bit_shift(mask));
}

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static bool valid_dev(hal_adp5360_t *dev) {
  return (dev != NULL) && dev->initialized;
}

static hal_status_t ensure_mutex(hal_adp5360_t *dev) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  return (jh_hal_mutex_create_once(&dev->mutex) != NULL) ? HAL_OK : HAL_ENOMEM;
}

static hal_status_t range_index(const adp5360_range_t *ranges, uint8_t count,
                                int32_t value, uint16_t *out_idx) {
  if (ranges == NULL || out_idx == NULL) {
    return HAL_EINVAL;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const adp5360_range_t *r = &ranges[i];
    const int32_t max = r->min + (int32_t)(r->end - r->start) * r->step;
    if (value < r->min || value > max) {
      continue;
    }
    if (r->step == 0) {
      *out_idx = r->start;
      return HAL_OK;
    }
    const int32_t off = value - r->min;
    if ((off % r->step) != 0) {
      continue;
    }
    *out_idx = (uint16_t)(r->start + (uint16_t)(off / r->step));
    return HAL_OK;
  }
  return HAL_EINVAL;
}

static hal_status_t range_window_index(const adp5360_range_t *ranges,
                                       uint8_t count, int32_t min_value,
                                       int32_t max_value, uint16_t *out_idx) {
  if (ranges == NULL || out_idx == NULL || min_value > max_value) {
    return HAL_EINVAL;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const adp5360_range_t *r = &ranges[i];
    for (uint16_t code = r->start; code <= r->end; ++code) {
      const int32_t v = r->min + (int32_t)(code - r->start) * r->step;
      if (v >= min_value && v <= max_value) {
        *out_idx = code;
        return HAL_OK;
      }
      if (code == UINT16_MAX) {
        break;
      }
    }
  }
  return HAL_EINVAL;
}

static hal_status_t range_value(const adp5360_range_t *ranges, uint8_t count,
                                uint16_t idx, int32_t *out_value) {
  if (ranges == NULL || out_value == NULL) {
    return HAL_EINVAL;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const adp5360_range_t *r = &ranges[i];
    if (idx >= r->start && idx <= r->end) {
      *out_value = r->min + (int32_t)(idx - r->start) * r->step;
      return HAL_OK;
    }
  }
  return HAL_EINVAL;
}

static uint16_t range_count(const adp5360_range_t *ranges, uint8_t count) {
  uint16_t n = 0u;
  for (uint8_t i = 0; i < count; ++i) {
    n = (uint16_t)(n + ranges[i].end - ranges[i].start + 1u);
  }
  return n;
}

static hal_status_t list_range_index(const adp5360_range_t *ranges,
                                     uint8_t count, uint16_t index,
                                     int32_t *out_value) {
  for (uint8_t i = 0; i < count; ++i) {
    const uint16_t n = (uint16_t)(ranges[i].end - ranges[i].start + 1u);
    if (index < n) {
      return range_value(&ranges[i], 1u, (uint16_t)(ranges[i].start + index),
                         out_value);
    }
    index = (uint16_t)(index - n);
  }
  return HAL_EINVAL;
}

static const adp5360_reg_desc_t *reg_desc(hal_adp5360_regulator_t reg) {
  return (reg == HAL_ADP5360_REGULATOR_BUCKBOOST) ? &desc_buckboost
                                                  : &desc_buck;
}

static hal_status_t reg_read_unlocked(hal_adp5360_t *dev, uint8_t reg,
                                      uint8_t *out_value) {
  return hal_i2c_write_read_bus_ex(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &reg,
                                   1u, out_value, 1u);
}

static hal_status_t reg_write_unlocked(hal_adp5360_t *dev, uint8_t reg,
                                       uint8_t value) {
  uint8_t tx[2] = {reg, value};
  return hal_i2c_write_read_bus_ex(dev->cfg.i2c_bus, dev->cfg.i2c_addr, tx,
                                   sizeof(tx), NULL, 0u);
}

static hal_status_t reg_burst_read_unlocked(hal_adp5360_t *dev, uint8_t reg,
                                            uint8_t *out, size_t len) {
  return hal_i2c_write_read_bus_ex(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &reg,
                                   1u, out, len);
}

static hal_status_t reg_burst_write_unlocked(hal_adp5360_t *dev, uint8_t reg,
                                             const uint8_t *data, size_t len) {
  if (len > 254u) {
    return HAL_EINVAL;
  }
  uint8_t tx[255];
  tx[0] = reg;
  for (size_t i = 0; i < len; ++i) {
    tx[i + 1u] = data[i];
  }
  return hal_i2c_write_read_bus_ex(dev->cfg.i2c_bus, dev->cfg.i2c_addr, tx,
                                   len + 1u, NULL, 0u);
}

static hal_status_t reg_update_unlocked(hal_adp5360_t *dev, uint8_t reg,
                                        uint8_t mask, uint8_t value) {
  uint8_t reg_val = 0u;
  hal_status_t st = reg_read_unlocked(dev, reg, &reg_val);
  if (st != HAL_OK) {
    return st;
  }
  reg_val = (uint8_t)((reg_val & (uint8_t)~mask) | field_prep(mask, value));
  return reg_write_unlocked(dev, reg, reg_val);
}

hal_adp5360_charger_config_t hal_adp5360_default_charger_config(void) {
  hal_adp5360_charger_config_t c = {};
  c.v_adpichg_uv = 4400000u;
  c.i_input_limit_ua = 50000u;
  c.v_term_uv = 4200000u;
  c.i_fast_charge_ua = 10000u;
  c.i_trickle_charge_ua = 1000u;
  c.i_term_ua = 5000u;
  c.v_weak_uv = 2700000u;
  c.v_recharge_uv = 120000u;
  c.enable_ldo = true;
  c.enable_eoc = true;
  c.enable_thermistor = false;
  c.v_temp_cold_uv = 0u;
  c.v_temp_cool_uv = 0u;
  c.v_temp_warm_uv = 0u;
  c.v_temp_hot_uv = 0u;
  c.battery_undervoltage_uv = 2050000u;
  c.battery_discharge_overcurrent_ua = 50000u;
  c.battery_charge_overvoltage_uv = 4200000u;
  c.battery_charge_overcurrent_ua = 25000u;
  return c;
}

hal_adp5360_fuel_gauge_config_t hal_adp5360_default_fuel_gauge_config(void) {
  hal_adp5360_fuel_gauge_config_t c = {};
  c.battery_capacity_mah = 100u;
  c.fuel_gauge_enable = true;
  c.curve = {2500u, 3000u, 3200u, 3400u, 3600u,
             3800u, 4000u, 4150u, 4300u, 4540u};
  return c;
}

hal_adp5360_regulator_config_t
hal_adp5360_default_regulator_config(hal_adp5360_regulator_t regulator) {
  hal_adp5360_regulator_config_t c = {};
  c.delay_idx = 0;
  c.soft_start_idx = 0;
  c.current_limit_idx = 0;
  c.enable_stop_pulse = false;
  (void)regulator;
  return c;
}

hal_adp5360_config_t hal_adp5360_default_config(void) {
  hal_adp5360_config_t c = {};
  c.i2c_bus = 0u;
  c.i2c_addr = HAL_ADP5360_I2C_ADDR_DEFAULT;
  c.watchdog_time = HAL_ADP5360_WATCHDOG_8S;
  c.enable_vout1_reset = true;
  c.enable_vout2_reset = false;
  c.reset_time_1p6s = false;
  c.enable_watchdog = false;
  c.enable_manual_reset_shipment = false;
  c.init_charger = true;
  c.init_fuel_gauge = true;
  c.init_buck = true;
  c.init_buckboost = true;
  c.charger = hal_adp5360_default_charger_config();
  c.fuel_gauge = hal_adp5360_default_fuel_gauge_config();
  c.buck = hal_adp5360_default_regulator_config(HAL_ADP5360_REGULATOR_BUCK);
  c.buckboost =
      hal_adp5360_default_regulator_config(HAL_ADP5360_REGULATOR_BUCKBOOST);
  return c;
}

static hal_status_t init_charger_unlocked(hal_adp5360_t *dev) {
  const hal_adp5360_charger_config_t *c = &dev->cfg.charger;
  uint8_t val = 0u;
  uint16_t idx = 0u;
  hal_status_t st;

  dev->charger_v_adpichg_uv =
      (uint32_t)clamp_i32((int32_t)c->v_adpichg_uv, 4400000, 4900000);
  dev->charger_i_input_limit_ua =
      (uint32_t)clamp_i32((int32_t)c->i_input_limit_ua, 50000, 500000);
  dev->charger_i_fast_ua =
      (uint32_t)clamp_i32((int32_t)c->i_fast_charge_ua, 10000, 320000);
  dev->charger_i_trickle_ua =
      (uint32_t)clamp_i32((int32_t)c->i_trickle_charge_ua, 1000, 10000);
  dev->charger_i_term_ua =
      (uint32_t)clamp_i32((int32_t)c->i_term_ua, 5000, 32500);

  st = range_index(range_v_bus_adaptive, ARRAY_COUNT(range_v_bus_adaptive),
                   (int32_t)dev->charger_v_adpichg_uv, &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_VADPICHG_MASK, idx);
  val |= field_prep(ADP5360_VSYS_5V_MASK, c->vsys_5v);
  st = range_index(range_v_bus_i_limit, ARRAY_COUNT(range_v_bus_i_limit),
                   (int32_t)dev->charger_i_input_limit_ua, &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_ILIM_MASK, idx);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_VBUS_ILIM, val);
  if (st != HAL_OK)
    return st;

  val = 0u;
  st = range_index(range_v_term, ARRAY_COUNT(range_v_term),
                   (int32_t)clamp_i32((int32_t)c->v_term_uv, 3560000, 4660000),
                   &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_VTERM_MASK, idx);
  st = range_index(range_i_trickle, ARRAY_COUNT(range_i_trickle),
                   (int32_t)dev->charger_i_trickle_ua, &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_ITRK_MASK, idx);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_TERM_CFG, val);
  if (st != HAL_OK)
    return st;

  val = 0u;
  st = range_index(range_i_term, ARRAY_COUNT(range_i_term),
                   (int32_t)dev->charger_i_term_ua, &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_IEND_MASK, idx);
  st = range_index(range_i_fast, ARRAY_COUNT(range_i_fast),
                   (int32_t)dev->charger_i_fast_ua, &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_ICHG_MASK, idx);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_CURR_CFG, val);
  if (st != HAL_OK)
    return st;

  val = field_prep(ADP5360_VTRK_DEAD_MASK, c->v_trickle_dead_idx) |
        field_prep(ADP5360_DIS_RECHARGE_MASK, c->disable_recharge);
  st = range_index(range_v_recharge, ARRAY_COUNT(range_v_recharge),
                   clamp_i32((int32_t)c->v_recharge_uv, 120000, 240000), &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_VRCH_MASK, idx);
  st = range_index(range_v_weak, ARRAY_COUNT(range_v_weak),
                   clamp_i32((int32_t)c->v_weak_uv, 2700000, 3400000), &idx);
  if (st != HAL_OK)
    return st;
  val |= field_prep(ADP5360_VWEAK_MASK, idx);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_VOLT_CFG, val);
  if (st != HAL_OK)
    return st;

  val = field_prep(ADP5360_TIMER_PERIOD_MASK, c->trickle_charge_timer_idx) |
        field_prep(ADP5360_EN_CHG_TIMER, c->enable_charger_timer) |
        field_prep(ADP5360_EN_T_END, c->enable_end_timer);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_TMR_CFG, val);
  if (st != HAL_OK)
    return st;

  val = field_prep(ADP5360_FUNC_EN_ADPICHG, c->enable_adaptive_charge) |
        field_prep(ADP5360_FUNC_EN_EOC, c->enable_eoc) |
        field_prep(ADP5360_FUNC_EN_LDO, c->enable_ldo) |
        field_prep(ADP5360_FUNC_OFF_ISOFET, c->off_isofet) |
        field_prep(ADP5360_FUNC_ILIM_JEITA_COOL, c->jeita_cool_ilim) |
        field_prep(ADP5360_FUNC_EN_JEITA, c->enable_jeita);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_FUNC_CFG, val);
  if (st != HAL_OK)
    return st;

  val = field_prep(ADP5360_THERM_CTRL_ITHR, c->thermistor_current_idx) |
        field_prep(ADP5360_THERM_CTRL_EN, c->enable_thermistor);
  st = reg_write_unlocked(dev, ADP5360_REG_CHARGER_THERM_CTRL, val);
  if (st != HAL_OK)
    return st;

  const uint8_t therm_regs[4] = {
      ADP5360_REG_BATTERY_THERM_0C, ADP5360_REG_BATTERY_THERM_10C,
      ADP5360_REG_BATTERY_THERM_45C, ADP5360_REG_BATTERY_THERM_60C};
  const uint32_t therm_vals[4] = {c->v_temp_cold_uv, c->v_temp_cool_uv,
                                  c->v_temp_warm_uv, c->v_temp_hot_uv};
  const adp5360_range_t *therm_ranges[4] = {
      range_v_temp_cold, range_v_temp_cold, range_v_temp_hot, range_v_temp_hot};
  const int32_t therm_max[4] = {2550000, 2550000, 510000, 510000};
  for (uint8_t i = 0; i < 4u; ++i) {
    st = range_index(therm_ranges[i], 1u,
                     clamp_i32((int32_t)therm_vals[i], 0, therm_max[i]), &idx);
    if (st != HAL_OK)
      return st;
    st = reg_write_unlocked(dev, therm_regs[i],
                            field_prep(ADP5360_THERM_THRESHOLD_MASK, idx));
    if (st != HAL_OK)
      return st;
  }

  val = field_prep(ADP5360_BATPROT_CFG_EN_BATPROTECT,
                   c->enable_battery_protection) |
        field_prep(ADP5360_BATPROT_CFG_EN_CHGLB, c->enable_charge_low_battery) |
        field_prep(ADP5360_BATPROT_CFG_ISOFET_OVCHG,
                   c->enable_isofet_overcurrent_off) |
        field_prep(ADP5360_BATPROT_CFG_OC_DISCHG_HICCUP,
                   c->discharge_overcurrent_hiccup) |
        field_prep(ADP5360_BATPROT_CFG_OC_CHG_HICCUP,
                   c->charge_overcurrent_hiccup);
  st = reg_write_unlocked(dev, ADP5360_REG_BATPROT_CFG, val);
  if (st != HAL_OK)
    return st;

  st = range_index(
      range_v_bat_discharge, ARRAY_COUNT(range_v_bat_discharge),
      clamp_i32((int32_t)c->battery_undervoltage_uv, 2050000, 2800000), &idx);
  if (st != HAL_OK)
    return st;
  val = field_prep(ADP5360_BATPROT_UV_DISCHARGE_MASK, idx) |
        field_prep(ADP5360_BATPROT_UV_HYSTERESIS_MASK,
                   c->battery_undervoltage_hysteresis_idx) |
        field_prep(ADP5360_BATPROT_UV_DEGLITCH_MASK,
                   c->battery_undervoltage_deglitch_idx);
  st = reg_write_unlocked(dev, ADP5360_REG_BATPROT_UV_CFG, val);
  if (st != HAL_OK)
    return st;

  st = range_index(
      range_i_bat_discharge, ARRAY_COUNT(range_i_bat_discharge),
      clamp_i32((int32_t)c->battery_discharge_overcurrent_ua, 50000, 600000),
      &idx);
  if (st != HAL_OK)
    return st;
  val = field_prep(ADP5360_BATPROT_ODCHG_OC_DISCH_MASK, idx) |
        field_prep(ADP5360_BATPROT_ODCHG_DEGLITCH_MASK,
                   c->battery_discharge_overcurrent_deglitch_idx);
  st = reg_write_unlocked(dev, ADP5360_REG_BATPROT_DISCHG_OC_CFG, val);
  if (st != HAL_OK)
    return st;

  st = range_index(
      range_v_bat_charge_ov, ARRAY_COUNT(range_v_bat_charge_ov),
      clamp_i32((int32_t)c->battery_charge_overvoltage_uv, 3550000, 4800000),
      &idx);
  if (st != HAL_OK)
    return st;
  val = field_prep(ADP5360_BATPROT_OV_CHARGE_MASK, idx) |
        field_prep(ADP5360_BATPROT_OV_HYSTERESIS_MASK,
                   c->battery_charge_overvoltage_hysteresis_idx) |
        field_prep(ADP5360_BATPROT_OV_DEGLITCH,
                   c->battery_charge_overvoltage_deglitch_idx);
  st = reg_write_unlocked(dev, ADP5360_REG_BATPROT_OV_CFG, val);
  if (st != HAL_OK)
    return st;

  st = range_index(
      range_i_bat_charge, ARRAY_COUNT(range_i_bat_charge),
      clamp_i32((int32_t)c->battery_charge_overcurrent_ua, 25000, 400000),
      &idx);
  if (st != HAL_OK)
    return st;
  val = field_prep(ADP5360_BATPROT_OCHG_OC_CHG_MASK, idx) |
        field_prep(ADP5360_BATPROT_OCHG_DEGLITCH_MASK,
                   c->battery_charge_overcurrent_deglitch_idx);
  return reg_write_unlocked(dev, ADP5360_REG_BATPROT_CHG_OC_CFG, val);
}

static hal_status_t init_fuel_gauge_unlocked(hal_adp5360_t *dev) {
  const hal_adp5360_fuel_gauge_config_t *c = &dev->cfg.fuel_gauge;
  const uint16_t curve_mv[10] = {c->curve.v_soc_0_mv,  c->curve.v_soc_5_mv,
                                 c->curve.v_soc_11_mv, c->curve.v_soc_19_mv,
                                 c->curve.v_soc_28_mv, c->curve.v_soc_41_mv,
                                 c->curve.v_soc_55_mv, c->curve.v_soc_69_mv,
                                 c->curve.v_soc_84_mv, c->curve.v_soc_100_mv};
  uint8_t buf[10] = {};
  for (uint8_t i = 0; i < 10u; ++i) {
    uint16_t idx = 0u;
    hal_status_t st =
        range_window_index(range_v_soc, ARRAY_COUNT(range_v_soc),
                           clamp_i32((int32_t)curve_mv[i], 2500, 4540),
                           clamp_i32((int32_t)curve_mv[i], 2500, 4540), &idx);
    if (st != HAL_OK || idx > 0xFFu) {
      return (st != HAL_OK) ? st : HAL_EINVAL;
    }
    buf[i] = (uint8_t)idx;
  }
  hal_status_t st =
      reg_burst_write_unlocked(dev, ADP5360_REG_V_SOC_0, buf, sizeof(buf));
  if (st != HAL_OK)
    return st;

  const uint16_t cap =
      (uint16_t)clamp_i32((int32_t)c->battery_capacity_mah, 0, 510);
  st = reg_write_unlocked(dev, ADP5360_REG_BAT_CAP, (uint8_t)(cap / 2u));
  if (st != HAL_OK)
    return st;

  uint8_t ctl =
      field_prep(ADP5360_FG_BAT_CAP_AGE_MASK, c->battery_capacity_age) |
      field_prep(ADP5360_FG_BAT_CAP_TEMP_MASK, c->battery_capacity_temp) |
      field_prep(ADP5360_FG_BAT_CAP_TEMP_EN_MASK,
                 c->battery_capacity_temp_enable) |
      field_prep(ADP5360_FG_BAT_CAP_AGE_EN_MASK,
                 c->battery_capacity_age_enable);
  st = reg_write_unlocked(dev, ADP5360_REG_BAT_SOC_ACM_CTL, ctl);
  if (st != HAL_OK)
    return st;

  uint8_t mode =
      field_prep(ADP5360_FG_SOC_LOW_THRESHOLD_MASK, c->soc_low_threshold) |
      field_prep(ADP5360_FG_SLEEP_CURRENT_MASK,
                 c->sleep_mode_current_threshold) |
      field_prep(ADP5360_FG_SLEEP_UPDATE_MASK, c->sleep_mode_update_rate) |
      field_prep(ADP5360_FG_OPERATION_MODE_MASK,
                 c->fuel_gauge_operation_sleep) |
      field_prep(ADP5360_FG_ENABLE_MASK, c->fuel_gauge_enable);
  return reg_write_unlocked(dev, ADP5360_REG_FUEL_GAUGE_MODE, mode);
}

static hal_status_t
init_regulator_unlocked(hal_adp5360_t *dev, hal_adp5360_regulator_t regulator,
                        const hal_adp5360_regulator_config_t *cfg) {
  const adp5360_reg_desc_t *desc = reg_desc(regulator);
  uint8_t temp_sup = 0u;
  uint8_t mask =
      ADP5360_SUPERVISORY_BUCK_RST_MASK | ADP5360_SUPERVISORY_BUCKBST_RST_MASK;
  hal_status_t st =
      reg_read_unlocked(dev, ADP5360_REG_SUPERVISORY_CFG, &temp_sup);
  if (st != HAL_OK)
    return st;
  st = reg_update_unlocked(dev, ADP5360_REG_SUPERVISORY_CFG, mask, 0u);
  if (st != HAL_OK)
    return st;
  st = reg_update_unlocked(dev, desc->out_reg, ADP5360_BUCK_OUTPUT_DLY_MSK,
                           (uint8_t)cfg->delay_idx);
  if (st != HAL_OK)
    return st;

  uint8_t val =
      field_prep(ADP5360_BUCK_CFG_SS_MSK, (uint8_t)cfg->soft_start_idx) |
      field_prep(ADP5360_BUCK_CFG_STP_MSK, cfg->enable_stop_pulse);
  val |= field_prep(desc->is_buckboost ? ADP5360_BUCK_CFG_BST_ILIM_MSK
                                       : ADP5360_BUCK_CFG_BUCK_ILIM_MSK,
                    (uint8_t)cfg->current_limit_idx);
  st = reg_write_unlocked(dev, desc->cfg_reg, val);
  if (st != HAL_OK)
    return st;

  if (desc->is_buckboost && dev->cfg.has_buckboost_enable_pin) {
    hal_gpio_set_mode(dev->cfg.buckboost_enable_pin, HAL_GPIO_OUTPUT);
    hal_gpio_write(dev->cfg.buckboost_enable_pin, false);
  }
  return reg_write_unlocked(dev, ADP5360_REG_SUPERVISORY_CFG, temp_sup);
}

hal_status_t hal_adp5360_init_ex(hal_adp5360_t *dev,
                                 const hal_adp5360_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  dev->cfg = (cfg != NULL) ? *cfg : hal_adp5360_default_config();
  dev->initialized = false;
  dev->charger_enabled = false;
  hal_status_t st = ensure_mutex(dev);
  if (st != HAL_OK)
    return st;

  hal_mutex_lock(dev->mutex);

  uint8_t id = 0u;
  st = reg_read_unlocked(dev, ADP5360_REG_DEVICE_ID, &id);
  if (st == HAL_OK && id != HAL_ADP5360_DEVICE_ID) {
    st = HAL_EPROTO;
  }
  if (st == HAL_OK) {
    uint8_t sup =
        field_prep(ADP5360_MFD_VOUT1_RST_MASK, dev->cfg.enable_vout1_reset) |
        field_prep(ADP5360_MFD_VOUT2_RST_MASK, dev->cfg.enable_vout2_reset) |
        field_prep(ADP5360_MFD_RESET_TIME_MASK, dev->cfg.reset_time_1p6s) |
        field_prep(ADP5360_MFD_WATCHDOG_TIME_MASK, dev->cfg.watchdog_time) |
        field_prep(ADP5360_MFD_ENABLE_WATCHDOG_MASK, dev->cfg.enable_watchdog) |
        field_prep(ADP5360_MFD_ENABLE_SHIPMENT_ON_MR_MASK,
                   dev->cfg.enable_manual_reset_shipment);
    st = reg_write_unlocked(dev, ADP5360_REG_SUPERVISORY_CFG, sup);
  }
  if (st == HAL_OK) {
    hal_delay_ms(dev->cfg.reset_time_1p6s ? 1600u : 200u);
    uint8_t dummy = 0u;
    st = reg_read_unlocked(dev, ADP5360_REG_INT_STATUS1, &dummy);
    if (st == HAL_OK)
      st = reg_read_unlocked(dev, ADP5360_REG_INT_STATUS2, &dummy);
  }
  if (st == HAL_OK && dev->cfg.has_manual_reset_pin) {
    hal_gpio_set_mode(dev->cfg.manual_reset_pin, HAL_GPIO_OUTPUT);
    hal_gpio_write(dev->cfg.manual_reset_pin, true);
  }
  if (st == HAL_OK && dev->cfg.init_charger) {
    st = init_charger_unlocked(dev);
  }
  if (st == HAL_OK && dev->cfg.init_fuel_gauge) {
    st = init_fuel_gauge_unlocked(dev);
  }
  if (st == HAL_OK && dev->cfg.init_buck) {
    st = init_regulator_unlocked(dev, HAL_ADP5360_REGULATOR_BUCK,
                                 &dev->cfg.buck);
  }
  if (st == HAL_OK && dev->cfg.init_buckboost) {
    st = init_regulator_unlocked(dev, HAL_ADP5360_REGULATOR_BUCKBOOST,
                                 &dev->cfg.buckboost);
  }
  dev->initialized = (st == HAL_OK);
  hal_mutex_unlock(dev->mutex);
  return st;
}

bool hal_adp5360_init(hal_adp5360_t *dev, const hal_adp5360_config_t *cfg) {
  return hal_status_to_bool(hal_adp5360_init_ex(dev, cfg));
}

void hal_adp5360_deinit(hal_adp5360_t *dev) {
  if (dev == NULL || dev->mutex == NULL) {
    return;
  }
  hal_mutex_destroy(dev->mutex);
  dev->mutex = NULL;
  dev->initialized = false;
}

#define WITH_DEV_LOCK(dev, body)                                               \
  do {                                                                         \
    hal_status_t _st = ensure_mutex(dev);                                      \
    if (_st != HAL_OK)                                                         \
      return _st;                                                              \
    if (!valid_dev(dev))                                                       \
      return HAL_EUNINIT;                                                      \
    hal_mutex_lock((dev)->mutex);                                              \
    body;                                                                      \
    hal_mutex_unlock((dev)->mutex);                                            \
    return _st;                                                                \
  } while (0)

hal_status_t hal_adp5360_reg_read(hal_adp5360_t *dev, uint8_t reg,
                                  uint8_t *out_value) {
  if (out_value == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, _st = reg_read_unlocked(dev, reg, out_value));
}

hal_status_t hal_adp5360_reg_write(hal_adp5360_t *dev, uint8_t reg,
                                   uint8_t value) {
  WITH_DEV_LOCK(dev, _st = reg_write_unlocked(dev, reg, value));
}

hal_status_t hal_adp5360_reg_burst_read(hal_adp5360_t *dev, uint8_t reg,
                                        uint8_t *out, size_t len) {
  if (len > 0u && out == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, _st = reg_burst_read_unlocked(dev, reg, out, len));
}

hal_status_t hal_adp5360_reg_burst_write(hal_adp5360_t *dev, uint8_t reg,
                                         const uint8_t *data, size_t len) {
  if (len > 0u && data == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, _st = reg_burst_write_unlocked(dev, reg, data, len));
}

hal_status_t hal_adp5360_reg_update(hal_adp5360_t *dev, uint8_t reg,
                                    uint8_t mask, uint8_t value) {
  WITH_DEV_LOCK(dev, _st = reg_update_unlocked(dev, reg, mask, value));
}

hal_status_t hal_adp5360_shipment_mode_enable(hal_adp5360_t *dev) {
  return hal_adp5360_reg_write(dev, ADP5360_REG_SHIPMENT, 1u);
}

hal_status_t hal_adp5360_shipment_mode_disable(hal_adp5360_t *dev) {
  return hal_adp5360_reg_write(dev, ADP5360_REG_SHIPMENT, 0u);
}

hal_status_t hal_adp5360_software_reset(hal_adp5360_t *dev) {
  WITH_DEV_LOCK(dev, {
    _st = reg_write_unlocked(dev, ADP5360_REG_SOC_RESET,
                             field_prep(ADP5360_MFD_SOC_RESET_MASK, 1u));
    if (_st == HAL_OK)
      _st = reg_write_unlocked(dev, ADP5360_REG_SOC_RESET,
                               field_prep(ADP5360_MFD_SOC_RESET_MASK, 0u));
  });
}

hal_status_t hal_adp5360_hardware_reset(hal_adp5360_t *dev) {
  WITH_DEV_LOCK(dev, {
    if (!dev->cfg.has_manual_reset_pin) {
      _st = HAL_EUNSUPPORTED;
    } else {
      hal_gpio_write(dev->cfg.manual_reset_pin, false);
      hal_delay_ms(40u);
      hal_gpio_write(dev->cfg.manual_reset_pin, true);
      hal_delay_ms(dev->cfg.reset_time_1p6s ? 1600u : 200u);
      uint8_t status = 0u;
      _st = reg_read_unlocked(dev, ADP5360_REG_PGOOD_STATUS, &status);
      if (_st == HAL_OK &&
          ((status & ADP5360_STATUS_MANUAL_RESET_INT_MASK) == 0u)) {
        _st = HAL_EIO;
      }
    }
  });
}

hal_status_t hal_adp5360_set_fuel_gauge_mode(hal_adp5360_t *dev,
                                             hal_adp5360_fg_mode_t mode) {
  if (mode != HAL_ADP5360_FG_MODE_SLEEP && mode != HAL_ADP5360_FG_MODE_ACTIVE) {
    return HAL_EINVAL;
  }
  return hal_adp5360_reg_update(dev, ADP5360_REG_FUEL_GAUGE_MODE,
                                ADP5360_MFD_FG_MODE_MASK,
                                mode == HAL_ADP5360_FG_MODE_SLEEP ? 1u : 0u);
}

hal_status_t hal_adp5360_charger_enable(hal_adp5360_t *dev, bool enable) {
  if (dev == NULL)
    return HAL_EINVAL;
  if (!dev->cfg.charger.enable_ldo)
    return HAL_EUNSUPPORTED;
  WITH_DEV_LOCK(dev, {
    _st = reg_update_unlocked(dev, ADP5360_REG_CHARGER_FUNC_CFG,
                              ADP5360_FUNC_EN_CHG, enable ? 1u : 0u);
    if (_st == HAL_OK)
      dev->charger_enabled = enable;
  });
}

hal_status_t
hal_adp5360_charger_get_online(hal_adp5360_t *dev,
                               hal_adp5360_charger_online_t *out_online) {
  if (out_online == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t v = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_FUNC_CFG, &v);
    if (_st == HAL_OK)
      *out_online = (v & ADP5360_FUNC_EN_CHG)
                        ? HAL_ADP5360_CHARGER_ONLINE_FIXED
                        : HAL_ADP5360_CHARGER_ONLINE_OFFLINE;
  });
}

hal_status_t hal_adp5360_charger_get_battery_present(hal_adp5360_t *dev,
                                                     bool *out_present) {
  if (out_present == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t v = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_STATUS_2, &v);
    if (_st == HAL_OK)
      *out_present = ((v & ADP5360_STATUS_2_BAT_CHG_MASK) != 1u);
  });
}

hal_status_t
hal_adp5360_charger_get_status(hal_adp5360_t *dev,
                               hal_adp5360_charger_status_t *out_status) {
  if (out_status == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t v = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_STATUS_1, &v);
    if (_st == HAL_OK) {
      switch (v & ADP5360_STATUS_1_CHARGER_MODE_MASK) {
      case 0:
        *out_status = HAL_ADP5360_CHARGER_STATUS_NOT_CHARGING;
        break;
      case 1:
      case 2:
      case 3:
        *out_status = HAL_ADP5360_CHARGER_STATUS_CHARGING;
        break;
      case 4:
        *out_status = HAL_ADP5360_CHARGER_STATUS_FULL;
        break;
      default:
        *out_status = HAL_ADP5360_CHARGER_STATUS_DISCHARGING;
        break;
      }
    }
  });
}

hal_status_t
hal_adp5360_charger_get_charge_type(hal_adp5360_t *dev,
                                    hal_adp5360_charge_type_t *out_type) {
  if (out_type == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t v = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_STATUS_1, &v);
    if (_st == HAL_OK) {
      switch (v & ADP5360_STATUS_1_CHARGER_MODE_MASK) {
      case 0:
      case 4:
        *out_type = HAL_ADP5360_CHARGE_TYPE_NONE;
        break;
      case 1:
        *out_type = HAL_ADP5360_CHARGE_TYPE_TRICKLE;
        break;
      case 2:
        *out_type = HAL_ADP5360_CHARGE_TYPE_FAST;
        break;
      case 3:
        *out_type = HAL_ADP5360_CHARGE_TYPE_LONGLIFE;
        break;
      default:
        *out_type = HAL_ADP5360_CHARGE_TYPE_UNKNOWN;
        break;
      }
    }
  });
}

hal_status_t
hal_adp5360_charger_get_health(hal_adp5360_t *dev,
                               hal_adp5360_charger_health_t *out_health) {
  if (out_health == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t v = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_STATUS_2, &v);
    if (_st != HAL_OK) {
    } else if (field_get(ADP5360_STATUS_2_THERM_MASK, v) == 1u) {
      *out_health = HAL_ADP5360_CHARGER_HEALTH_COLD;
    } else if (field_get(ADP5360_STATUS_2_THERM_MASK, v) == 2u) {
      *out_health = HAL_ADP5360_CHARGER_HEALTH_COOL;
    } else if (field_get(ADP5360_STATUS_2_THERM_MASK, v) == 3u) {
      *out_health = HAL_ADP5360_CHARGER_HEALTH_WARM;
    } else if (field_get(ADP5360_STATUS_2_THERM_MASK, v) == 4u) {
      *out_health = HAL_ADP5360_CHARGER_HEALTH_HOT;
    } else {
      _st = reg_read_unlocked(dev, ADP5360_REG_FAULT_STATUS, &v);
      if (_st == HAL_OK && (v & ADP5360_FAULT_WATCHDOG_TIMEOUT)) {
        *out_health = HAL_ADP5360_CHARGER_HEALTH_WATCHDOG_TIMER_EXPIRE;
      } else if (_st == HAL_OK && (v & ADP5360_FAULT_BAT_CHG_OVERVOLT)) {
        *out_health = HAL_ADP5360_CHARGER_HEALTH_OVERVOLTAGE;
      } else if (_st == HAL_OK && (v & ADP5360_FAULT_TEMP_SHUTDOWN)) {
        *out_health = HAL_ADP5360_CHARGER_HEALTH_OVERHEAT;
      } else if (_st == HAL_OK) {
        _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_STATUS_1, &v);
        if (_st == HAL_OK && ((v & ADP5360_STATUS_1_CHARGER_MODE_MASK) == 6u)) {
          *out_health = HAL_ADP5360_CHARGER_HEALTH_SAFETY_TIMER_EXPIRE;
        } else if (_st == HAL_OK) {
          _st = reg_read_unlocked(dev, ADP5360_REG_CHARGER_STATUS_2, &v);
          if (_st == HAL_OK)
            *out_health = ((v & ADP5360_STATUS_2_BAT_CHG_MASK) == 1u)
                              ? HAL_ADP5360_CHARGER_HEALTH_NO_BATTERY
                              : HAL_ADP5360_CHARGER_HEALTH_GOOD;
        }
      }
    }
  });
}

static hal_status_t set_range_field(hal_adp5360_t *dev,
                                    const adp5360_range_t *ranges,
                                    uint8_t range_n, uint8_t reg, uint8_t mask,
                                    uint32_t value) {
  uint16_t idx = 0u;
  hal_status_t st = range_index(ranges, range_n, (int32_t)value, &idx);
  if (st != HAL_OK)
    return st;
  return reg_update_unlocked(dev, reg, mask, (uint8_t)idx);
}

hal_status_t hal_adp5360_charger_set_fast_current(hal_adp5360_t *dev,
                                                  uint32_t ua) {
  if (dev == NULL)
    return HAL_EINVAL;
  ua = (uint32_t)clamp_i32((int32_t)ua, 10000, 320000);
  WITH_DEV_LOCK(dev, {
    _st = set_range_field(dev, range_i_fast, ARRAY_COUNT(range_i_fast),
                          ADP5360_REG_CHARGER_CURR_CFG, ADP5360_ICHG_MASK, ua);
    if (_st == HAL_OK)
      dev->charger_i_fast_ua = ua;
  });
}

hal_status_t hal_adp5360_charger_set_trickle_current(hal_adp5360_t *dev,
                                                     uint32_t ua) {
  if (dev == NULL)
    return HAL_EINVAL;
  ua = (uint32_t)clamp_i32((int32_t)ua, 1000, 10000);
  WITH_DEV_LOCK(dev, {
    _st = set_range_field(dev, range_i_trickle, ARRAY_COUNT(range_i_trickle),
                          ADP5360_REG_CHARGER_TERM_CFG, ADP5360_ITRK_MASK, ua);
    if (_st == HAL_OK)
      dev->charger_i_trickle_ua = ua;
  });
}

hal_status_t hal_adp5360_charger_set_termination_current(hal_adp5360_t *dev,
                                                         uint32_t ua) {
  if (dev == NULL)
    return HAL_EINVAL;
  ua = (uint32_t)clamp_i32((int32_t)ua, 5000, 32500);
  WITH_DEV_LOCK(dev, {
    _st = set_range_field(dev, range_i_term, ARRAY_COUNT(range_i_term),
                          ADP5360_REG_CHARGER_CURR_CFG, ADP5360_IEND_MASK, ua);
    if (_st == HAL_OK)
      dev->charger_i_term_ua = ua;
  });
}

hal_status_t hal_adp5360_charger_set_input_current_limit(hal_adp5360_t *dev,
                                                         uint32_t ua) {
  if (dev == NULL)
    return HAL_EINVAL;
  ua = (uint32_t)clamp_i32((int32_t)ua, 50000, 500000);
  WITH_DEV_LOCK(dev, {
    _st = set_range_field(dev, range_v_bus_i_limit,
                          ARRAY_COUNT(range_v_bus_i_limit),
                          ADP5360_REG_CHARGER_VBUS_ILIM, ADP5360_ILIM_MASK, ua);
    if (_st == HAL_OK)
      dev->charger_i_input_limit_ua = ua;
  });
}

hal_status_t hal_adp5360_charger_set_input_voltage_limit(hal_adp5360_t *dev,
                                                         uint32_t uv) {
  if (dev == NULL)
    return HAL_EINVAL;
  uv = (uint32_t)clamp_i32((int32_t)uv, 4400000, 4900000);
  WITH_DEV_LOCK(dev, {
    _st = set_range_field(
        dev, range_v_bus_adaptive, ARRAY_COUNT(range_v_bus_adaptive),
        ADP5360_REG_CHARGER_VBUS_ILIM, ADP5360_VADPICHG_MASK, uv);
    if (_st == HAL_OK)
      dev->charger_v_adpichg_uv = uv;
  });
}

hal_status_t
hal_adp5360_charger_clear_fault(hal_adp5360_t *dev,
                                hal_adp5360_charger_health_t health) {
  uint8_t v = 0u;
  if (health == HAL_ADP5360_CHARGER_HEALTH_WATCHDOG_TIMER_EXPIRE)
    v = ADP5360_FAULT_WATCHDOG_TIMEOUT;
  else if (health == HAL_ADP5360_CHARGER_HEALTH_OVERVOLTAGE)
    v = ADP5360_FAULT_BAT_CHG_OVERVOLT;
  else if (health == HAL_ADP5360_CHARGER_HEALTH_OVERHEAT)
    v = ADP5360_FAULT_TEMP_SHUTDOWN;
  else
    return HAL_EINVAL;
  return hal_adp5360_reg_write(dev, ADP5360_REG_FAULT_STATUS, v);
}

hal_status_t hal_adp5360_fuel_gauge_enable(hal_adp5360_t *dev, bool enable) {
  return hal_adp5360_reg_update(dev, ADP5360_REG_FUEL_GAUGE_MODE,
                                ADP5360_FG_ENABLE_MASK, enable ? 1u : 0u);
}

hal_status_t hal_adp5360_fuel_gauge_get_cycle_count(hal_adp5360_t *dev,
                                                    uint16_t *out_count) {
  if (out_count == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t b[2] = {};
    _st = reg_burst_read_unlocked(dev, ADP5360_REG_BAT_SOC_ACM_H, b, 2u);
    if (_st == HAL_OK)
      *out_count =
          (uint16_t)((((uint16_t)b[0] << 8) | b[1]) >> ADP5360_FG_SOC_SHIFT);
  });
}

hal_status_t hal_adp5360_fuel_gauge_get_voltage_uv(hal_adp5360_t *dev,
                                                   uint32_t *out_uv) {
  if (out_uv == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t b[2] = {};
    _st = reg_burst_read_unlocked(dev, ADP5360_REG_VBAT_READ_H, b, 2u);
    if (_st == HAL_OK)
      *out_uv =
          (uint32_t)((((uint16_t)b[0] << 8) | b[1]) >> ADP5360_FG_VBAT_SHIFT) *
          ADP5360_FG_MV_TO_UV;
  });
}

hal_status_t hal_adp5360_fuel_gauge_get_status(hal_adp5360_t *dev,
                                               uint8_t *out_status) {
  return hal_adp5360_reg_read(dev, ADP5360_REG_PGOOD_STATUS, out_status);
}

hal_status_t hal_adp5360_fuel_gauge_get_design_capacity_mah(hal_adp5360_t *dev,
                                                            uint16_t *out_mah) {
  if (out_mah == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t b = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_BAT_CAP, &b);
    if (_st == HAL_OK)
      *out_mah = (uint16_t)b * 2u;
  });
}

hal_status_t hal_adp5360_fuel_gauge_get_soc_pct(hal_adp5360_t *dev,
                                                uint8_t *out_pct) {
  if (out_pct == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t b = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_BAT_SOC, &b);
    if (_st == HAL_OK)
      *out_pct = (b > 100u) ? 100u : b;
  });
}

hal_status_t hal_adp5360_fuel_gauge_get_soc_alarm_pct(hal_adp5360_t *dev,
                                                      uint8_t *out_pct) {
  if (out_pct == NULL)
    return HAL_EINVAL;
  hal_status_t st = ensure_mutex(dev);
  if (st != HAL_OK)
    return st;
  if (!valid_dev(dev))
    return HAL_EUNINIT;
  hal_mutex_lock(dev->mutex);
  uint8_t b = 0u;
  st = reg_read_unlocked(dev, ADP5360_REG_FUEL_GAUGE_MODE, &b);
  if (st == HAL_OK) {
    static const uint8_t vals[4] = {6u, 11u, 21u, 31u};
    const uint8_t v = field_get(ADP5360_FG_SOC_LOW_THRESHOLD_MASK, b);
    *out_pct = vals[v & 3u];
  }
  hal_mutex_unlock(dev->mutex);
  return st;
}

hal_status_t
hal_adp5360_fuel_gauge_get_thermistor_voltage_uv(hal_adp5360_t *dev,
                                                 uint32_t *out_uv) {
  if (out_uv == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t b[2] = {};
    _st = reg_burst_read_unlocked(dev, ADP5360_REG_V_THERM, b, 2u);
    if (_st == HAL_OK)
      *out_uv = (uint32_t)(((uint16_t)b[1] << 8) | b[0]) * 1000u;
  });
}

hal_status_t hal_adp5360_fuel_gauge_set_soc_alarm_pct(hal_adp5360_t *dev,
                                                      uint8_t pct) {
  uint8_t v = 0u;
  if (pct == 6u)
    v = 0u;
  else if (pct == 11u)
    v = 1u;
  else if (pct == 21u)
    v = 2u;
  else if (pct == 31u)
    v = 3u;
  else
    return HAL_EINVAL;
  return hal_adp5360_reg_update(dev, ADP5360_REG_FUEL_GAUGE_MODE,
                                ADP5360_FG_SOC_LOW_THRESHOLD_MASK, v);
}

hal_status_t hal_adp5360_fuel_gauge_set_design_capacity_mah(hal_adp5360_t *dev,
                                                            uint16_t mah) {
  const uint16_t clamped = (uint16_t)clamp_i32((int32_t)mah, 0, 510);
  return hal_adp5360_reg_write(dev, ADP5360_REG_BAT_CAP,
                               (uint8_t)(clamped / 2u));
}

uint16_t hal_adp5360_regulator_count_voltages(hal_adp5360_regulator_t reg) {
  const adp5360_reg_desc_t *d = reg_desc(reg);
  return range_count(d->v_ranges, d->v_range_count);
}

hal_status_t hal_adp5360_regulator_list_voltage(hal_adp5360_regulator_t reg,
                                                uint16_t index,
                                                int32_t *out_uv) {
  const adp5360_reg_desc_t *d = reg_desc(reg);
  return list_range_index(d->v_ranges, d->v_range_count, index, out_uv);
}

uint16_t
hal_adp5360_regulator_count_current_limits(hal_adp5360_regulator_t reg) {
  const adp5360_reg_desc_t *d = reg_desc(reg);
  return range_count(d->i_ranges, d->i_range_count);
}

hal_status_t
hal_adp5360_regulator_list_current_limit(hal_adp5360_regulator_t reg,
                                         uint16_t index, int32_t *out_ua) {
  const adp5360_reg_desc_t *d = reg_desc(reg);
  return list_range_index(d->i_ranges, d->i_range_count, index, out_ua);
}

hal_status_t hal_adp5360_regulator_set_voltage(hal_adp5360_t *dev,
                                               hal_adp5360_regulator_t reg,
                                               int32_t min_uv, int32_t max_uv) {
  const adp5360_reg_desc_t *d = reg_desc(reg);
  uint16_t idx = 0u;
  hal_status_t st =
      range_window_index(d->v_ranges, d->v_range_count, min_uv, max_uv, &idx);
  if (st != HAL_OK)
    return st;
  return hal_adp5360_reg_update(dev, d->out_reg, ADP5360_BUCK_OUTPUT_VOUT_MSK,
                                (uint8_t)idx);
}

hal_status_t hal_adp5360_regulator_get_voltage(hal_adp5360_t *dev,
                                               hal_adp5360_regulator_t reg,
                                               int32_t *out_uv) {
  if (out_uv == NULL)
    return HAL_EINVAL;
  const adp5360_reg_desc_t *d = reg_desc(reg);
  WITH_DEV_LOCK(dev, {
    uint8_t raw = 0u;
    _st = reg_read_unlocked(dev, d->out_reg, &raw);
    if (_st == HAL_OK)
      _st = range_value(d->v_ranges, d->v_range_count,
                        field_get(ADP5360_BUCK_OUTPUT_VOUT_MSK, raw), out_uv);
  });
}

hal_status_t
hal_adp5360_regulator_set_current_limit(hal_adp5360_t *dev,
                                        hal_adp5360_regulator_t reg,
                                        int32_t min_ua, int32_t max_ua) {
  const adp5360_reg_desc_t *d = reg_desc(reg);
  uint16_t idx = 0u;
  hal_status_t st =
      range_window_index(d->i_ranges, d->i_range_count, min_ua, max_ua, &idx);
  if (st != HAL_OK)
    return st;
  return hal_adp5360_reg_update(dev, d->cfg_reg,
                                d->is_buckboost
                                    ? ADP5360_BUCK_CFG_BST_ILIM_MSK
                                    : ADP5360_BUCK_CFG_BUCK_ILIM_MSK,
                                (uint8_t)idx);
}

hal_status_t hal_adp5360_regulator_get_current_limit(
    hal_adp5360_t *dev, hal_adp5360_regulator_t reg, int32_t *out_ua) {
  if (out_ua == NULL)
    return HAL_EINVAL;
  const adp5360_reg_desc_t *d = reg_desc(reg);
  WITH_DEV_LOCK(dev, {
    uint8_t raw = 0u;
    const uint8_t mask = d->is_buckboost ? ADP5360_BUCK_CFG_BST_ILIM_MSK
                                         : ADP5360_BUCK_CFG_BUCK_ILIM_MSK;
    _st = reg_read_unlocked(dev, d->cfg_reg, &raw);
    if (_st == HAL_OK)
      _st = range_value(d->i_ranges, d->i_range_count, field_get(mask, raw),
                        out_ua);
  });
}

hal_status_t hal_adp5360_regulator_enable(hal_adp5360_t *dev,
                                          hal_adp5360_regulator_t reg) {
  if (dev != NULL && reg == HAL_ADP5360_REGULATOR_BUCKBOOST &&
      dev->cfg.has_buckboost_enable_pin) {
    hal_gpio_write(dev->cfg.buckboost_enable_pin, true);
  }
  return hal_adp5360_reg_update(dev, reg_desc(reg)->cfg_reg,
                                ADP5360_BUCK_CFG_EN_MSK, 1u);
}

hal_status_t hal_adp5360_regulator_disable(hal_adp5360_t *dev,
                                           hal_adp5360_regulator_t reg) {
  if (dev != NULL && reg == HAL_ADP5360_REGULATOR_BUCKBOOST &&
      dev->cfg.has_buckboost_enable_pin) {
    hal_gpio_write(dev->cfg.buckboost_enable_pin, false);
  }
  return hal_adp5360_reg_update(dev, reg_desc(reg)->cfg_reg,
                                ADP5360_BUCK_CFG_EN_MSK, 0u);
}

hal_status_t hal_adp5360_regulator_set_buck_mode(hal_adp5360_t *dev,
                                                 hal_adp5360_buck_mode_t mode) {
  if (mode != HAL_ADP5360_BUCK_MODE_AUTO && mode != HAL_ADP5360_BUCK_MODE_PWM)
    return HAL_EINVAL;
  return hal_adp5360_reg_update(dev, ADP5360_REG_BUCK_CFG,
                                ADP5360_BUCK_CFG_BUCK_MODE_MSK, mode);
}

hal_status_t
hal_adp5360_regulator_get_buck_mode(hal_adp5360_t *dev,
                                    hal_adp5360_buck_mode_t *out_mode) {
  if (out_mode == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t raw = 0u;
    _st = reg_read_unlocked(dev, ADP5360_REG_BUCK_CFG, &raw);
    if (_st == HAL_OK)
      *out_mode = field_get(ADP5360_BUCK_CFG_BUCK_MODE_MSK, raw)
                      ? HAL_ADP5360_BUCK_MODE_PWM
                      : HAL_ADP5360_BUCK_MODE_AUTO;
  });
}

hal_status_t hal_adp5360_regulator_set_active_discharge(
    hal_adp5360_t *dev, hal_adp5360_regulator_t reg, bool active) {
  return hal_adp5360_reg_update(dev, reg_desc(reg)->cfg_reg,
                                ADP5360_BUCK_CFG_DISCHG_MSK, active ? 1u : 0u);
}

hal_status_t hal_adp5360_regulator_get_active_discharge(
    hal_adp5360_t *dev, hal_adp5360_regulator_t reg, bool *out_active) {
  if (out_active == NULL)
    return HAL_EINVAL;
  WITH_DEV_LOCK(dev, {
    uint8_t raw = 0u;
    _st = reg_read_unlocked(dev, reg_desc(reg)->cfg_reg, &raw);
    if (_st == HAL_OK)
      *out_active = (raw & ADP5360_BUCK_CFG_DISCHG_MSK) != 0u;
  });
}

#endif /* HAL_ENABLE_ADP5360 && HAL_ENABLE_I2C */
#endif /* supported target */
