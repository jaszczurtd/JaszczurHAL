#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_I2C_SLAVE

#include "hal/i2c/hal_i2c_slave.h"
#include "hal/system/hal_sync.h"

#include <stddef.h>
#include <string.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_i2c_pins.h"
#include "port/stm32g474_regs.h"
#endif

typedef struct {
  uint8_t regs[HAL_I2C_SLAVE_REG_MAP_SIZE];
  uint8_t reg_ptr;
  uint8_t address;
  bool initialized;
  bool rx_seen;
  uint32_t transaction_count;
#ifdef JH_STM32G474_HW
  uint32_t hw_base;
  uint32_t hw_rcc_mask;
#endif
} i2c_slave_state_t;

static i2c_slave_state_t s_slave[2];

static inline uint8_t slave_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_i2c_slave: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline i2c_slave_state_t *slave_state(uint8_t bus) {
  return &s_slave[slave_bus_index(bus)];
}

static void slave_state_reset(i2c_slave_state_t *st, uint8_t address) {
  memset(st->regs, 0, sizeof(st->regs));
  st->reg_ptr = 0u;
  st->address = address;
  st->rx_seen = false;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
}

static inline void slave_lock(void) { hal_critical_section_enter(); }

static inline void slave_unlock(void) { hal_critical_section_exit(); }

#ifdef JH_STM32G474_HW

#define JH_STM32_PIN(port, pin) ((uint8_t)(((port) * 16u) + (pin)))

enum {
  I2C_CTRL_1 = 1u,
  I2C_CTRL_2 = 2u,
};

typedef struct {
  uint8_t controller;
  uint32_t base;
  uint32_t rcc_mask;
  uint8_t ev_irqn;
  uint8_t er_irqn;
  uint8_t default_sda_pin;
  uint8_t default_scl_pin;
} i2c_slave_hw_desc_t;

static const i2c_slave_hw_desc_t k_i2c_slave_hw_desc[2] = {
    {I2C_CTRL_1, I2C1_BASE, RCC_APB1ENR1_I2C1EN, I2C1_EV_IRQn, I2C1_ER_IRQn,
     JH_STM32_PIN(1u, 9u), JH_STM32_PIN(1u, 8u)},
    {I2C_CTRL_2, I2C2_BASE, RCC_APB1ENR1_I2C2EN, I2C2_EV_IRQn, I2C2_ER_IRQn,
     JH_STM32_PIN(0u, 8u), JH_STM32_PIN(0u, 9u)},
};

static bool i2c_slave_pin_find_af(uint8_t controller, bool is_sda, uint8_t pin,
                                  uint8_t *out_af) {
  return jh_stm32g474_i2c_find_af(controller, is_sda, pin, out_af);
}

static void i2c_slave_gpio_set_af_od_pullup(uint8_t pin, uint8_t af) {
  jh_stm32g474_i2c_set_af_od_pullup(pin, af);
}

static void i2c_slave_nvic_enable(uint8_t irqn) {
  NVIC_ICPR(irqn >> 5u) = (1u << (irqn & 31u));
  NVIC_ISER(irqn >> 5u) = (1u << (irqn & 31u));
}

static bool i2c_slave_hw_configure(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin, uint8_t address) {
  const i2c_slave_hw_desc_t *desc = &k_i2c_slave_hw_desc[slave_bus_index(bus)];
  i2c_slave_state_t *st = slave_state(bus);

  if (sda_pin == 0u) {
    sda_pin = desc->default_sda_pin;
  }
  if (scl_pin == 0u) {
    scl_pin = desc->default_scl_pin;
  }

  uint8_t sda_af = 0u;
  uint8_t scl_af = 0u;
  if (!i2c_slave_pin_find_af(desc->controller, true, sda_pin, &sda_af) ||
      !i2c_slave_pin_find_af(desc->controller, false, scl_pin, &scl_af)) {
    st->hw_base = 0u;
    st->hw_rcc_mask = 0u;
    return false;
  }

  st->hw_base = desc->base;
  st->hw_rcc_mask = desc->rcc_mask;
  RCC_APB1ENR1 |= st->hw_rcc_mask;
  (void)RCC_APB1ENR1;

  i2c_slave_gpio_set_af_od_pullup(scl_pin, scl_af);
  i2c_slave_gpio_set_af_od_pullup(sda_pin, sda_af);

  I2C_CR1_REG(st->hw_base) &= ~I2C_CR1_PE;
  I2C_TIMINGR_REG(st->hw_base) = I2C_TIMINGR_100K_16MHZ;
  I2C_OAR1_REG(st->hw_base) = 0u;
  I2C_OAR1_REG(st->hw_base) =
      ((uint32_t)(address & 0x7Fu) << 1u) | I2C_OAR1_OA1EN;
  I2C_ICR_REG(st->hw_base) = I2C_ICR_ADDRCF | I2C_ICR_NACKCF | I2C_ICR_STOPCF |
                             I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
  I2C_CR1_REG(st->hw_base) = I2C_CR1_PE | I2C_CR1_TXIE | I2C_CR1_RXIE |
                             I2C_CR1_ADDRIE | I2C_CR1_NACKIE | I2C_CR1_STOPIE |
                             I2C_CR1_ERRIE;

  i2c_slave_nvic_enable(desc->ev_irqn);
  i2c_slave_nvic_enable(desc->er_irqn);
  return true;
}

