#include "hal_digipot.h"

#ifdef HAL_ENABLE_DIGIPOT

#    include "hal_i2c.h"
#    include "hal_sync.h"

#    include <stddef.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Portable digital-potentiometer driver.
 *
 * Wire-style hal_i2c API. The same source compiles and runs on every backend
 * that provides hal_i2c (RP2040, STM32G474, mock).
 * ───────────────────────────────────────────────────────────────────────── */

/* ── MCP401x constants (Microchip) ───────────────────────────────────────── */
#    define MCP401X_I2C_ADDRESS      0x2Fu
#    define MCP401X_STEP_RESISTANCES 127u /* 128 taps - 1 */
#    define MCP401X_WIPER_RESISTANCE 150u /* Ohms */
#    define MCP401X_E2E_5K           5000u
#    define MCP401X_E2E_10K          10000u
#    define MCP401X_E2E_50K          50000u
#    define MCP401X_E2E_100K         100000u

/* ── MAX5395 constants (Maxim) ───────────────────────────────────────────── */
#    define MAX5395_STEP_RESISTANCES 255u /* 256 taps - 1 */
#    define MAX5395_WIPER_RES_QP_ON  25u  /* Ohms, charge pump enabled        */
#    define MAX5395_WIPER_RES_QP_OFF 45u  /* Ohms, charge pump disabled  */
#    define MAX5395_E2E_10K          10000u
#    define MAX5395_E2E_50K          50000u
#    define MAX5395_E2E_100K         100000u
#    define MAX5395_ADDR_GND         0x28u
#    define MAX5395_ADDR_NC          0x29u
#    define MAX5395_ADDR_VDD         0x2Bu
/* Command byte set (see MAX5395 datasheet). */
#    define MAX5395_CMD_WIPER      0x00u
#    define MAX5395_CMD_CONFIG     0x80u /* read config; also SD-clear opcode */
#    define MAX5395_CMD_SD_CLR     0x80u
#    define MAX5395_CMD_SD_L_WREG  0x88u /* open L, keep wiper register */
#    define MAX5395_CMD_SD_H_WREG  0x90u /* open H, keep wiper register */
#    define MAX5395_CMD_QP_OFF     0xA0u
#    define MAX5395_CMD_RST        0xC0u
#    define MAX5395_CONFIG_QP_MASK 0x80u

/* ── Instance pool ───────────────────────────────────────────────────────── */

struct hal_digipot_impl_s {
    bool in_use;
    hal_digipot_config_t cfg;
    hal_mutex_t mutex;
};

static hal_digipot_impl_s s_pool[HAL_DIGIPOT_MAX_INSTANCES];

static hal_digipot_impl_s *pool_alloc(void) {
    hal_digipot_impl_s *slot = NULL;
    hal_critical_section_enter();
    for (int i = 0; i < HAL_DIGIPOT_MAX_INSTANCES; ++i) {
        if (!s_pool[i].in_use) {
            s_pool[i].in_use = true;
            slot = &s_pool[i];
            break;
        }
    }
    hal_critical_section_exit();
    return slot;
}

/* ── hal_i2c (Wire-style) transaction helpers ────────────────────────────── */

static bool dp_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t n) {
    hal_i2c_begin_transmission_bus(bus, addr);
    for (size_t i = 0; i < n; ++i) {
        hal_i2c_write_bus(bus, data[i]);
    }
    return hal_i2c_end_transmission_bus(bus) == 0u;
}

/* Raw single-byte read (no register/command prefix) — MCP401x wiper read-back.
 */
