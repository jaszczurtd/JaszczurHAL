#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_I2C_SLAVE

#include "../../hal_i2c_slave.h"
#include <Wire.h>
#include <pico/critical_section.h>
#include <string.h>

/* ── Per-bus slave state ────────────────────────────────────────────────────
 */

typedef struct {
  uint8_t regs[HAL_I2C_SLAVE_REG_MAP_SIZE];
  uint8_t reg_ptr;
  uint8_t address;
  critical_section_t reg_lock;
  volatile unsigned int reg_lock_state;
  bool initialized;
  uint32_t transaction_count;
} i2c_slave_state_t;

static i2c_slave_state_t s_slave[2];

enum { SLAVE_LOCK_UNINIT = 0u, SLAVE_LOCK_INITING = 1u, SLAVE_LOCK_READY = 2u };

static inline uint8_t slave_bus_index(uint8_t bus) { return bus == 1 ? 1 : 0; }

static TwoWire *slave_bus_wire(uint8_t bus) {
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
  return slave_bus_index(bus) == 1 ? &Wire1 : &Wire;
#else
  (void)bus;
  return &Wire;
#endif
}

static void slave_ensure_lock(uint8_t bus) {
  uint8_t idx = slave_bus_index(bus);
  i2c_slave_state_t *st = &s_slave[idx];
  if (__atomic_load_n(&st->reg_lock_state, __ATOMIC_ACQUIRE) ==
      SLAVE_LOCK_READY) {
    return;
  }

  unsigned int expected = SLAVE_LOCK_UNINIT;
  if (__atomic_compare_exchange_n(&st->reg_lock_state, &expected,
                                  SLAVE_LOCK_INITING, false, __ATOMIC_ACQUIRE,
                                  __ATOMIC_RELAXED)) {
    critical_section_init(&st->reg_lock);
    __atomic_store_n(&st->reg_lock_state, SLAVE_LOCK_READY, __ATOMIC_RELEASE);
    return;
  }

  while (__atomic_load_n(&st->reg_lock_state, __ATOMIC_ACQUIRE) !=
         SLAVE_LOCK_READY) {
  }
}

static inline void slave_lock(i2c_slave_state_t *st) {
  critical_section_enter_blocking(&st->reg_lock);
}

static inline void slave_unlock(i2c_slave_state_t *st) {
  critical_section_exit(&st->reg_lock);
}

/* ── Wire callbacks (bus 0) ─────────────────────────────────────────────────
 */

static void on_receive_0(int num_bytes) {
  i2c_slave_state_t *st = &s_slave[0];
  TwoWire *wire = &Wire;

  if (num_bytes < 1)
    return;

  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  slave_lock(st);
  st->reg_ptr = (uint8_t)wire->read();
  num_bytes--;
  while (num_bytes-- > 0) {
    if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
      st->regs[st->reg_ptr++] = (uint8_t)wire->read();
    } else {
      (void)wire->read(); /* discard overflow */
    }
  }
  slave_unlock(st);
}

static void on_request_0(void) {
  i2c_slave_state_t *st = &s_slave[0];
  TwoWire *wire = &Wire;

  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  slave_lock(st);
  uint8_t ptr = st->reg_ptr;
  uint8_t remaining = (ptr < HAL_I2C_SLAVE_REG_MAP_SIZE)
                          ? (HAL_I2C_SLAVE_REG_MAP_SIZE - ptr)
                          : 0;
  if (remaining > 0) {
    wire->write(&st->regs[ptr], remaining);
    st->reg_ptr = ptr + remaining;
  } else {
    uint8_t zero = 0;
    wire->write(&zero, 1);
  }
  slave_unlock(st);
}

/* ── Wire callbacks (bus 1) ─────────────────────────────────────────────────
 */

#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)

static void on_receive_1(int num_bytes) {
  i2c_slave_state_t *st = &s_slave[1];
  TwoWire *wire = &Wire1;

  if (num_bytes < 1)
    return;

  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);

  slave_lock(st);
  st->reg_ptr = (uint8_t)wire->read();
  num_bytes--;

  while (num_bytes-- > 0) {
    if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
      st->regs[st->reg_ptr++] = (uint8_t)wire->read();
    } else {
      (void)wire->read();
    }
  }
  slave_unlock(st);
}