static void i2c_slave_hw_deinit(i2c_slave_state_t *st) {
  if (st->hw_base != 0u) {
    I2C_CR1_REG(st->hw_base) &= ~I2C_CR1_PE;
    I2C_OAR1_REG(st->hw_base) = 0u;
    I2C_ICR_REG(st->hw_base) = I2C_ICR_ADDRCF | I2C_ICR_NACKCF |
                               I2C_ICR_STOPCF | I2C_ICR_BERRCF |
                               I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
  }
  st->hw_base = 0u;
  st->hw_rcc_mask = 0u;
}

static void i2c_slave_handle_addr(i2c_slave_state_t *st, uint32_t isr) {
  (void)isr;
  st->rx_seen = false;
  I2C_ICR_REG(st->hw_base) = I2C_ICR_ADDRCF;
}

static void i2c_slave_handle_rx(i2c_slave_state_t *st) {
  const uint8_t value = (uint8_t)I2C_RXDR_REG(st->hw_base);
  if (!st->rx_seen) {
    st->reg_ptr = value;
    st->rx_seen = true;
    return;
  }
  if (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) {
    st->regs[st->reg_ptr++] = value;
  }
}

static void i2c_slave_handle_tx(i2c_slave_state_t *st) {
  I2C_TXDR_REG(st->hw_base) =
      (st->reg_ptr < HAL_I2C_SLAVE_REG_MAP_SIZE) ? st->regs[st->reg_ptr++] : 0u;
}

static void i2c_slave_flush_txdr(i2c_slave_state_t *st) {
  I2C_ISR_REG(st->hw_base) = I2C_ISR_TXE;
}

static void i2c_slave_handle_event(uint8_t bus) {
  i2c_slave_state_t *st = slave_state(bus);
  if (!st->initialized || st->hw_base == 0u) {
    return;
  }

  uint32_t isr = I2C_ISR_REG(st->hw_base);
  if ((isr & I2C_ISR_ADDR) != 0u) {
    i2c_slave_handle_addr(st, isr);
    isr = I2C_ISR_REG(st->hw_base);
  }
  if ((isr & I2C_ISR_RXNE) != 0u) {
    i2c_slave_handle_rx(st);
  }
  if ((isr & I2C_ISR_TXIS) != 0u) {
    i2c_slave_handle_tx(st);
  }
  if ((isr & I2C_ISR_NACKF) != 0u) {
    i2c_slave_flush_txdr(st);
    I2C_ICR_REG(st->hw_base) = I2C_ICR_NACKCF;
  }
  if ((isr & I2C_ISR_STOPF) != 0u) {
    i2c_slave_flush_txdr(st);
    st->rx_seen = false;
    __atomic_fetch_add(&st->transaction_count, 1u, __ATOMIC_RELAXED);
    I2C_ICR_REG(st->hw_base) = I2C_ICR_STOPCF;
  }
}

static void i2c_slave_handle_error(uint8_t bus) {
  i2c_slave_state_t *st = slave_state(bus);
  if (st->hw_base == 0u) {
    return;
  }
  I2C_ICR_REG(st->hw_base) =
      I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF | I2C_ICR_NACKCF;
}

extern "C" void I2C1_EV_IRQHandler(void) { i2c_slave_handle_event(0u); }
extern "C" void I2C1_ER_IRQHandler(void) { i2c_slave_handle_error(0u); }
extern "C" void I2C2_EV_IRQHandler(void) { i2c_slave_handle_event(1u); }
extern "C" void I2C2_ER_IRQHandler(void) { i2c_slave_handle_error(1u); }

