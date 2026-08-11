#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_I2C

#include "hal/core/hal_mutex_once.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/i2c/hal_i2c_internal.h"
#include "hal/system/hal_sync.h"

#include <string.h>

#if defined(HAL_ENABLE_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif

#ifdef JH_STM32G474_HW
#include "port/stm32g474_i2c_pins.h"
#include "port/stm32g474_regs.h"
#endif

#define STM32_I2C_BUF_SIZE 255

typedef struct {
  uint8_t rx_buf[STM32_I2C_BUF_SIZE];
  int rx_len;
  int rx_pos;
  uint8_t tx_buf[STM32_I2C_BUF_SIZE];
  int tx_len;
  uint8_t cur_addr;
  uint8_t last_error; /* result of the last end_transmission */
  bool initialized;
  uint32_t clock_hz;
  uint32_t transaction_count;
  uint32_t bus_clear_count;
  uint8_t sda_pin;
  uint8_t scl_pin;
  hal_mutex_t mutex;
  uintptr_t lock_owner;
  uint32_t lock_depth;
#ifdef JH_STM32G474_HW
  uint32_t hw_base;
  uint32_t hw_rcc_mask;
#endif
} i2c_bus_state_t;

static i2c_bus_state_t s_i2c[2] = {};

static inline bool i2c_bus_valid(uint8_t bus) { return bus <= 1u; }

static inline uint8_t i2c_bus_index(uint8_t bus) {
  HAL_ASSERT(bus <= 1u, "hal_i2c: invalid bus index");
  return (bus <= 1u) ? bus : 0u;
}

static inline i2c_bus_state_t *i2c_state(uint8_t bus) {
  return &s_i2c[i2c_bus_index(bus)];
}

bool jh_hal_i2c_bus_is_initialized(uint8_t bus) {
  return i2c_bus_valid(bus) && i2c_state(bus)->initialized;
}

static void i2c_ensure_mutex(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  (void)jh_hal_mutex_create_once(&st->mutex);
}

static uintptr_t i2c_current_owner_token(void) {
#if defined(HAL_ENABLE_FREERTOS)
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  return (task != NULL) ? (uintptr_t)task : 1u;
#else
  return 1u;
#endif
}

static void i2c_lock_state(i2c_bus_state_t *st) {
  const uintptr_t owner = i2c_current_owner_token();
  if ((st->lock_depth > 0u) && (st->lock_owner == owner)) {
    st->lock_depth++;
    return;
  }

  hal_mutex_lock(st->mutex);
  st->lock_owner = owner;
  st->lock_depth = 1u;
}

static void i2c_unlock_state(i2c_bus_state_t *st) {
  const uintptr_t owner = i2c_current_owner_token();
  HAL_ASSERT((st->lock_depth > 0u) && (st->lock_owner == owner),
             "hal_i2c_unlock: bus is not locked by this context");
  if ((st->lock_depth == 0u) || (st->lock_owner != owner)) {
    return;
  }

  st->lock_depth--;
  if (st->lock_depth == 0u) {
    st->lock_owner = 0u;
    hal_mutex_unlock(st->mutex);
  }
}

static void i2c_lock_bus(uint8_t bus) {
  i2c_ensure_mutex(bus);
  i2c_lock_state(i2c_state(bus));
}

static void i2c_unlock_bus(uint8_t bus) {
  i2c_ensure_mutex(bus);
  i2c_unlock_state(i2c_state(bus));
}

static hal_status_t i2c_status_from_result(uint8_t result) {
  switch (result) {
  case HAL_I2C_RESULT_OK:
    return HAL_OK;
  case HAL_I2C_ERROR_TIMEOUT:
    return HAL_ETIMEOUT;
  case HAL_I2C_ERROR_GENERIC:
    return HAL_EBUS;
  case HAL_I2C_ERROR_OTHER:
  default:
    return HAL_EIO;
  }
}

