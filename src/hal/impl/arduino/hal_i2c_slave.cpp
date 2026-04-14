#include "../../hal_config.h"
#ifndef HAL_DISABLE_I2C_SLAVE

#include "../../hal_i2c_slave.h"
#include "../../hal_sync.h"
#include <Wire.h>
#include <string.h>

/* ── Per-bus slave state ──────────────────────────────────────────────────── */

typedef struct {
    uint8_t     regs[HAL_I2C_SLAVE_REG_MAP_SIZE];
    uint8_t     reg_ptr;
    uint8_t     address;
    hal_mutex_t mutex;
    bool        initialized;
} i2c_slave_state_t;

static i2c_slave_state_t s_slave[2];

static inline uint8_t slave_bus_index(uint8_t bus) {
    return bus == 1 ? 1 : 0;
}

static TwoWire *slave_bus_wire(uint8_t bus) {
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    return slave_bus_index(bus) == 1 ? &Wire1 : &Wire;
#else
    (void)bus;
    return &Wire;
#endif
}

static void slave_ensure_mutex(uint8_t bus) {
    uint8_t idx = slave_bus_index(bus);
    if (!s_slave[idx].mutex) {
        hal_critical_section_enter();
        if (!s_slave[idx].mutex) {
            s_slave[idx].mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

/* ── Wire callbacks (bus 0) ───────────────────────────────────────────────── */

static void on_receive_0(int num_bytes) {
    i2c_slave_state_t *st = &s_slave[0];
    TwoWire *wire = &Wire;

    if (num_bytes < 1) return;

    /* First byte is the register pointer. */
    st->reg_ptr = (uint8_t)wire->read();
    num_bytes--;

    /* Any remaining bytes are written sequentially into the register map. */
    hal_mutex_lock(st->mutex);
    while (num_bytes-- > 0) {
        if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
            st->regs[st->reg_ptr++] = (uint8_t)wire->read();
        } else {
            (void)wire->read(); /* discard overflow */
        }
    }
    hal_mutex_unlock(st->mutex);
}

static void on_request_0(void) {
    i2c_slave_state_t *st = &s_slave[0];
    TwoWire *wire = &Wire;

    hal_mutex_lock(st->mutex);
    uint8_t ptr = st->reg_ptr;
    uint8_t remaining = (ptr < HAL_I2C_SLAVE_REG_MAP_SIZE)
                            ? (HAL_I2C_SLAVE_REG_MAP_SIZE - ptr) : 0;
    if (remaining > 0) {
        wire->write(&st->regs[ptr], remaining);
        st->reg_ptr = ptr + remaining;
    } else {
        uint8_t zero = 0;
        wire->write(&zero, 1);
    }
    hal_mutex_unlock(st->mutex);
}

/* ── Wire callbacks (bus 1) ───────────────────────────────────────────────── */

#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)

static void on_receive_1(int num_bytes) {
    i2c_slave_state_t *st = &s_slave[1];
    TwoWire *wire = &Wire1;

    if (num_bytes < 1) return;

    st->reg_ptr = (uint8_t)wire->read();
    num_bytes--;

    hal_mutex_lock(st->mutex);
    while (num_bytes-- > 0) {
        if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
            st->regs[st->reg_ptr++] = (uint8_t)wire->read();
        } else {
            (void)wire->read();
        }
    }
    hal_mutex_unlock(st->mutex);
}

static void on_request_1(void) {
    i2c_slave_state_t *st = &s_slave[1];
    TwoWire *wire = &Wire1;

    hal_mutex_lock(st->mutex);
    uint8_t ptr = st->reg_ptr;
    uint8_t remaining = (ptr < HAL_I2C_SLAVE_REG_MAP_SIZE)
                            ? (HAL_I2C_SLAVE_REG_MAP_SIZE - ptr) : 0;
    if (remaining > 0) {
        wire->write(&st->regs[ptr], remaining);
        st->reg_ptr = ptr + remaining;
    } else {
        uint8_t zero = 0;
        wire->write(&zero, 1);
    }
    hal_mutex_unlock(st->mutex);
}

#endif /* WIRE_INTERFACES_COUNT > 1 */

/* ── Public API ───────────────────────────────────────────────────────────── */

void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
    hal_i2c_slave_init_bus(0, sda_pin, scl_pin, address);
}

void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address) {
    uint8_t idx = slave_bus_index(bus);
    slave_ensure_mutex(idx);

    i2c_slave_state_t *st = &s_slave[idx];
    memset(st->regs, 0, sizeof(st->regs));
    st->reg_ptr = 0;
    st->address = address;
    st->initialized = true;

    TwoWire *wire = slave_bus_wire(idx);
    wire->setSDA(sda_pin);
    wire->setSCL(scl_pin);
    wire->begin(address);

    if (idx == 0) {
        wire->onReceive(on_receive_0);
        wire->onRequest(on_request_0);
    }
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    else {
        wire->onReceive(on_receive_1);
        wire->onRequest(on_request_1);
    }
#endif
}

void hal_i2c_slave_deinit(void) {
    hal_i2c_slave_deinit_bus(0);
}

void hal_i2c_slave_deinit_bus(uint8_t bus) {
    uint8_t idx = slave_bus_index(bus);
    slave_bus_wire(idx)->end();
    s_slave[idx].initialized = false;
}

/* ── Register accessors (mutex-protected) ─────────────────────────────────── */

void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value) {
    hal_i2c_slave_reg_write8_bus(0, reg, value);
}

void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value) {
    uint8_t idx = slave_bus_index(bus);
    if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) return;
    slave_ensure_mutex(idx);
    hal_mutex_lock(s_slave[idx].mutex);
    s_slave[idx].regs[reg] = value;
    hal_mutex_unlock(s_slave[idx].mutex);
}

void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value) {
    hal_i2c_slave_reg_write16_bus(0, reg, value);
}

void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value) {
    uint8_t idx = slave_bus_index(bus);
    if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) return;
    slave_ensure_mutex(idx);
    hal_mutex_lock(s_slave[idx].mutex);
    s_slave[idx].regs[reg]     = (uint8_t)(value >> 8);
    s_slave[idx].regs[reg + 1] = (uint8_t)(value & 0xFF);
    hal_mutex_unlock(s_slave[idx].mutex);
}

uint8_t hal_i2c_slave_reg_read8(uint8_t reg) {
    return hal_i2c_slave_reg_read8_bus(0, reg);
}

uint8_t hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg) {
    uint8_t idx = slave_bus_index(bus);
    if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    slave_ensure_mutex(idx);
    hal_mutex_lock(s_slave[idx].mutex);
    uint8_t val = s_slave[idx].regs[reg];
    hal_mutex_unlock(s_slave[idx].mutex);
    return val;
}

uint16_t hal_i2c_slave_reg_read16(uint8_t reg) {
    return hal_i2c_slave_reg_read16_bus(0, reg);
}

uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg) {
    uint8_t idx = slave_bus_index(bus);
    if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) return 0;
    slave_ensure_mutex(idx);
    hal_mutex_lock(s_slave[idx].mutex);
    uint16_t val = ((uint16_t)s_slave[idx].regs[reg] << 8)
                 | (uint16_t)s_slave[idx].regs[reg + 1];
    hal_mutex_unlock(s_slave[idx].mutex);
    return val;
}

uint8_t hal_i2c_slave_get_address(void) {
    return hal_i2c_slave_get_address_bus(0);
}

uint8_t hal_i2c_slave_get_address_bus(uint8_t bus) {
    return s_slave[slave_bus_index(bus)].address;
}

#endif /* HAL_DISABLE_I2C_SLAVE */