#endif /* JH_STM32G474_HW */

void hal_i2c_slave_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t address) {
  hal_i2c_slave_init_bus(0, sda_pin, scl_pin, address);
}

void hal_i2c_slave_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                            uint8_t address) {
  uint8_t idx = slave_bus_index(bus);
  i2c_slave_state_t *st = &s_slave[idx];

  slave_lock();
  slave_state_reset(st, address);
  st->initialized = true;
  slave_unlock();

#ifdef JH_STM32G474_HW
  if (!i2c_slave_hw_configure(idx, sda_pin, scl_pin, address)) {
    st->initialized = false;
    HAL_ASSERT(false, "hal_i2c_slave_init_bus: invalid SDA/SCL pin mapping for "
                      "selected bus");
  }
#else
  (void)sda_pin;
  (void)scl_pin;
#endif
}

void hal_i2c_slave_deinit(void) { hal_i2c_slave_deinit_bus(0); }

void hal_i2c_slave_deinit_bus(uint8_t bus) {
  i2c_slave_state_t *st = slave_state(bus);
#ifdef JH_STM32G474_HW
  i2c_slave_hw_deinit(st);
#endif
  slave_lock();
  st->initialized = false;
  st->address = 0u;
  st->reg_ptr = 0u;
  st->rx_seen = false;
  __atomic_store_n(&st->transaction_count, 0u, __ATOMIC_RELEASE);
  memset(st->regs, 0, sizeof(st->regs));
  slave_unlock();
}

void hal_i2c_slave_reg_write8(uint8_t reg, uint8_t value) {
  hal_i2c_slave_reg_write8_bus(0, reg, value);
}

void hal_i2c_slave_reg_write8_bus(uint8_t bus, uint8_t reg, uint8_t value) {
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return;
  }
  i2c_slave_state_t *st = slave_state(bus);
  slave_lock();
  st->regs[reg] = value;
  slave_unlock();
}

void hal_i2c_slave_reg_write16(uint8_t reg, uint16_t value) {
  hal_i2c_slave_reg_write16_bus(0, reg, value);
}

void hal_i2c_slave_reg_write16_bus(uint8_t bus, uint8_t reg, uint16_t value) {
  if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return;
  }
  i2c_slave_state_t *st = slave_state(bus);
  slave_lock();
  st->regs[reg] = (uint8_t)(value >> 8);
  st->regs[reg + 1] = (uint8_t)(value & 0xFF);
  slave_unlock();
}

uint8_t hal_i2c_slave_reg_read8(uint8_t reg) {
  return hal_i2c_slave_reg_read8_bus(0, reg);
}

uint8_t hal_i2c_slave_reg_read8_bus(uint8_t bus, uint8_t reg) {
  if (reg >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return 0u;
  }
  i2c_slave_state_t *st = slave_state(bus);
  slave_lock();
  uint8_t value = st->regs[reg];
  slave_unlock();
  return value;
}

uint16_t hal_i2c_slave_reg_read16(uint8_t reg) {
  return hal_i2c_slave_reg_read16_bus(0, reg);
}

uint16_t hal_i2c_slave_reg_read16_bus(uint8_t bus, uint8_t reg) {
  if ((uint16_t)reg + 1U >= HAL_I2C_SLAVE_REG_MAP_SIZE) {
    return 0u;
  }
  i2c_slave_state_t *st = slave_state(bus);
  slave_lock();
  uint16_t value = ((uint16_t)st->regs[reg] << 8) | (uint16_t)st->regs[reg + 1];
  slave_unlock();
  return value;
}

uint8_t hal_i2c_slave_get_address(void) {
  return hal_i2c_slave_get_address_bus(0);
}

uint8_t hal_i2c_slave_get_address_bus(uint8_t bus) {
  return slave_state(bus)->address;
}

uint32_t hal_i2c_slave_get_transaction_count(void) {
  return hal_i2c_slave_get_transaction_count_bus(0);
}

uint32_t hal_i2c_slave_get_transaction_count_bus(uint8_t bus) {
  return __atomic_load_n(&slave_state(bus)->transaction_count,
                         __ATOMIC_ACQUIRE);
}

#endif /* HAL_ENABLE_I2C_SLAVE */
#endif // HAL_TARGET_IS_STM32G474
