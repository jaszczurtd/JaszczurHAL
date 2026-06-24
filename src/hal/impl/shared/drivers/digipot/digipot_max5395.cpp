#include "hal/hal_config.h"

#if defined(HAL_ENABLE_DIGIPOT) && defined(HAL_ENABLE_MAX5395)

#include "hal_digipot_ops.h"

#define MAX5395_STEP_RESISTANCES 255u /* 256 taps - 1 */
#define MAX5395_WIPER_RES_QP_ON 25u   /* Ohms, charge pump enabled */
#define MAX5395_WIPER_RES_QP_OFF 45u  /* Ohms, charge pump disabled */
#define MAX5395_E2E_10K 10000u
#define MAX5395_E2E_50K 50000u
#define MAX5395_E2E_100K 100000u
#define MAX5395_ADDR_GND 0x28u
#define MAX5395_ADDR_NC 0x29u
#define MAX5395_ADDR_VDD 0x2Bu

#define MAX5395_CMD_WIPER 0x00u
#define MAX5395_CMD_CONFIG 0x80u
#define MAX5395_CMD_SD_CLR 0x80u
#define MAX5395_CMD_SD_L_WREG 0x88u
#define MAX5395_CMD_SD_H_WREG 0x90u
#define MAX5395_CMD_QP_OFF 0xA0u
#define MAX5395_CMD_RST 0xC0u
#define MAX5395_CONFIG_QP_MASK 0x80u

static bool max5395_addr_valid(uint8_t a) {
  return a == MAX5395_ADDR_GND || a == MAX5395_ADDR_NC || a == MAX5395_ADDR_VDD;
}

static bool max5395_e2e_valid(uint32_t r) {
  return r == MAX5395_E2E_10K || r == MAX5395_E2E_50K || r == MAX5395_E2E_100K;
}

static bool max5395_mode_valid(const hal_digipot_config_t *cfg) {
  return cfg->mode == HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER ||
         cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL ||
         cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH;
}

static bool max5395_validate(const hal_digipot_config_t *cfg) {
  return cfg != nullptr && max5395_addr_valid(cfg->i2c_addr) &&
         max5395_e2e_valid(cfg->e2e_resistance) && max5395_mode_valid(cfg);
}

static bool max5395_init(hal_digipot_config_t *cfg) {
  if (cfg == nullptr) {
    return false;
  }
  const uint8_t addr = cfg->i2c_addr;

  uint8_t rst[2] = {MAX5395_CMD_RST, 0x00u};
  if (!hal_digipot_i2c_write(cfg->i2c_bus, addr, rst, sizeof(rst))) {
    return false;
  }

  if (!cfg->charge_pump_en) {
    uint8_t qp[2] = {MAX5395_CMD_QP_OFF, 0x00u};
    if (!hal_digipot_i2c_write(cfg->i2c_bus, addr, qp, sizeof(qp))) {
      return false;
    }
  }

  if (cfg->mode != HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER) {
    const uint8_t cmd = (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL)
                            ? MAX5395_CMD_SD_H_WREG
                            : MAX5395_CMD_SD_L_WREG;
    uint8_t sd[2] = {cmd, 0x00u};
    if (!hal_digipot_i2c_write(cfg->i2c_bus, addr, sd, sizeof(sd))) {
      return false;
    }
  }
  return true;
}

static bool max5395_set_resistance(const hal_digipot_config_t *cfg,
                                   uint32_t ohms) {
  if (cfg == nullptr || ohms > cfg->e2e_resistance) {
    return false;
  }
  const uint8_t addr = cfg->i2c_addr;

  uint8_t wiper = 0u;
  switch (cfg->mode) {
  case HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER:
    wiper = hal_digipot_scale_to_wiper_trunc(ohms, cfg->e2e_resistance,
                                             MAX5395_STEP_RESISTANCES);
    break;
  case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL:
  case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH: {
    uint8_t config = 0u;
    if (!hal_digipot_i2c_read_cmd(cfg->i2c_bus, addr, MAX5395_CMD_CONFIG,
                                  &config)) {
      return false;
    }
    const uint32_t wiper_res = (0u == (config & MAX5395_CONFIG_QP_MASK))
                                   ? MAX5395_WIPER_RES_QP_OFF
                                   : MAX5395_WIPER_RES_QP_ON;
    const uint32_t r = (wiper_res > ohms) ? wiper_res : ohms;
    wiper = hal_digipot_scale_to_wiper_trunc(r - wiper_res, cfg->e2e_resistance,
                                             MAX5395_STEP_RESISTANCES);
    if (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH) {
      wiper = (uint8_t)(MAX5395_STEP_RESISTANCES - wiper);
    }
  } break;
  default:
    return false;
  }

  uint8_t clr[2] = {MAX5395_CMD_SD_CLR, 0x00u};
  if (!hal_digipot_i2c_write(cfg->i2c_bus, addr, clr, sizeof(clr))) {
    return false;
  }
  uint8_t wr[2] = {MAX5395_CMD_WIPER, wiper};
  if (!hal_digipot_i2c_write(cfg->i2c_bus, addr, wr, sizeof(wr))) {
    return false;
  }

  if (cfg->mode != HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER) {
    const uint8_t cmd = (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL)
                            ? MAX5395_CMD_SD_H_WREG
                            : MAX5395_CMD_SD_L_WREG;
    uint8_t sd[2] = {cmd, 0x00u};
    if (!hal_digipot_i2c_write(cfg->i2c_bus, addr, sd, sizeof(sd))) {
      return false;
    }
  }
  return true;
}

static uint16_t max5395_step_count(void) { return MAX5395_STEP_RESISTANCES; }

static const hal_digipot_ops_t s_max5395_ops = {
    max5395_validate,
    max5395_init,
    max5395_set_resistance,
    max5395_step_count,
};

const hal_digipot_ops_t *hal_digipot_max5395_ops(void) {
  return &s_max5395_ops;
}

#endif /* HAL_ENABLE_DIGIPOT && HAL_ENABLE_MAX5395 */