#ifdef JH_STM32G474_HW
#define I2C_TIMEOUT 200000u

/* Port-indexed pin id helper: pin = port*16 + pin_number. */
#define JH_STM32_PIN(port, pin) ((uint8_t)(((port) * 16u) + (pin)))

enum {
  I2C_CTRL_1 = 1u,
  I2C_CTRL_2 = 2u,
};

typedef struct {
  uint8_t controller;
  uint32_t base;
  uint32_t rcc_mask;
  uint8_t default_sda_pin;
  uint8_t default_scl_pin;
} i2c_hw_desc_t;

static const i2c_hw_desc_t k_i2c_hw_desc[2] = {
    {/* bus 0 */
     I2C_CTRL_1, I2C1_BASE, RCC_APB1ENR1_I2C1EN, JH_STM32_PIN(1u, 9u),
     JH_STM32_PIN(1u, 8u)},
    {/* bus 1 */
     I2C_CTRL_2, I2C2_BASE, RCC_APB1ENR1_I2C2EN, JH_STM32_PIN(0u, 8u),
     JH_STM32_PIN(0u, 9u)},
};

static inline const i2c_hw_desc_t *i2c_hw_desc_for_bus(uint8_t bus) {
  return &k_i2c_hw_desc[i2c_bus_index(bus)];
}

static inline uint32_t i2c_pin_port(uint8_t pin) {
  return jh_stm32g474_pin_port(pin);
}

static inline uint32_t i2c_pin_num(uint8_t pin) {
  return jh_stm32g474_pin_number(pin);
}

static inline bool i2c_pin_valid(uint8_t pin) {
  return i2c_pin_port(pin) <= 6u;
}

static bool i2c_pin_find_af(uint8_t controller, bool is_sda, uint8_t pin,
                            uint8_t *out_af) {
  return jh_stm32g474_i2c_find_af(controller, is_sda, pin, out_af);
}

static void i2c_gpio_enable_clock(uint32_t port) {
  if (port <= 6u) {
    RCC_AHB2ENR |= (1u << port);
    (void)RCC_AHB2ENR;
  }
}

static void i2c_gpio_set_af_od_pullup(uint8_t pin, uint8_t af) {
  jh_stm32g474_i2c_set_af_od_pullup(pin, af);
}

static void i2c_gpio_set_input_pullup(uint8_t pin) {
  const uint32_t port = i2c_pin_port(pin);
  const uint32_t n = i2c_pin_num(pin);
  if (port > 6u) {
    return;
  }

  i2c_gpio_enable_clock(port);
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_INPUT << (n * 2u));
  GPIO_OTYPER(port) |= (1u << n);
  GPIO_PUPDR(port) =
      (GPIO_PUPDR(port) & ~(0x3u << (n * 2u))) | (GPIO_PUPD_UP << (n * 2u));
}

static void i2c_gpio_set_output_od_pullup(uint8_t pin) {
  const uint32_t port = i2c_pin_port(pin);
  const uint32_t n = i2c_pin_num(pin);
  if (port > 6u) {
    return;
  }

  i2c_gpio_enable_clock(port);
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_OUTPUT << (n * 2u));
  GPIO_OTYPER(port) |= (1u << n);
  GPIO_OSPEEDR(port) |= (0x3u << (n * 2u));
  GPIO_PUPDR(port) =
      (GPIO_PUPDR(port) & ~(0x3u << (n * 2u))) | (GPIO_PUPD_UP << (n * 2u));
}

static void i2c_gpio_write(uint8_t pin, bool high) {
  const uint32_t port = i2c_pin_port(pin);
  const uint32_t n = i2c_pin_num(pin);
  if (port > 6u) {
    return;
  }
  GPIO_BSRR(port) = high ? (1u << n) : (1u << (n + 16u));
}

