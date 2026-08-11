#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_DIGIPOT) && defined(HAL_ENABLE_MCP401X)

#include "hal_digipot_ops.h"

#define MCP401X_I2C_ADDRESS 0x2Fu
#define MCP401X_STEP_RESISTANCES 127u /* 128 taps - 1 */
#define MCP401X_WIPER_RESISTANCE 150u /* Ohms */
#define MCP401X_E2E_5K 5000u
#define MCP401X_E2E_10K 10000u
#define MCP401X_E2E_50K 50000u
#define MCP401X_E2E_100K 100000u

static bool mcp401x_e2e_valid(uint32_t r) {
  return r == MCP401X_E2E_5K || r == MCP401X_E2E_10K || r == MCP401X_E2E_50K ||
         r == MCP401X_E2E_100K;
}

static bool mcp401x_mode_valid(const hal_digipot_config_t *cfg) {
  switch (cfg->mcp401x_device) {
  case HAL_DIGIPOT_MCP4017:
  case HAL_DIGIPOT_MCP4019:
    return cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
  case HAL_DIGIPOT_MCP4018:
    return cfg->mode == HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER ||
           cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL ||
           cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH;
  default:
    return false;
  }
}

static bool mcp401x_validate(const hal_digipot_config_t *cfg) {
  return cfg != nullptr && mcp401x_e2e_valid(cfg->e2e_resistance) &&
         mcp401x_mode_valid(cfg);
}

static hal_status_t mcp401x_init(hal_digipot_config_t *cfg) {
  if (cfg == nullptr) {
    return HAL_EINVAL;
  }
  /* Address is fixed in silicon regardless of what the caller set. */
  cfg->i2c_addr = MCP401X_I2C_ADDRESS;
  return HAL_OK;
}

static hal_status_t mcp401x_set_resistance(const hal_digipot_config_t *cfg,
                                           uint32_t ohms) {
  if (cfg == nullptr || ohms > cfg->e2e_resistance) {
    return HAL_EINVAL;
  }

  uint8_t wiper = 0u;
  switch (cfg->mode) {
  case HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER:
    if (cfg->mcp401x_device != HAL_DIGIPOT_MCP4018) {
      return HAL_EINVAL;
    }
    wiper = hal_digipot_scale_to_wiper_trunc(ohms, cfg->e2e_resistance,
                                             MCP401X_STEP_RESISTANCES);
    break;
  case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL:
  case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH: {
    const uint32_t r =
        (MCP401X_WIPER_RESISTANCE > ohms) ? MCP401X_WIPER_RESISTANCE : ohms;
    wiper = hal_digipot_scale_to_wiper_round_nearest(
        r - MCP401X_WIPER_RESISTANCE, cfg->e2e_resistance,
        MCP401X_STEP_RESISTANCES);

    if (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH) {
      if (cfg->mcp401x_device != HAL_DIGIPOT_MCP4018) {
        return HAL_EINVAL;
      }
      wiper = (uint8_t)(MCP401X_STEP_RESISTANCES - wiper);
    }
  } break;
  default:
    return HAL_EINVAL;
  }

  if (!hal_digipot_i2c_write(cfg->i2c_bus, MCP401X_I2C_ADDRESS, &wiper, 1u)) {
    return HAL_EBUS;
  }
  uint8_t reg = 0u;
  if (!hal_digipot_i2c_read_raw(cfg->i2c_bus, MCP401X_I2C_ADDRESS, &reg)) {
    return HAL_EBUS;
  }
  return wiper == reg ? HAL_OK : HAL_EIO;
}

static uint16_t mcp401x_step_count(void) { return MCP401X_STEP_RESISTANCES; }

static const hal_digipot_ops_t s_mcp401x_ops = {
    mcp401x_validate,
    mcp401x_init,
    mcp401x_set_resistance,
    mcp401x_step_count,
};

const hal_digipot_ops_t *hal_digipot_mcp401x_ops(void) {
  return &s_mcp401x_ops;
}

#endif /* HAL_ENABLE_DIGIPOT && HAL_ENABLE_MCP401X */
