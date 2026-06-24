#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_I2C_SLAVE

#include "../../hal_i2c_slave.h"

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <pico/critical_section.h>
#include <string.h>

#define RP2040_I2C_SLAVE_INIT_CLOCK_HZ 100000u

typedef struct {
  uint8_t regs[HAL_I2C_SLAVE_REG_MAP_SIZE];
  uint8_t reg_ptr;
  uint8_t address;
  uint8_t sda_pin;
  uint8_t scl_pin;
  critical_section_t reg_lock;
  volatile unsigned int reg_lock_state;
  bool initialized;
  bool rx_seen;
  bool transfer_in_progress;
  uint32_t transaction_count;
} i2c_slave_state_t;

static i2c_slave_state_t s_slave[2];

enum { SLAVE_LOCK_UNINIT = 0u, SLAVE_LOCK_INITING = 1u, SLAVE_LOCK_READY = 2u };

static inline uint8_t slave_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_i2c_slave: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline i2c_inst_t *slave_i2c_hw(uint8_t bus) {
  return slave_bus_index(bus) == 1u ? i2c1 : i2c0;
}

static inline i2c_slave_state_t *slave_state(uint8_t bus) {
  return &s_slave[slave_bus_index(bus)];
}

static inline uint slave_irq_num(uint8_t bus) {
  return I2C0_IRQ + slave_bus_index(bus);
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

static void slave_finish_transfer(i2c_slave_state_t *st) {
  st->rx_seen = false;
  if (st->transfer_in_progress) {
    __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
    st->transfer_in_progress = false;
  }
}

static void slave_handle_receive(i2c_slave_state_t *st, i2c_inst_t *i2c) {
  st->transfer_in_progress = true;

  while (i2c_get_read_available(i2c) > 0u) {
    uint8_t value = i2c_read_byte_raw(i2c);
    slave_lock(st);
    if (!st->rx_seen) {
      st->reg_ptr = value;
      st->rx_seen = true;
    } else if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
      st->regs[st->reg_ptr++] = value;
    }
    slave_unlock(st);
  }
}

static void slave_handle_request(i2c_slave_state_t *st, i2c_inst_t *i2c) {
  st->transfer_in_progress = true;

  uint8_t value = 0u;
  slave_lock(st);
  if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
    value = st->regs[st->reg_ptr++];
  }
  slave_unlock(st);
  i2c_write_byte_raw(i2c, value);
}

static void slave_handle_irq(uint8_t idx) {
  i2c_slave_state_t *st = &s_slave[idx];
  if (!st->initialized) {
    return;
  }

  i2c_inst_t *i2c = slave_i2c_hw(idx);
  i2c_hw_t *hw = i2c_get_hw(i2c);
  uint32_t intr_stat = hw->intr_stat;
  if (intr_stat == 0u) {
    return;
  }

  bool finish_before_data = false;
  bool finish_after_data = false;
  if ((intr_stat & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) != 0u) {
    (void)hw->clr_tx_abrt;
    finish_before_data = true;
  }
  if ((intr_stat & I2C_IC_INTR_STAT_R_START_DET_BITS) != 0u) {
    (void)hw->clr_start_det;
    finish_before_data = true;
  }
  if ((intr_stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) != 0u) {
    (void)hw->clr_stop_det;
    finish_after_data = true;
  }
  if (finish_before_data) {
    slave_finish_transfer(st);
  }

  if ((intr_stat & I2C_IC_INTR_STAT_R_RX_FULL_BITS) != 0u) {
    slave_handle_receive(st, i2c);
  }
  if ((intr_stat & I2C_IC_INTR_STAT_R_RD_REQ_BITS) != 0u) {
    (void)hw->clr_rd_req;
    slave_handle_request(st, i2c);
  }
  if (finish_after_data) {
    slave_finish_transfer(st);
  }
}

static void slave_irq_handler_0(void) { slave_handle_irq(0u); }

static void slave_irq_handler_1(void) { slave_handle_irq(1u); }

static irq_handler_t slave_irq_handler_for(uint8_t idx) {
  return idx == 1u ? slave_irq_handler_1 : slave_irq_handler_0;
}

static void slave_gpio_config(uint8_t sda_pin, uint8_t scl_pin) {
  gpio_set_function(sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(sda_pin);
  gpio_pull_up(scl_pin);
}

static void slave_hw_init(uint8_t idx) {
  i2c_slave_state_t *st = &s_slave[idx];
  i2c_inst_t *i2c = slave_i2c_hw(idx);

  slave_gpio_config(st->sda_pin, st->scl_pin);
  (void)i2c_init(i2c, RP2040_I2C_SLAVE_INIT_CLOCK_HZ);
  i2c_set_slave_mode(i2c, true, st->address);

  i2c_hw_t *hw = i2c_get_hw(i2c);
  (void)hw->clr_intr;
  hw->intr_mask =
      I2C_IC_INTR_MASK_M_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS |
      I2C_IC_INTR_MASK_M_TX_ABRT_BITS | I2C_IC_INTR_MASK_M_STOP_DET_BITS |
      I2C_IC_INTR_MASK_M_START_DET_BITS;

  uint irqn = slave_irq_num(idx);
  irq_set_exclusive_handler(irqn, slave_irq_handler_for(idx));
  irq_set_enabled(irqn, true);
}

static void slave_hw_deinit(uint8_t idx) {
  i2c_inst_t *i2c = slave_i2c_hw(idx);
  uint irqn = slave_irq_num(idx);

  irq_set_enabled(irqn, false);
  irq_remove_handler(irqn, slave_irq_handler_for(idx));

  i2c_hw_t *hw = i2c_get_hw(i2c);
  hw->intr_mask = I2C_IC_INTR_MASK_RESET;
  (void)hw->clr_intr;
  i2c_set_slave_mode(i2c, false, 0u);
  i2c_deinit(i2c);
}

void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
  hal_i2c_slave_init_bus(0, sda_pin, scl_pin, address);
}

void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address) {
  uint8_t idx = slave_bus_index(bus);
  slave_ensure_lock(idx);

  i2c_slave_state_t *st = &s_slave[idx];
  if (st->initialized) {
    slave_hw_deinit(idx);
  }

  slave_lock(st);
  memset(st->regs, 0, sizeof(st->regs));
  st->reg_ptr = 0u;
  st->address = address;
  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->rx_seen = false;
  st->transfer_in_progress = false;
  st->initialized = true;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
  slave_unlock(st);

  slave_hw_init(idx);
}

void hal_i2c_slave_deinit(void) { hal_i2c_slave_deinit_bus(0); }

void hal_i2c_slave_deinit_bus(uint8_t bus) {
  uint8_t idx = slave_bus_index(bus);
  slave_ensure_lock(idx);
  i2c_slave_state_t *st = &s_slave[idx];

  if (st->initialized) {
    slave_hw_deinit(idx);
  }

  slave_lock(st);
  st->initialized = false;
  st->address = 0u;
  st->reg_ptr = 0u;
  st->rx_seen = false;
  st->transfer_in_progress = false;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
  memset(st->regs, 0, sizeof(st->regs));
  slave_unlock(st);
}

void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value) {
  hal_i2c_slave_reg_write8_bus(0, reg, value);
}

void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value) {
  uint8_t idx = slave_bus_index(bus);
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return;
  }
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
  if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return;
  }
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
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return 0u;
  }
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
  if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return 0u;
  }
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