static bool i2c_gpio_read(uint8_t pin) {
  const uint32_t port = i2c_pin_port(pin);
  const uint32_t n = i2c_pin_num(pin);
  if (port > 6u) {
    return false;
  }
  return (GPIO_IDR(port) & (1u << n)) != 0u;
}

static void i2c_bus_clear_delay(void) {
  for (volatile uint32_t i = 0u; i < 80u; ++i) {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("nop");
#endif
  }
}

static uint32_t i2c_timing_from_clock(uint32_t clock_hz) {
  if (clock_hz >= HAL_I2C_CLOCK_FAST_PLUS_HZ) {
    return I2C_TIMINGR_1M_16MHZ;
  }
  if (clock_hz >= HAL_I2C_CLOCK_FAST_HZ) {
    return I2C_TIMINGR_400K_16MHZ;
  }
  return I2C_TIMINGR_100K_16MHZ;
}

static void i2c_hw_apply_clock(uint32_t base, uint32_t clock_hz) {
  I2C_CR1_REG(base) &= ~I2C_CR1_PE;
  I2C_TIMINGR_REG(base) = i2c_timing_from_clock(clock_hz);
  I2C_CR1_REG(base) |= I2C_CR1_PE;
}

static bool i2c_hw_configure_bus(i2c_bus_state_t *st, const i2c_hw_desc_t *desc,
                                 uint8_t requested_sda_pin,
                                 uint8_t requested_scl_pin) {
  uint8_t sda_pin = requested_sda_pin;
  uint8_t scl_pin = requested_scl_pin;

  if (sda_pin == 0u) {
    sda_pin = desc->default_sda_pin;
  }
  if (scl_pin == 0u) {
    scl_pin = desc->default_scl_pin;
  }

  uint8_t sda_af = 0u;
  uint8_t scl_af = 0u;
  if (!i2c_pin_find_af(desc->controller, true, sda_pin, &sda_af) ||
      !i2c_pin_find_af(desc->controller, false, scl_pin, &scl_af)) {
    st->hw_base = 0u;
    st->hw_rcc_mask = 0u;
    return false;
  }

  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->hw_base = desc->base;
  st->hw_rcc_mask = desc->rcc_mask;

  RCC_APB1ENR1 |= st->hw_rcc_mask;
  (void)RCC_APB1ENR1;

  i2c_gpio_set_af_od_pullup(st->scl_pin, scl_af);
  i2c_gpio_set_af_od_pullup(st->sda_pin, sda_af);
  i2c_hw_apply_clock(st->hw_base, st->clock_hz);
  return true;
}

static inline bool i2c_hw_ready(const i2c_bus_state_t *st) {
  return st->initialized && st->hw_base != 0u;
}

/* Master write of @p len bytes (AUTOEND). Returns HAL_I2C_* status. */
static uint8_t i2c_hw_write(uint32_t base, uint8_t addr, const uint8_t *buf,
                            int len) {
  I2C_ICR_REG(base) = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
  I2C_CR2_REG(base) = ((uint32_t)addr << 1) | ((uint32_t)(uint8_t)len << 16) |
                      I2C_CR2_AUTOEND | I2C_CR2_START;
  for (int i = 0; i < len; ++i) {
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C_ISR_REG(base) & (I2C_ISR_TXIS | I2C_ISR_NACKF)) && to) {
      --to;
    }
    if (to == 0u) {
      return HAL_I2C_ERROR_TIMEOUT;
    }
    if (I2C_ISR_REG(base) & I2C_ISR_NACKF) {
      break;
    }
    I2C_TXDR_REG(base) = buf[i];
  }
  uint32_t to = I2C_TIMEOUT;
  while (!(I2C_ISR_REG(base) & I2C_ISR_STOPF) && to) {
    --to;
  }
  if (to == 0u) {
    return HAL_I2C_ERROR_TIMEOUT;
  }
  const bool nack = (I2C_ISR_REG(base) & I2C_ISR_NACKF) != 0u;
  I2C_ICR_REG(base) = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  return nack ? HAL_I2C_ERROR_GENERIC : HAL_I2C_RESULT_OK;
}