static bool dp_read_raw(uint8_t bus, uint8_t addr, uint8_t *out) {
    if (hal_i2c_request_from_bus(bus, addr, 1u) != 1u) {
        return false;
    }
    const int v = hal_i2c_read_bus(bus);
    if (v < 0) {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

/* Command-then-read of one byte — MAX5395 configuration register read. */
static bool dp_read_cmd(uint8_t bus, uint8_t addr, uint8_t cmd, uint8_t *out) {
    hal_i2c_begin_transmission_bus(bus, addr);
    hal_i2c_write_bus(bus, cmd);
    if (hal_i2c_end_transmission_bus(bus) != 0u) {
        return false;
    }
    return dp_read_raw(bus, addr, out);
}

static uint8_t scale_to_wiper_trunc(uint32_t value, uint32_t full_scale,
                                    uint16_t steps) {
    return (uint8_t)(value * (uint32_t)steps / full_scale);
}

static uint8_t scale_to_wiper_round_nearest(uint32_t value,
                                            uint32_t full_scale,
                                            uint16_t steps) {
    const uint32_t scaled = value * (uint32_t)steps;
    uint8_t wiper = (uint8_t)(scaled / full_scale);
    const uint32_t remainder = scaled % full_scale;
    wiper += ((remainder * 2u) > full_scale) ? 1u : 0u;
    return wiper;
}

/* ── MCP401x ─────────────────────────────────────────────────────────────── */

static bool mcp401x_e2e_valid(uint32_t r) {
    return r == MCP401X_E2E_5K || r == MCP401X_E2E_10K ||
           r == MCP401X_E2E_50K || r == MCP401X_E2E_100K;
}

/* Device/mode legality: which modes each MCP401x variant supports. */
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

static bool mcp401x_set_resistance(const hal_digipot_config_t *cfg,
                                   uint32_t ohms) {
    if (ohms > cfg->e2e_resistance) {
        return false;
    }

    uint8_t wiper = 0u;
    switch (cfg->mode) {
        case HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER:
            if (cfg->mcp401x_device != HAL_DIGIPOT_MCP4018) {
                return false;
            }
            wiper = scale_to_wiper_trunc(ohms, cfg->e2e_resistance,
                                          MCP401X_STEP_RESISTANCES);
            break;
        case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL:
        case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH: {
            const uint32_t r = (MCP401X_WIPER_RESISTANCE > ohms) ?
                                   MCP401X_WIPER_RESISTANCE :
                                   ohms;
            wiper = scale_to_wiper_round_nearest(
                r - MCP401X_WIPER_RESISTANCE,
                cfg->e2e_resistance,
                MCP401X_STEP_RESISTANCES);

            if (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH) {
                if (cfg->mcp401x_device != HAL_DIGIPOT_MCP4018) {
                    return false;
                }
                wiper = (uint8_t)(MCP401X_STEP_RESISTANCES - wiper);
            }
        } break;
        default:
            return false;
    }

    /* Set the wiper register, then read it back to verify the write. */
    if (!dp_write(cfg->i2c_bus, MCP401X_I2C_ADDRESS, &wiper, 1u)) {
        return false;
    }
    uint8_t reg = 0u;
    if (!dp_read_raw(cfg->i2c_bus, MCP401X_I2C_ADDRESS, &reg)) {
        return false;
    }
    return wiper == reg;
}

/* ── MAX5395 ─────────────────────────────────────────────────────────────── */

static bool max5395_addr_valid(uint8_t a) {
    return a == MAX5395_ADDR_GND || a == MAX5395_ADDR_NC ||
           a == MAX5395_ADDR_VDD;
}

static bool max5395_e2e_valid(uint32_t r) {
    return r == MAX5395_E2E_10K || r == MAX5395_E2E_50K ||
           r == MAX5395_E2E_100K;
}

static bool max5395_mode_valid(const hal_digipot_config_t *cfg) {
    return cfg->mode == HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER ||
           cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL ||
           cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH;
}

static bool max5395_init_chip(const hal_digipot_config_t *cfg) {
    const uint8_t addr = cfg->i2c_addr;

    /* Return the device to power-on defaults (wiper midscale, charge pump on,
     * shutdown modes cleared). */
    uint8_t rst[2] = { MAX5395_CMD_RST, 0x00u };
    if (!dp_write(cfg->i2c_bus, addr, rst, sizeof(rst))) {
        return false;
    }

    /* Disable the charge pump if the caller asked for low-power operation. */
    if (!cfg->charge_pump_en) {
        uint8_t qp[2] = { MAX5395_CMD_QP_OFF, 0x00u };
        if (!dp_write(cfg->i2c_bus, addr, qp, sizeof(qp))) {
            return false;
        }
    }

    /* In rheostat modes, open the unused terminal to reduce system current. */
    if (cfg->mode != HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER) {
        const uint8_t cmd =
            (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL) ?
                MAX5395_CMD_SD_H_WREG :
                MAX5395_CMD_SD_L_WREG;
        uint8_t sd[2] = { cmd, 0x00u };
        if (!dp_write(cfg->i2c_bus, addr, sd, sizeof(sd))) {
            return false;
        }
    }
    return true;
}

static bool max5395_set_resistance(const hal_digipot_config_t *cfg,
                                   uint32_t ohms) {
    if (ohms > cfg->e2e_resistance) {
        return false;
    }
    const uint8_t addr = cfg->i2c_addr;

    uint8_t wiper = 0u;
    switch (cfg->mode) {
        case HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER:
            wiper = scale_to_wiper_trunc(ohms, cfg->e2e_resistance,
                                          MAX5395_STEP_RESISTANCES);
            break;
        case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL:
        case HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH: {
            uint8_t config = 0u;
            if (!dp_read_cmd(cfg->i2c_bus, addr, MAX5395_CMD_CONFIG, &config)) {
                return false;
            }
            const uint32_t wiper_res =
                (0u == (config & MAX5395_CONFIG_QP_MASK)) ?
                    MAX5395_WIPER_RES_QP_OFF :
                    MAX5395_WIPER_RES_QP_ON;
            const uint32_t r = (wiper_res > ohms) ? wiper_res : ohms;
            wiper = scale_to_wiper_trunc(r - wiper_res,
                                          cfg->e2e_resistance,
                                          MAX5395_STEP_RESISTANCES);
            if (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH) {
                wiper = (uint8_t)(MAX5395_STEP_RESISTANCES - wiper);
            }
        } break;
        default:
            return false;
    }

    /* Remove any existing shutdown condition, then load the wiper register. */
    uint8_t clr[2] = { MAX5395_CMD_SD_CLR, 0x00u };
    if (!dp_write(cfg->i2c_bus, addr, clr, sizeof(clr))) {
        return false;
    }
    uint8_t wr[2] = { MAX5395_CMD_WIPER, wiper };
    if (!dp_write(cfg->i2c_bus, addr, wr, sizeof(wr))) {
        return false;
    }

    /* Re-assert the shutdown condition for the unused terminal (rheostat). */
    if (cfg->mode != HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER) {
        const uint8_t cmd =
            (cfg->mode == HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL) ?
                MAX5395_CMD_SD_H_WREG :
                MAX5395_CMD_SD_L_WREG;
        uint8_t sd[2] = { cmd, 0x00u };
        if (!dp_write(cfg->i2c_bus, addr, sd, sizeof(sd))) {
            return false;
        }
    }
    return true;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

hal_digipot_t hal_digipot_init(const hal_digipot_config_t *cfg) {
    if (cfg == NULL) {
        return NULL;
    }

    /* Validate the configuration against the selected chip before allocating.
     */
    switch (cfg->chip) {
        case HAL_DIGIPOT_CHIP_MCP401X:
            if (!mcp401x_e2e_valid(cfg->e2e_resistance) ||
                !mcp401x_mode_valid(cfg)) {
                return NULL;
            }
            break;
        case HAL_DIGIPOT_CHIP_MAX5395:
            if (!max5395_addr_valid(cfg->i2c_addr) ||
                !max5395_e2e_valid(cfg->e2e_resistance) ||
                !max5395_mode_valid(cfg)) {
                return NULL;
            }
            break;
        default:
            return NULL;
    }

    hal_digipot_impl_s *h = pool_alloc();
    if (h == NULL) {
        return NULL;
    }
    h->cfg = *cfg;
    h->mutex = hal_mutex_create();

    /* MCP401x: address is fixed in silicon regardless of what the caller set.
     */
    if (cfg->chip == HAL_DIGIPOT_CHIP_MCP401X) {
        h->cfg.i2c_addr = MCP401X_I2C_ADDRESS;
    }

    bool ok = true;
    if (cfg->chip == HAL_DIGIPOT_CHIP_MAX5395) {
        if (h->mutex != NULL) {
            hal_mutex_lock(h->mutex);
        }
        ok = max5395_init_chip(&h->cfg);
        if (h->mutex != NULL) {
            hal_mutex_unlock(h->mutex);
        }
    }

    if (!ok) {
        hal_digipot_deinit(h);
        return NULL;
    }
    return h;
}

void hal_digipot_deinit(hal_digipot_t h) {
    if (h == NULL) {
        return;
    }
    if (h->mutex != NULL) {
        hal_mutex_destroy(h->mutex);
        h->mutex = NULL;
    }
    h->in_use = false;
}

bool hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms) {
    if (h == NULL || !h->in_use) {
        return false;
    }
    if (h->mutex != NULL) {
        hal_mutex_lock(h->mutex);
    }
    bool ok = false;
    switch (h->cfg.chip) {
        case HAL_DIGIPOT_CHIP_MCP401X:
            ok = mcp401x_set_resistance(&h->cfg, ohms);
            break;
        case HAL_DIGIPOT_CHIP_MAX5395:
            ok = max5395_set_resistance(&h->cfg, ohms);
            break;
        default:
            ok = false;
            break;
    }
    if (h->mutex != NULL) {
        hal_mutex_unlock(h->mutex);
    }
    return ok;
}

uint16_t hal_digipot_step_count(hal_digipot_t h) {
    if (h == NULL || !h->in_use) {
        return 0u;
    }
    return (h->cfg.chip == HAL_DIGIPOT_CHIP_MAX5395) ?
               MAX5395_STEP_RESISTANCES :
               MCP401X_STEP_RESISTANCES;
}

uint32_t hal_digipot_e2e_resistance(hal_digipot_t h) {
    return (h != NULL && h->in_use) ? h->cfg.e2e_resistance : 0u;
}

hal_digipot_mode_t hal_digipot_mode(hal_digipot_t h) {
    return (h != NULL && h->in_use) ? h->cfg.mode :
                                      HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;
}

#endif /* HAL_ENABLE_DIGIPOT */
