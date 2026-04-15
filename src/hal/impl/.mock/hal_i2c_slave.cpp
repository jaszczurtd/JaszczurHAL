#include "../../hal_i2c_slave.h"
#include "hal_mock.h"

#include <string.h>

typedef struct {
    uint8_t  regs[HAL_I2C_SLAVE_REG_MAP_SIZE];
    uint8_t  reg_ptr;
    uint8_t  address;
    bool     initialized;
    int      lock_depth;
} mock_i2c_slave_state_t;

static mock_i2c_slave_state_t s_slave_state[2];

static inline uint8_t slave_bus_idx(uint8_t bus) {
    return bus == 1 ? 1 : 0;
}

static inline mock_i2c_slave_state_t *slave_st(uint8_t bus) {
    return &s_slave_state[slave_bus_idx(bus)];
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
    hal_i2c_slave_init_bus(0, sda_pin, scl_pin, address);
}

void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address) {
    (void)sda_pin; (void)scl_pin;
    mock_i2c_slave_state_t *st = slave_st(bus);
    memset(st->regs, 0, sizeof(st->regs));
    st->reg_ptr     = 0;
    st->address     = address;
    st->initialized = true;
    st->lock_depth  = 0;
}

void hal_i2c_slave_deinit(void) {
    hal_i2c_slave_deinit_bus(0);
}

void hal_i2c_slave_deinit_bus(uint8_t bus) {
    mock_i2c_slave_state_t *st = slave_st(bus);
    st->initialized = false;
    st->address     = 0;
    st->reg_ptr     = 0;
    st->lock_depth  = 0;
    memset(st->regs, 0, sizeof(st->regs));
}

/* ── Register accessors ──────────────────────────────────────────────────── */

uint8_t hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value) {
    return hal_i2c_slave_reg_write8_bus(0, reg, value);
}

uint8_t hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value) {
    if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    mock_i2c_slave_state_t *st = slave_st(bus);
    st->lock_depth++;
    st->regs[reg] = value;
    st->lock_depth--;
    return 1;
}

uint16_t hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value) {
    return hal_i2c_slave_reg_write16_bus(0, reg, value);
}

uint16_t hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value) {
    if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    mock_i2c_slave_state_t *st = slave_st(bus);
    st->lock_depth++;
    st->regs[reg]     = (uint8_t)(value >> 8);
    st->regs[reg + 1] = (uint8_t)(value & 0xFF);
    st->lock_depth--;
    return 2;
}

uint8_t hal_i2c_slave_reg_read8(uint8_t reg) {
    return hal_i2c_slave_reg_read8_bus(0, reg);
}

uint8_t hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg) {
    if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    mock_i2c_slave_state_t *st = slave_st(bus);
    st->lock_depth++;
    uint8_t val = st->regs[reg];
    st->lock_depth--;
    return val;
}

uint16_t hal_i2c_slave_reg_read16(uint8_t reg) {
    return hal_i2c_slave_reg_read16_bus(0, reg);
}

uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg) {
    if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    mock_i2c_slave_state_t *st = slave_st(bus);
    st->lock_depth++;
    uint16_t val = ((uint16_t)st->regs[reg] << 8) | (uint16_t)st->regs[reg + 1];
    st->lock_depth--;
    return val;
}

uint8_t hal_i2c_slave_get_address(void) {
    return hal_i2c_slave_get_address_bus(0);
}

uint8_t hal_i2c_slave_get_address_bus(uint8_t bus) {
    return slave_st(bus)->address;
}

/* ── Mock helpers ─────────────────────────────────────────────────────────── */

bool hal_mock_i2c_slave_is_initialized(void) {
    return hal_mock_i2c_slave_is_initialized_bus(0);
}

bool hal_mock_i2c_slave_is_initialized_bus(uint8_t bus) {
    return slave_st(bus)->initialized;
}

uint8_t hal_mock_i2c_slave_get_reg(uint8_t reg) {
    return hal_mock_i2c_slave_get_reg_bus(0, reg);
}

uint8_t hal_mock_i2c_slave_get_reg_bus(uint8_t bus, uint8_t reg) {
    if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    return slave_st(bus)->regs[reg];
}

void hal_mock_i2c_slave_set_reg(uint8_t reg, uint8_t value) {
    hal_mock_i2c_slave_set_reg_bus(0, reg, value);
}

void hal_mock_i2c_slave_set_reg_bus(uint8_t bus, uint8_t reg, uint8_t value) {
    if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) return;
    slave_st(bus)->regs[reg] = value;
}

uint8_t hal_mock_i2c_slave_get_reg_ptr(void) {
    return hal_mock_i2c_slave_get_reg_ptr_bus(0);
}

uint8_t hal_mock_i2c_slave_get_reg_ptr_bus(uint8_t bus) {
    return slave_st(bus)->reg_ptr;
}

void hal_mock_i2c_slave_simulate_receive(const uint8_t *data, int len) {
    hal_mock_i2c_slave_simulate_receive_bus(0, data, len);
}

void hal_mock_i2c_slave_simulate_receive_bus(uint8_t bus, const uint8_t *data,
                                             int len) {
    if (len < 1) return;
    mock_i2c_slave_state_t *st = slave_st(bus);

    /* First byte = register pointer. */
    st->reg_ptr = data[0];

    /* Remaining bytes written into registers sequentially. */
    for (int i = 1; i < len; i++) {
        if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
            st->regs[st->reg_ptr++] = data[i];
        }
    }
}

int hal_mock_i2c_slave_simulate_request(uint8_t *out_buf, int max_len) {
    return hal_mock_i2c_slave_simulate_request_bus(0, out_buf, max_len);
}

int hal_mock_i2c_slave_simulate_request_bus(uint8_t bus, uint8_t *out_buf,
                                            int max_len) {
    mock_i2c_slave_state_t *st = slave_st(bus);
    int count = 0;
    while (count < max_len && st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
        out_buf[count++] = st->regs[st->reg_ptr++];
    }
    return count;
}