/* Master read of @p len bytes (AUTOEND). Returns number of bytes received. */
static int i2c_hw_read(uint32_t base, uint8_t addr, uint8_t *buf, int len) {
  if (len <= 0) {
    return 0;
  }
  I2C_ICR_REG(base) = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
  I2C_CR2_REG(base) = ((uint32_t)addr << 1) | ((uint32_t)(uint8_t)len << 16) |
                      I2C_CR2_RD_WRN | I2C_CR2_AUTOEND | I2C_CR2_START;
  int got = 0;
  for (int i = 0; i < len; ++i) {
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C_ISR_REG(base) & (I2C_ISR_RXNE | I2C_ISR_NACKF)) && to) {
      --to;
    }
    if (to == 0u || (I2C_ISR_REG(base) & I2C_ISR_NACKF)) {
      break;
    }
    buf[i] = (uint8_t)I2C_RXDR_REG(base);
    ++got;
  }
  uint32_t to = I2C_TIMEOUT;
  while (!(I2C_ISR_REG(base) & I2C_ISR_STOPF) && to) {
    --to;
  }
  I2C_ICR_REG(base) = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  return got;
}

static bool i2c_hw_write_read(uint32_t base, uint8_t addr, const uint8_t *tx,
                              int tx_len, uint8_t *rx, int rx_len) {
  if (tx_len <= 0 || rx_len < 0) {
    return false;
  }
  I2C_ICR_REG(base) = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
  I2C_CR2_REG(base) =
      ((uint32_t)addr << 1) | ((uint32_t)(uint8_t)tx_len << 16) | I2C_CR2_START;
  for (int i = 0; i < tx_len; ++i) {
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C_ISR_REG(base) & (I2C_ISR_TXIS | I2C_ISR_NACKF)) && to) {
      --to;
    }
    if (to == 0u || (I2C_ISR_REG(base) & I2C_ISR_NACKF)) {
      I2C_ICR_REG(base) = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
      return false;
    }
    I2C_TXDR_REG(base) = tx[i];
  }

  uint32_t to = I2C_TIMEOUT;
  while (!(I2C_ISR_REG(base) & (I2C_ISR_TC | I2C_ISR_NACKF)) && to) {
    --to;
  }
  if (to == 0u || (I2C_ISR_REG(base) & I2C_ISR_NACKF)) {
    I2C_ICR_REG(base) = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    return false;
  }

  if (rx_len == 0) {
    I2C_CR2_REG(base) |= I2C_CR2_STOP;
    to = I2C_TIMEOUT;
    while (!(I2C_ISR_REG(base) & I2C_ISR_STOPF) && to) {
      --to;
    }
    I2C_ICR_REG(base) = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    return to != 0u;
  }

  I2C_CR2_REG(base) = ((uint32_t)addr << 1) |
                      ((uint32_t)(uint8_t)rx_len << 16) | I2C_CR2_RD_WRN |
                      I2C_CR2_AUTOEND | I2C_CR2_START;
  int got = 0;
  for (int i = 0; i < rx_len; ++i) {
    to = I2C_TIMEOUT;
    while (!(I2C_ISR_REG(base) & (I2C_ISR_RXNE | I2C_ISR_NACKF)) && to) {
      --to;
    }
    if (to == 0u || (I2C_ISR_REG(base) & I2C_ISR_NACKF)) {
      break;
    }
    rx[i] = (uint8_t)I2C_RXDR_REG(base);
    ++got;
  }

  to = I2C_TIMEOUT;
  while (!(I2C_ISR_REG(base) & I2C_ISR_STOPF) && to) {
    --to;
  }
  I2C_ICR_REG(base) = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  return (to != 0u) && (got == rx_len);
}