static void on_request_1(void) {
  i2c_slave_state_t *st = &s_slave[1];
  TwoWire *wire = &Wire1;

  __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
  slave_lock(st);
  uint8_t ptr = st->reg_ptr;
  uint8_t remaining = (ptr < HAL_I2C_SLAVE_REG_MAP_SIZE)
                          ? (HAL_I2C_SLAVE_REG_MAP_SIZE - ptr)
                          : 0;
  if (remaining > 0) {
    wire->write(&st->regs[ptr], remaining);
    st->reg_ptr = ptr + remaining;
  } else {
    uint8_t zero = 0;
    wire->write(&zero, 1);
  }
  slave_unlock(st);
}

#endif /* WIRE_INTERFACES_COUNT > 1 */

/* ── Public API ─────────────────────────────────────────────────────────────
 */

void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
  hal_i2c_slave_init_bus(0, sda_pin, scl_pin, address);
}

void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address) {
  uint8_t idx = slave_bus_index(bus);
  slave_ensure_lock(idx);

  i2c_slave_state_t *st = &s_slave[idx];
  slave_lock(st);
  memset(st->regs, 0, sizeof(st->regs));
  st->reg_ptr = 0;
  st->address = address;
  st->initialized = true;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
  slave_unlock(st);

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

void hal_i2c_slave_deinit(void) { hal_i2c_slave_deinit_bus(0); }

void hal_i2c_slave_deinit_bus(uint8_t bus) {
  uint8_t idx = slave_bus_index(bus);
  slave_bus_wire(idx)->end();
  s_slave[idx].initialized = false;
}

/* ── Register accessors (mutex-protected) ───────────────────────────────────
 */

void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value) {
  hal_i2c_slave_reg_write8_bus(0, reg, value);
}

void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value) {
  uint8_t idx = slave_bus_index(bus);
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE)
    return;
  slave_ensure_lock(idx);
  slave_lock(&s_slave[idx]);
  s_slave[idx].regs[reg] = value;
  slave_unlock(&s_slave[idx]);
}

void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value) {
  hal_i2c_slave_reg_write16_bus(0, reg, value);
}

void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value) {
  uint8_t idx = slave_bus_index(bus);
  if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE)
    return;
  slave_ensure_lock(idx);
  slave_lock(&s_slave[idx]);
  s_slave[idx].regs[reg] = (uint8_t)(value >> 8);
  s_slave[idx].regs[reg + 1] = (uint8_t)(value & 0xFF);
  slave_unlock(&s_slave[idx]);
}

uint8_t hal_i2c_slave_reg_read8(uint8_t reg) {
  return hal_i2c_slave_reg_read8_bus(0, reg);
}

uint8_t hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg) {
  uint8_t idx = slave_bus_index(bus);
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE)
    return 0;
  slave_ensure_lock(idx);
  slave_lock(&s_slave[idx]);
  uint8_t val = s_slave[idx].regs[reg];
  slave_unlock(&s_slave[idx]);
  return val;
}

uint16_t hal_i2c_slave_reg_read16(uint8_t reg) {
  return hal_i2c_slave_reg_read16_bus(0, reg);
}

uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg) {
  uint8_t idx = slave_bus_index(bus);
  if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE)
    return 0;
  slave_ensure_lock(idx);
  slave_lock(&s_slave[idx]);
  uint16_t val = ((uint16_t)s_slave[idx].regs[reg] << 8) |
                 (uint16_t)s_slave[idx].regs[reg + 1];
  slave_unlock(&s_slave[idx]);
  return val;
}

uint8_t hal_i2c_slave_get_address(void) {
  return hal_i2c_slave_get_address_bus(0);
}

uint8_t hal_i2c_slave_get_address_bus(uint8_t bus) {
  return s_slave[slave_bus_index(bus)].address;
}

uint32_t hal_i2c_slave_get_transaction_count(void) {
  return hal_i2c_slave_get_transaction_count_bus(0);
}

uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus) {
  return __atomic_load_n(&s_slave[slave_bus_index(bus)].transaction_count,
                         __ATOMIC_ACQUIRE);
}

#endif /* HAL_ENABLE_I2C_SLAVE */
#endif // HAL_TARGET_IS_RP2040