/* Zero-byte probe: returns true if the device ACKs (is present). */
static bool i2c_hw_ack(uint32_t base, uint8_t addr) {
  I2C_ICR_REG(base) = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
  I2C_CR2_REG(base) = ((uint32_t)addr << 1) | I2C_CR2_AUTOEND | I2C_CR2_START;
  uint32_t to = I2C_TIMEOUT;
  while (!(I2C_ISR_REG(base) & I2C_ISR_STOPF) && to) {
    --to;
  }
  const bool nack = (I2C_ISR_REG(base) & I2C_ISR_NACKF) != 0u;
  I2C_ICR_REG(base) = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
  return (to != 0u) && !nack;
}
#endif /* JH_STM32G474_HW */

hal_status_t hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t clock_hz) {
  return hal_i2c_init_bus(0, sda_pin, scl_pin, clock_hz);
}

hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda_pin, uint8_t scl_pin,
                              uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  i2c_ensure_mutex(bus);

  i2c_bus_state_t *st = i2c_state(bus);
  st->rx_len = 0;
  st->rx_pos = 0;
  st->tx_len = 0;
  st->cur_addr = 0u;
  st->last_error = 0u;
  st->clock_hz = (clock_hz == 0u) ? HAL_I2C_CLOCK_STANDARD_HZ : clock_hz;
  st->transaction_count = 0u;
  st->bus_clear_count = 0u;
  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  if (st->lock_depth == 0u) {
    st->lock_owner = 0u;
  }
#ifdef JH_STM32G474_HW
  st->hw_base = 0u;
  st->hw_rcc_mask = 0u;

  const i2c_hw_desc_t *desc = i2c_hw_desc_for_bus(bus);
  if (!i2c_hw_configure_bus(st, desc, sda_pin, scl_pin)) {
    st->initialized = false;
    return HAL_ECONFIG;
  }
#endif
  st->initialized = true;
  return HAL_OK;
}

hal_status_t hal_i2c_set_clock(uint32_t clock_hz) {
  return hal_i2c_set_clock_bus(0, clock_hz);
}

hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t clock_hz) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  i2c_ensure_mutex(bus);
  i2c_bus_state_t *st = i2c_state(bus);
  i2c_lock_state(st);
  st->clock_hz = (clock_hz == 0u) ? HAL_I2C_CLOCK_STANDARD_HZ : clock_hz;
#ifdef JH_STM32G474_HW
  if (i2c_hw_ready(st)) {
    i2c_hw_apply_clock(st->hw_base, st->clock_hz);
  }
#endif
  i2c_unlock_state(st);
  return HAL_OK;
}

void hal_i2c_deinit(void) { hal_i2c_deinit_bus(0); }

void hal_i2c_deinit_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  st->initialized = false;
  st->rx_len = 0;
  st->rx_pos = 0;
  st->tx_len = 0;
  st->cur_addr = 0u;
#ifdef JH_STM32G474_HW
  if (st->hw_base != 0u) {
    I2C_CR1_REG(st->hw_base) &= ~I2C_CR1_PE;
  }
  st->hw_base = 0u;
  st->hw_rcc_mask = 0u;
#endif
}

void hal_i2c_lock(void) { hal_i2c_lock_bus(0); }

void hal_i2c_lock_bus(uint8_t bus) { i2c_lock_bus(bus); }

void hal_i2c_unlock(void) { hal_i2c_unlock_bus(0); }

void hal_i2c_unlock_bus(uint8_t bus) { i2c_unlock_bus(bus); }

void hal_i2c_begin_transmission(uint8_t address) {
  hal_i2c_begin_transmission_bus(0, address);
}

void hal_i2c_begin_transmission_bus(uint8_t bus, uint8_t address) {
  i2c_lock_bus(bus);
  i2c_state(bus)->cur_addr = address;
  i2c_state(bus)->tx_len = 0;
}

size_t hal_i2c_write(uint8_t data) { return hal_i2c_write_bus(0, data); }

size_t hal_i2c_write_bus(uint8_t bus, uint8_t data) {
  i2c_bus_state_t *st = i2c_state(bus);
  if (st->tx_len >= STM32_I2C_BUF_SIZE) {
    return 0u;
  }
  st->tx_buf[st->tx_len++] = data;
  return 1u;
}

hal_status_t hal_i2c_end_transmission_ex(void) {
  return hal_i2c_end_transmission_bus_ex(0);
}

hal_status_t hal_i2c_end_transmission_bus_ex(uint8_t bus) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  i2c_bus_state_t *st = i2c_state(bus);
  uint8_t err = HAL_I2C_RESULT_OK;
#ifdef JH_STM32G474_HW
  if (i2c_hw_ready(st)) {
    err = i2c_hw_write(st->hw_base, st->cur_addr, st->tx_buf, st->tx_len);
  } else {
    err = HAL_I2C_ERROR_TIMEOUT;
  }
#endif
  st->last_error = err;
  st->tx_len = 0;
  st->transaction_count++;
  i2c_unlock_bus(bus);
  return i2c_status_from_result(err);
}

static bool i2c_write_read_bus_impl(uint8_t bus, uint8_t address,
                                    const uint8_t *tx, size_t tx_len,
                                    uint8_t *rx, size_t rx_len);

hal_status_t hal_i2c_write_read_bus_ex(uint8_t bus, uint8_t address,
                                       const uint8_t *tx, size_t tx_len,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (tx_len > 0u && tx == NULL) ||
      (rx_len > 0u && rx == NULL) || tx_len > STM32_I2C_BUF_SIZE ||
      rx_len > STM32_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  if (tx_len == 0u) {
    return hal_i2c_read_bytes_bus_ex(bus, address, rx, rx_len);
  }
  if (!i2c_state(bus)->initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  return hal_status_from_bool(
      i2c_write_read_bus_impl(bus, address, tx, tx_len, rx, rx_len), HAL_EBUS);
}

static bool i2c_write_read_bus_impl(uint8_t bus, uint8_t address,
                                    const uint8_t *tx, size_t tx_len,
                                    uint8_t *rx, size_t rx_len) {
  if ((tx_len > 0u && tx == NULL) || (rx_len > 0u && rx == NULL) ||
      tx_len > 255u || rx_len > 255u) {
    return false;
  }

  i2c_bus_state_t *st = i2c_state(bus);
  i2c_lock_bus(bus);
  bool ok = true;
#ifdef JH_STM32G474_HW
  if (!i2c_hw_ready(st)) {
    i2c_unlock_bus(bus);
    return false;
  }

  ok =
      i2c_hw_write_read(st->hw_base, address, tx, (int)tx_len, rx, (int)rx_len);
  st->transaction_count += (rx_len > 0u) ? 2u : 1u;
  i2c_unlock_bus(bus);
  return ok;
#endif

  st->cur_addr = address;
  st->tx_len = 0;
  for (size_t i = 0; i < tx_len; ++i) {
    if (st->tx_len >= STM32_I2C_BUF_SIZE) {
      ok = false;
      break;
    }
    st->tx_buf[st->tx_len++] = tx[i];
  }
  st->tx_len = 0;
  st->transaction_count++;

  if (ok && rx_len > 0u) {
    st->rx_len = (int)rx_len;
    st->rx_pos = 0;
    for (size_t i = 0; i < rx_len; ++i) {
      int v = hal_i2c_read_bus(bus);
      if (v < 0) {
        ok = false;
        break;
      }
      rx[i] = (uint8_t)v;
    }
    st->transaction_count++;
  }

  i2c_unlock_bus(bus);
  return ok;
}

static bool i2c_read_bytes_bus_impl(uint8_t bus, uint8_t address, uint8_t *rx,
                                    size_t rx_len);

hal_status_t hal_i2c_read_bytes_bus_ex(uint8_t bus, uint8_t address,
                                       uint8_t *rx, size_t rx_len) {
  if (!i2c_bus_valid(bus) || (rx_len > 0u && rx == NULL) ||
      rx_len > STM32_I2C_BUF_SIZE) {
    return HAL_EINVAL;
  }
  if (rx_len == 0u) {
    return HAL_OK;
  }
  if (!i2c_state(bus)->initialized) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  return hal_status_from_bool(i2c_read_bytes_bus_impl(bus, address, rx, rx_len),
                              HAL_EBUS);
}

static bool i2c_read_bytes_bus_impl(uint8_t bus, uint8_t address, uint8_t *rx,
                                    size_t rx_len) {
  if ((rx_len > 0u && rx == NULL) || rx_len > 255u) {
    return false;
  }
  if (rx_len == 0u) {
    return true;
  }

  i2c_bus_state_t *st = i2c_state(bus);
  i2c_lock_bus(bus);
  bool ok = true;
#ifdef JH_STM32G474_HW
  if (!i2c_hw_ready(st)) {
    i2c_unlock_bus(bus);
    return false;
  }

  int got = i2c_hw_read(st->hw_base, address, rx, (int)rx_len);
  st->rx_len = 0;
  st->rx_pos = 0;
  st->transaction_count++;
  i2c_unlock_bus(bus);
  return got == (int)rx_len;
#else
  (void)address;
  memset(rx, 0, rx_len);
  st->rx_len = 0;
  st->rx_pos = 0;
  st->transaction_count++;
#endif
  i2c_unlock_bus(bus);
  return ok;
}

static uint8_t i2c_request_from_bus_impl(uint8_t bus, uint8_t address,
                                         uint8_t count);

hal_status_t hal_i2c_request_from_bus_ex(uint8_t bus, uint8_t address,
                                         uint8_t count, uint8_t *outReceived) {
  if (outReceived == NULL || !i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
  *outReceived = 0u;
  if (!i2c_state(bus)->initialized && count > 0u) {
    HAL_ASSERT(false, "hal_i2c: bus used before hal_i2c_init_bus");
    return HAL_EUNINIT;
  }
  *outReceived = i2c_request_from_bus_impl(bus, address, count);
  return (*outReceived == count) ? HAL_OK : HAL_EBUS;
}

static uint8_t i2c_request_from_bus_impl(uint8_t bus, uint8_t address,
                                         uint8_t count) {
  i2c_bus_state_t *st = i2c_state(bus);

  i2c_lock_bus(bus);
  /* count is uint8_t (<=255) and the rx buffer holds 255 bytes, so it always
   * fits. */
  int got = 0;
#ifdef JH_STM32G474_HW
  if (i2c_hw_ready(st)) {
    got = i2c_hw_read(st->hw_base, address, st->rx_buf, (int)count);
  } else {
    got = 0;
  }
#else
  (void)address;
  got = (int)count;
#endif
  st->rx_len = got;
  st->rx_pos = 0;
  st->transaction_count++;
  i2c_unlock_bus(bus);
  return (uint8_t)got;
}

int hal_i2c_available(void) { return hal_i2c_available_bus(0); }

int hal_i2c_available_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  return st->rx_len - st->rx_pos;
}

int hal_i2c_read(void) { return hal_i2c_read_bus(0); }

int hal_i2c_read_bus(uint8_t bus) {
  i2c_bus_state_t *st = i2c_state(bus);
  if (st->rx_pos < st->rx_len) {
    return st->rx_buf[st->rx_pos++];
  }
  return -1;
}

bool hal_i2c_is_busy(uint8_t address) {
  return hal_i2c_is_busy_bus(0, address);
}

bool hal_i2c_is_busy_bus(uint8_t bus, uint8_t address) {
#ifdef JH_STM32G474_HW
  i2c_bus_state_t *st = i2c_state(bus);
  if (i2c_hw_ready(st)) {
    i2c_lock_bus(bus);
    bool ack = i2c_hw_ack(st->hw_base, address);
    i2c_unlock_bus(bus);
    return !ack; /* busy/absent == did NOT ACK */
  }
  return true;
#endif
  (void)bus;
  (void)address;
  return false;
}

uint32_t hal_i2c_get_transaction_count(void) {
  return hal_i2c_get_transaction_count_bus(0);
}

uint32_t hal_i2c_get_transaction_count_bus(uint8_t bus) {
  return i2c_state(bus)->transaction_count;
}

hal_status_t hal_i2c_bus_clear(uint8_t sda_pin, uint8_t scl_pin) {
  return hal_i2c_bus_clear_bus(0, sda_pin, scl_pin);
}

static void i2c_bus_clear_bus_impl(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin);

#ifdef JH_STM32G474_HW
static bool i2c_resolve_clear_pins(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin, uint8_t *out_sda,
                                   uint8_t *out_scl) {
  const i2c_bus_state_t *st = i2c_state(bus);
  const i2c_hw_desc_t *desc = i2c_hw_desc_for_bus(bus);
  *out_sda = sda_pin != 0u
                 ? sda_pin
                 : (st->sda_pin != 0u ? st->sda_pin : desc->default_sda_pin);
  *out_scl = scl_pin != 0u
                 ? scl_pin
                 : (st->scl_pin != 0u ? st->scl_pin : desc->default_scl_pin);
  return i2c_pin_valid(*out_sda) && i2c_pin_valid(*out_scl);
}
#endif

hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin) {
  if (!i2c_bus_valid(bus)) {
    return HAL_EINVAL;
  }
#ifdef JH_STM32G474_HW
  uint8_t clear_sda = 0u;
  uint8_t clear_scl = 0u;
  if (!i2c_resolve_clear_pins(bus, sda_pin, scl_pin, &clear_sda, &clear_scl)) {
    return HAL_EINVAL;
  }
#endif
  i2c_bus_clear_bus_impl(bus, sda_pin, scl_pin);
  return HAL_OK;
}

static void i2c_bus_clear_bus_impl(uint8_t bus, uint8_t sda_pin,
                                   uint8_t scl_pin) {
#ifdef JH_STM32G474_HW
  uint8_t clear_sda = 0u;
  uint8_t clear_scl = 0u;
  if (!i2c_resolve_clear_pins(bus, sda_pin, scl_pin, &clear_sda, &clear_scl)) {
    HAL_ASSERT(false, "hal_i2c_bus_clear_bus: invalid pin id");
    i2c_state(bus)->bus_clear_count++;
    return;
  }

  i2c_gpio_set_input_pullup(clear_sda);
  i2c_gpio_set_output_od_pullup(clear_scl);
  i2c_gpio_write(clear_scl, true);
  i2c_bus_clear_delay();

  for (uint8_t i = 0u; i < 9u; ++i) {
    if (i2c_gpio_read(clear_sda)) {
      break;
    }
    i2c_gpio_write(clear_scl, false);
    i2c_bus_clear_delay();
    i2c_gpio_write(clear_scl, true);
    i2c_bus_clear_delay();
  }

  i2c_gpio_set_output_od_pullup(clear_sda);
  i2c_gpio_write(clear_sda, false);
  i2c_bus_clear_delay();
  i2c_gpio_write(clear_scl, true);
  i2c_bus_clear_delay();
  i2c_gpio_write(clear_sda, true);
  i2c_bus_clear_delay();

  i2c_gpio_set_input_pullup(clear_sda);
  i2c_gpio_set_input_pullup(clear_scl);
#else
  (void)sda_pin;
  (void)scl_pin;
#endif

  i2c_state(bus)->bus_clear_count++;
}

#endif /* HAL_ENABLE_I2C */

#endif // HAL_TARGET_IS_STM32G474
