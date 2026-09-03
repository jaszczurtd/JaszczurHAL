/*
 * Shared simple I/O chip drivers.
 *
 * The MCP23017, PCA9654E, PCF8574, 74HC595, MCP3221 and MCP4725 transaction
 * flows are based on working grblHAL plugin drivers by Terje Io. This refactor
 * preserves the proven register sequences and data encoding while using
 * JaszczurHAL bus, GPIO, timing, status and synchronization primitives.
 */

#include "hal/core/hal_target.h"
#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_MCP23017) || defined(HAL_ENABLE_PCA9654E) ||            \
    defined(HAL_ENABLE_PCF8574) || defined(HAL_ENABLE_HC595) ||                \
    defined(HAL_ENABLE_MCP3221) || defined(HAL_ENABLE_MCP4725)

#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_status.h"
#include "hal/core/jh_endian.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/i2c/hal_i2c.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_system.h"

#include <stddef.h>
#include <stdint.h>

static hal_status_t status_from_i2c(bool ok) { return ok ? HAL_OK : HAL_EBUS; }

static hal_status_t status_from_i2c_end(uint8_t code) {
  return (code == 0u) ? HAL_OK : HAL_EBUS;
}

#if defined(HAL_ENABLE_MCP23017) || defined(HAL_ENABLE_PCA9654E) ||            \
    defined(HAL_ENABLE_PCF8574) || defined(HAL_ENABLE_MCP4725)
static hal_status_t i2c_write_bytes(uint8_t bus, uint8_t addr,
                                    const uint8_t *data, size_t len) {
  if (data == NULL && len > 0u) {
    return HAL_EINVAL;
  }

  hal_i2c_begin_transmission_bus(bus, addr);
  for (size_t i = 0; i < len; ++i) {
    if (hal_i2c_write_bus(bus, data[i]) != 1u) {
      (void)hal_i2c_end_transmission_bus(bus);
      return HAL_EBUS;
    }
  }
  return status_from_i2c_end(hal_i2c_end_transmission_bus(bus));
}
#endif

#if defined(HAL_ENABLE_MCP23017) || defined(HAL_ENABLE_PCA9654E)
static hal_status_t i2c_write_reg8(uint8_t bus, uint8_t addr, uint8_t reg,
                                   uint8_t value) {
  const uint8_t data[2] = {reg, value};
  return i2c_write_bytes(bus, addr, data, sizeof(data));
}
#endif

#if defined(HAL_ENABLE_MCP23017)
#include "hal/gpio/hal_mcp23017.h"

#define MCP23017_IODIRA 0x00u
#define MCP23017_IODIRB 0x01u
#define MCP23017_IPOLA 0x02u
#define MCP23017_GPINTENA 0x04u
#define MCP23017_DEFVALA 0x06u
#define MCP23017_INTCONA 0x08u
#define MCP23017_GPPUA 0x0Cu
#define MCP23017_INTFA 0x0Eu
#define MCP23017_INTCAPA 0x10u
#define MCP23017_GPIOA 0x12u

static bool mcp23017_valid(const hal_mcp23017_t *dev) {
  return dev != NULL && dev->initialized && dev->mutex != NULL;
}

static bool mcp23017_mode_valid(hal_mcp23017_mode_t mode) {
  return mode == HAL_MCP23017_MODE_8_IN_8_OUT ||
         mode == HAL_MCP23017_MODE_16_OUT || mode == HAL_MCP23017_MODE_16_IN;
}

static uint8_t mcp23017_input_offset(const hal_mcp23017_t *dev) {
  return dev->cfg.mode == HAL_MCP23017_MODE_8_IN_8_OUT ? 1u : 0u;
}

static uint8_t mcp23017_input_count_mode(hal_mcp23017_mode_t mode) {
  if (mode == HAL_MCP23017_MODE_8_IN_8_OUT) {
    return 8u;
  }
  if (mode == HAL_MCP23017_MODE_16_IN) {
    return 16u;
  }
  return 0u;
}

static uint8_t mcp23017_output_count_mode(hal_mcp23017_mode_t mode) {
  if (mode == HAL_MCP23017_MODE_8_IN_8_OUT) {
    return 8u;
  }
  if (mode == HAL_MCP23017_MODE_16_OUT) {
    return 16u;
  }
  return 0u;
}

static uint16_t mcp23017_mask_for_count(uint8_t count) {
  return count >= 16u ? 0xFFFFu : (uint16_t)((1u << count) - 1u);
}

static hal_status_t mcp23017_write_reg(uint8_t bus, uint8_t addr, uint8_t reg,
                                       uint16_t value, uint8_t width) {
  uint8_t data[3] = {reg, 0u, 0u};
  jh_store_le16(&data[1], value);
  return i2c_write_bytes(bus, addr, data, (width == 2u) ? 3u : 2u);
}

static hal_status_t mcp23017_read_reg(uint8_t bus, uint8_t addr, uint8_t reg,
                                      uint16_t *out_value, uint8_t width) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  uint8_t rx[2] = {0u, 0u};
  if (!hal_i2c_write_read_bus(bus, addr, &reg, 1u, rx, width)) {
    return HAL_EBUS;
  }
  *out_value = jh_load_le16(rx);
  return HAL_OK;
}

hal_mcp23017_config_t hal_mcp23017_default_config(void) {
  hal_mcp23017_config_t cfg = {0u, HAL_MCP23017_I2C_ADDR_DEFAULT,
                               HAL_MCP23017_MODE_8_IN_8_OUT};
  return cfg;
}

hal_status_t hal_mcp23017_init_ex(hal_mcp23017_t *dev,
                                  const hal_mcp23017_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }

  hal_mcp23017_config_t effective =
      cfg != NULL ? *cfg : hal_mcp23017_default_config();
  if (!mcp23017_mode_valid(effective.mode)) {
    return HAL_EINVAL;
  }

  *dev = {};
  dev->cfg = effective;
  dev->mutex = jh_hal_mutex_create_once(&dev->mutex);
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(dev->mutex);

  const uint8_t in_count = mcp23017_input_count_mode(dev->cfg.mode);
  const uint8_t out_count = mcp23017_output_count_mode(dev->cfg.mode);
  const uint16_t out_mask = mcp23017_mask_for_count(out_count);
  const uint8_t in_offset =
      dev->cfg.mode == HAL_MCP23017_MODE_8_IN_8_OUT ? 1u : 0u;
  const uint8_t out_width = out_count > 8u ? 2u : 1u;
  const uint8_t in_width = in_count > 8u ? 2u : 1u;

  hal_status_t status = HAL_OK;
  if (dev->cfg.mode == HAL_MCP23017_MODE_8_IN_8_OUT) {
    status = i2c_write_reg8(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                            MCP23017_IODIRA, 0x00u);
    if (status == HAL_OK) {
      status = i2c_write_reg8(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                              MCP23017_IODIRB, 0xFFu);
    }
  } else {
    status = mcp23017_write_reg(
        dev->cfg.i2c_bus, dev->cfg.i2c_addr, MCP23017_IODIRA,
        dev->cfg.mode == HAL_MCP23017_MODE_16_IN ? 0xFFFFu : 0x0000u, 2u);
  }

  if (status == HAL_OK && in_count > 0u) {
    status =
        mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                           (uint8_t)(MCP23017_IPOLA + in_offset), 0u, in_width);
  }
  if (status == HAL_OK && in_count > 0u) {
    status =
        mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                           (uint8_t)(MCP23017_GPPUA + in_offset), 0u, in_width);
  }
  if (status == HAL_OK && in_count > 0u) {
    status = mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                (uint8_t)(MCP23017_GPINTENA + in_offset), 0u,
                                in_width);
  }
  if (status == HAL_OK && out_count > 0u) {
    dev->outputs = 0u;
    status =
        mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr, MCP23017_GPIOA,
                           dev->outputs & out_mask, out_width);
  }

  if (status == HAL_OK) {
    dev->initialized = true;
  }

  hal_mutex_unlock(dev->mutex);

  if (status != HAL_OK) {
    hal_mcp23017_deinit(dev);
  }
  return status;
}

bool hal_mcp23017_init(hal_mcp23017_t *dev, const hal_mcp23017_config_t *cfg) {
  return hal_status_to_bool(hal_mcp23017_init_ex(dev, cfg));
}

void hal_mcp23017_deinit(hal_mcp23017_t *dev) {
  if (dev == NULL) {
    return;
  }
  if (dev->mutex != NULL) {
    hal_mutex_destroy(dev->mutex);
  }
  *dev = {};
}

uint8_t hal_mcp23017_input_count(const hal_mcp23017_t *dev) {
  return dev == NULL ? 0u : mcp23017_input_count_mode(dev->cfg.mode);
}

uint8_t hal_mcp23017_output_count(const hal_mcp23017_t *dev) {
  return dev == NULL ? 0u : mcp23017_output_count_mode(dev->cfg.mode);
}

hal_status_t hal_mcp23017_write_all_ex(hal_mcp23017_t *dev, uint16_t value) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_output_count(dev);
  if (count == 0u) {
    return HAL_EUNSUPPORTED;
  }

  const uint16_t mask = mcp23017_mask_for_count(count);
  hal_mutex_lock(dev->mutex);
  dev->outputs = value & mask;
  const hal_status_t status =
      mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr, MCP23017_GPIOA,
                         dev->outputs, count > 8u ? 2u : 1u);
  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_mcp23017_write_all(hal_mcp23017_t *dev, uint16_t value) {
  return hal_status_to_bool(hal_mcp23017_write_all_ex(dev, value));
}

hal_status_t hal_mcp23017_write_pin_ex(hal_mcp23017_t *dev, uint8_t pin,
                                       bool on) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_output_count(dev);
  if (pin >= count) {
    return HAL_EINVAL;
  }

  const uint16_t bit = (uint16_t)(1u << pin);
  bool effective = on;
  if ((dev->output_inverted & bit) != 0u) {
    effective = !effective;
  }
  uint16_t next = dev->outputs;
  if (effective) {
    next |= bit;
  } else {
    next &= (uint16_t)~bit;
  }
  return hal_mcp23017_write_all_ex(dev, next);
}

bool hal_mcp23017_write_pin(hal_mcp23017_t *dev, uint8_t pin, bool on) {
  return hal_status_to_bool(hal_mcp23017_write_pin_ex(dev, pin, on));
}

uint16_t hal_mcp23017_output_latch(const hal_mcp23017_t *dev) {
  return dev == NULL ? 0u : dev->outputs;
}

hal_status_t hal_mcp23017_read_all_ex(hal_mcp23017_t *dev,
                                      uint16_t *out_value) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_input_count(dev);
  if (count == 0u) {
    return HAL_EUNSUPPORTED;
  }
  const uint8_t offset = mcp23017_input_offset(dev);
  hal_mutex_lock(dev->mutex);
  const hal_status_t status = mcp23017_read_reg(
      dev->cfg.i2c_bus, dev->cfg.i2c_addr, (uint8_t)(MCP23017_GPIOA + offset),
      out_value, count > 8u ? 2u : 1u);
  if (status == HAL_OK && out_value != NULL) {
    *out_value &= mcp23017_mask_for_count(count);
  }
  hal_mutex_unlock(dev->mutex);
  return status;
}

uint16_t hal_mcp23017_read_all(hal_mcp23017_t *dev) {
  uint16_t value = 0u;
  (void)hal_mcp23017_read_all_ex(dev, &value);
  return value;
}

hal_status_t hal_mcp23017_read_pin_ex(hal_mcp23017_t *dev, uint8_t pin,
                                      bool *out_value) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  const uint8_t count = hal_mcp23017_input_count(dev);
  if (pin >= count) {
    return HAL_EINVAL;
  }
  uint16_t value = 0u;
  const hal_status_t status = hal_mcp23017_read_all_ex(dev, &value);
  if (status == HAL_OK) {
    *out_value = (value & (1u << pin)) != 0u;
  }
  return status;
}

bool hal_mcp23017_read_pin(hal_mcp23017_t *dev, uint8_t pin) {
  bool value = false;
  (void)hal_mcp23017_read_pin_ex(dev, pin, &value);
  return value;
}

hal_status_t hal_mcp23017_config_input_ex(hal_mcp23017_t *dev, uint8_t pin,
                                          bool inverted, bool pullup) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_input_count(dev);
  if (pin >= count) {
    return HAL_EINVAL;
  }

  const uint16_t bit = (uint16_t)(1u << pin);
  const uint8_t offset = mcp23017_input_offset(dev);
  const uint8_t width = count > 8u ? 2u : 1u;
  hal_mutex_lock(dev->mutex);
  if (inverted) {
    dev->input_inverted |= bit;
  } else {
    dev->input_inverted &= (uint16_t)~bit;
  }
  hal_status_t status = mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                           (uint8_t)(MCP23017_IPOLA + offset),
                                           dev->input_inverted, width);
  if (status == HAL_OK) {
    if (pullup) {
      dev->input_pullup |= bit;
    } else {
      dev->input_pullup &= (uint16_t)~bit;
    }
    status = mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                (uint8_t)(MCP23017_GPPUA + offset),
                                dev->input_pullup, width);
  }
  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_mcp23017_config_input(hal_mcp23017_t *dev, uint8_t pin, bool inverted,
                               bool pullup) {
  return hal_status_to_bool(
      hal_mcp23017_config_input_ex(dev, pin, inverted, pullup));
}

hal_status_t hal_mcp23017_config_output_ex(hal_mcp23017_t *dev, uint8_t pin,
                                           bool inverted) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_output_count(dev);
  if (pin >= count) {
    return HAL_EINVAL;
  }
  const uint16_t bit = (uint16_t)(1u << pin);
  if (inverted) {
    dev->output_inverted |= bit;
  } else {
    dev->output_inverted &= (uint16_t)~bit;
  }
  return hal_mcp23017_write_pin_ex(dev, pin, (dev->outputs & bit) != 0u);
}

bool hal_mcp23017_config_output(hal_mcp23017_t *dev, uint8_t pin,
                                bool inverted) {
  return hal_status_to_bool(hal_mcp23017_config_output_ex(dev, pin, inverted));
}

hal_status_t hal_mcp23017_config_irq_ex(hal_mcp23017_t *dev, uint8_t pin,
                                        hal_mcp23017_irq_mode_t mode) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_input_count(dev);
  if (pin >= count) {
    return HAL_EINVAL;
  }

  const uint16_t bit = (uint16_t)(1u << pin);
  if (mode == HAL_MCP23017_IRQ_NONE) {
    dev->irq_enabled &= (uint16_t)~bit;
  } else {
    dev->irq_enabled |= bit;
    switch (mode) {
    case HAL_MCP23017_IRQ_RISING:
    case HAL_MCP23017_IRQ_FALLING:
    case HAL_MCP23017_IRQ_CHANGE:
      dev->irq_change &= (uint16_t)~bit;
      dev->irq_level &= (uint16_t)~bit;
      break;
    case HAL_MCP23017_IRQ_HIGH:
      dev->irq_change |= bit;
      dev->irq_level &= (uint16_t)~bit;
      break;
    case HAL_MCP23017_IRQ_LOW:
      dev->irq_change |= bit;
      dev->irq_level |= bit;
      break;
    case HAL_MCP23017_IRQ_NONE:
    default:
      break;
    }
  }

  const uint8_t offset = mcp23017_input_offset(dev);
  const uint8_t width = count > 8u ? 2u : 1u;
  hal_mutex_lock(dev->mutex);
  hal_status_t status = mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                           (uint8_t)(MCP23017_DEFVALA + offset),
                                           dev->irq_level, width);
  if (status == HAL_OK) {
    status = mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                (uint8_t)(MCP23017_INTCONA + offset),
                                dev->irq_change, width);
  }
  if (status == HAL_OK) {
    status = mcp23017_write_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                (uint8_t)(MCP23017_GPINTENA + offset),
                                dev->irq_enabled, width);
  }
  hal_mutex_unlock(dev->mutex);
  return status;
}

hal_status_t hal_mcp23017_read_irq_ex(hal_mcp23017_t *dev, uint16_t *out_flags,
                                      uint16_t *out_captured) {
  if (!mcp23017_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_mcp23017_input_count(dev);
  if (count == 0u) {
    return HAL_EUNSUPPORTED;
  }
  const uint8_t offset = mcp23017_input_offset(dev);
  const uint8_t width = count > 8u ? 2u : 1u;
  hal_mutex_lock(dev->mutex);
  hal_status_t status = mcp23017_read_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                          (uint8_t)(MCP23017_INTFA + offset),
                                          &dev->irq_flag, width);
  if (status == HAL_OK) {
    status = mcp23017_read_reg(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                               (uint8_t)(MCP23017_INTCAPA + offset),
                               &dev->irq_value, width);
  }
  hal_mutex_unlock(dev->mutex);
  if (status == HAL_OK) {
    if (out_flags != NULL) {
      *out_flags = dev->irq_flag;
    }
    if (out_captured != NULL) {
      *out_captured = dev->irq_value;
    }
  }
  return status;
}
#endif /* HAL_ENABLE_MCP23017 */

#if defined(HAL_ENABLE_PCA9654E)
#include "hal/gpio/hal_pca9654e.h"

#define PCA9654E_READ_INPUT 0u
#define PCA9654E_RW_OUTPUT 1u
#define PCA9654E_RW_INVERSION 2u
#define PCA9654E_RW_CONFIG 3u

static bool pca9654e_valid(const hal_pca9654e_t *dev) {
  return dev != NULL && dev->initialized && dev->mutex != NULL;
}

hal_pca9654e_config_t hal_pca9654e_default_config(void) {
  hal_pca9654e_config_t cfg = {0u, HAL_PCA9654E_I2C_ADDR_DEFAULT};
  return cfg;
}

hal_status_t hal_pca9654e_init_ex(hal_pca9654e_t *dev,
                                  const hal_pca9654e_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  *dev = {};
  dev->cfg = cfg != NULL ? *cfg : hal_pca9654e_default_config();
  dev->mutex = jh_hal_mutex_create_once(&dev->mutex);
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(dev->mutex);
  hal_status_t status = i2c_write_reg8(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                       PCA9654E_RW_CONFIG, 0u);
  if (status == HAL_OK) {
    status = i2c_write_reg8(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                            PCA9654E_RW_INVERSION, 0u);
  }
  if (status == HAL_OK) {
    status = i2c_write_reg8(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                            PCA9654E_RW_OUTPUT, 0u);
  }
  if (status == HAL_OK) {
    dev->initialized = true;
  }
  hal_mutex_unlock(dev->mutex);

  if (status != HAL_OK) {
    hal_pca9654e_deinit(dev);
  }
  return status;
}

bool hal_pca9654e_init(hal_pca9654e_t *dev, const hal_pca9654e_config_t *cfg) {
  return hal_status_to_bool(hal_pca9654e_init_ex(dev, cfg));
}

void hal_pca9654e_deinit(hal_pca9654e_t *dev) {
  if (dev == NULL) {
    return;
  }
  if (dev->mutex != NULL) {
    hal_mutex_destroy(dev->mutex);
  }
  *dev = {};
}

hal_status_t hal_pca9654e_write_all_ex(hal_pca9654e_t *dev, uint8_t value) {
  if (!pca9654e_valid(dev)) {
    return HAL_EUNINIT;
  }
  hal_mutex_lock(dev->mutex);
  dev->outputs = value;
  const hal_status_t status = i2c_write_reg8(
      dev->cfg.i2c_bus, dev->cfg.i2c_addr, PCA9654E_RW_OUTPUT, dev->outputs);
  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_pca9654e_write_all(hal_pca9654e_t *dev, uint8_t value) {
  return hal_status_to_bool(hal_pca9654e_write_all_ex(dev, value));
}

hal_status_t hal_pca9654e_write_pin_ex(hal_pca9654e_t *dev, uint8_t pin,
                                       bool on) {
  if (!pca9654e_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (pin >= HAL_PCA9654E_PIN_COUNT) {
    return HAL_EINVAL;
  }
  const uint8_t bit = (uint8_t)(1u << pin);
  bool effective = on;
  if ((dev->inverted & bit) != 0u) {
    effective = !effective;
  }
  uint8_t next = dev->outputs;
  if (effective) {
    next |= bit;
  } else {
    next &= (uint8_t)~bit;
  }
  return hal_pca9654e_write_all_ex(dev, next);
}

bool hal_pca9654e_write_pin(hal_pca9654e_t *dev, uint8_t pin, bool on) {
  return hal_status_to_bool(hal_pca9654e_write_pin_ex(dev, pin, on));
}

hal_status_t hal_pca9654e_config_pin_ex(hal_pca9654e_t *dev, uint8_t pin,
                                        bool inverted) {
  if (!pca9654e_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (pin >= HAL_PCA9654E_PIN_COUNT) {
    return HAL_EINVAL;
  }
  const uint8_t bit = (uint8_t)(1u << pin);
  if (inverted) {
    dev->inverted |= bit;
  } else {
    dev->inverted &= (uint8_t)~bit;
  }
  return hal_pca9654e_write_pin_ex(dev, pin, (dev->outputs & bit) != 0u);
}

bool hal_pca9654e_config_pin(hal_pca9654e_t *dev, uint8_t pin, bool inverted) {
  return hal_status_to_bool(hal_pca9654e_config_pin_ex(dev, pin, inverted));
}

uint8_t hal_pca9654e_output_latch(const hal_pca9654e_t *dev) {
  return dev == NULL ? 0u : dev->outputs;
}
#endif /* HAL_ENABLE_PCA9654E */

#if defined(HAL_ENABLE_PCF8574)
#include "hal/gpio/hal_pcf8574.h"

static bool pcf8574_valid(const hal_pcf8574_t *dev) {
  return dev != NULL && dev->initialized && dev->mutex != NULL;
}

hal_pcf8574_config_t hal_pcf8574_default_config(void) {
  hal_pcf8574_config_t cfg = {0u, HAL_PCF8574_I2C_ADDR_DEFAULT, 0xFFu};
  return cfg;
}

hal_status_t hal_pcf8574_init_ex(hal_pcf8574_t *dev,
                                 const hal_pcf8574_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  *dev = {};
  dev->cfg = cfg != NULL ? *cfg : hal_pcf8574_default_config();
  dev->mutex = jh_hal_mutex_create_once(&dev->mutex);
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(dev->mutex);
  dev->latch = dev->cfg.initial_latch;
  const hal_status_t status =
      i2c_write_bytes(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &dev->latch, 1u);
  if (status == HAL_OK) {
    dev->initialized = true;
  }
  hal_mutex_unlock(dev->mutex);

  if (status != HAL_OK) {
    hal_pcf8574_deinit(dev);
  }
  return status;
}

bool hal_pcf8574_init(hal_pcf8574_t *dev, const hal_pcf8574_config_t *cfg) {
  return hal_status_to_bool(hal_pcf8574_init_ex(dev, cfg));
}

void hal_pcf8574_deinit(hal_pcf8574_t *dev) {
  if (dev == NULL) {
    return;
  }
  if (dev->mutex != NULL) {
    hal_mutex_destroy(dev->mutex);
  }
  *dev = {};
}

hal_status_t hal_pcf8574_write_all_ex(hal_pcf8574_t *dev, uint8_t value) {
  if (!pcf8574_valid(dev)) {
    return HAL_EUNINIT;
  }
  hal_mutex_lock(dev->mutex);
  dev->latch = value;
  const hal_status_t status =
      i2c_write_bytes(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &dev->latch, 1u);
  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_pcf8574_write_all(hal_pcf8574_t *dev, uint8_t value) {
  return hal_status_to_bool(hal_pcf8574_write_all_ex(dev, value));
}

hal_status_t hal_pcf8574_write_pin_ex(hal_pcf8574_t *dev, uint8_t pin,
                                      bool on) {
  if (!pcf8574_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (pin >= HAL_PCF8574_PIN_COUNT) {
    return HAL_EINVAL;
  }
  const uint8_t bit = (uint8_t)(1u << pin);
  bool effective = on;
  if ((dev->inverted & bit) != 0u) {
    effective = !effective;
  }
  uint8_t next = dev->latch;
  if (effective) {
    next |= bit;
  } else {
    next &= (uint8_t)~bit;
  }
  return hal_pcf8574_write_all_ex(dev, next);
}

bool hal_pcf8574_write_pin(hal_pcf8574_t *dev, uint8_t pin, bool on) {
  return hal_status_to_bool(hal_pcf8574_write_pin_ex(dev, pin, on));
}

hal_status_t hal_pcf8574_read_all_ex(hal_pcf8574_t *dev, uint8_t *out_value) {
  if (!pcf8574_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  uint8_t value = 0u;
  hal_mutex_lock(dev->mutex);
  const hal_status_t status = status_from_i2c(
      hal_i2c_read_bytes_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &value, 1u));
  hal_mutex_unlock(dev->mutex);
  if (status == HAL_OK) {
    *out_value = (uint8_t)(value ^ dev->inverted);
  }
  return status;
}

uint8_t hal_pcf8574_read_all(hal_pcf8574_t *dev) {
  uint8_t value = 0u;
  (void)hal_pcf8574_read_all_ex(dev, &value);
  return value;
}

hal_status_t hal_pcf8574_read_pin_ex(hal_pcf8574_t *dev, uint8_t pin,
                                     bool *out_value) {
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  if (pin >= HAL_PCF8574_PIN_COUNT) {
    return HAL_EINVAL;
  }
  uint8_t value = 0u;
  const hal_status_t status = hal_pcf8574_read_all_ex(dev, &value);
  if (status == HAL_OK) {
    *out_value = (value & (1u << pin)) != 0u;
  }
  return status;
}

bool hal_pcf8574_read_pin(hal_pcf8574_t *dev, uint8_t pin) {
  bool value = false;
  (void)hal_pcf8574_read_pin_ex(dev, pin, &value);
  return value;
}

hal_status_t hal_pcf8574_config_pin_ex(hal_pcf8574_t *dev, uint8_t pin,
                                       bool inverted) {
  if (!pcf8574_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (pin >= HAL_PCF8574_PIN_COUNT) {
    return HAL_EINVAL;
  }
  const uint8_t bit = (uint8_t)(1u << pin);
  if (inverted) {
    dev->inverted |= bit;
  } else {
    dev->inverted &= (uint8_t)~bit;
  }
  return hal_pcf8574_write_pin_ex(dev, pin, (dev->latch & bit) != 0u);
}

bool hal_pcf8574_config_pin(hal_pcf8574_t *dev, uint8_t pin, bool inverted) {
  return hal_status_to_bool(hal_pcf8574_config_pin_ex(dev, pin, inverted));
}

uint8_t hal_pcf8574_output_latch(const hal_pcf8574_t *dev) {
  return dev == NULL ? 0u : dev->latch;
}
#endif /* HAL_ENABLE_PCF8574 */

#if defined(HAL_ENABLE_HC595)
#include "hal/gpio/hal_hc595.h"

static bool hc595_valid(const hal_hc595_t *dev) {
  return dev != NULL && dev->initialized && dev->mutex != NULL;
}

static uint8_t hc595_chip_count(uint8_t chips) {
  if (chips == 0u) {
    return 1u;
  }
  return chips > HAL_HC595_MAX_CHIPS ? HAL_HC595_MAX_CHIPS : chips;
}

static hal_status_t hc595_flush_locked(hal_hc595_t *dev) {
  const uint8_t chips = hc595_chip_count(dev->cfg.chips);
  uint8_t data[HAL_HC595_MAX_CHIPS] = {};
  for (uint8_t i = 0u; i < chips; ++i) {
    const uint8_t shift = (uint8_t)((chips - 1u - i) * 8u);
    data[i] = (uint8_t)(dev->outputs >> shift);
  }

  const hal_spi_settings_t settings = {dev->cfg.clock_hz, HAL_SPI_MSBFIRST,
                                       HAL_SPI_MODE0};
  hal_spi_lock(dev->cfg.spi_bus);
  hal_gpio_write(dev->cfg.cs_pin, false);
  hal_spi_begin_transaction(dev->cfg.spi_bus, &settings);
  hal_spi_write(dev->cfg.spi_bus, data, chips);
  hal_spi_end_transaction(dev->cfg.spi_bus);
  hal_gpio_write(dev->cfg.cs_pin, true);
  hal_spi_unlock(dev->cfg.spi_bus);
  return HAL_OK;
}

hal_hc595_config_t hal_hc595_default_config(uint8_t cs_pin) {
  hal_hc595_config_t cfg = {0u, cs_pin, 1u, HAL_SPI_CLOCK_DEFAULT_HZ};
  return cfg;
}

hal_status_t hal_hc595_init_ex(hal_hc595_t *dev,
                               const hal_hc595_config_t *cfg) {
  if (dev == NULL || cfg == NULL || cfg->chips == 0u ||
      cfg->chips > HAL_HC595_MAX_CHIPS) {
    return HAL_EINVAL;
  }
  *dev = {};
  dev->cfg = *cfg;
  if (dev->cfg.clock_hz == 0u) {
    dev->cfg.clock_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
  }
  dev->mutex = jh_hal_mutex_create_once(&dev->mutex);
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }

  hal_gpio_set_mode(dev->cfg.cs_pin, HAL_GPIO_OUTPUT_HIGH);
  hal_mutex_lock(dev->mutex);
  hal_status_t status = hc595_flush_locked(dev);
  if (status == HAL_OK) {
    dev->initialized = true;
  }
  hal_mutex_unlock(dev->mutex);
  if (status != HAL_OK) {
    hal_hc595_deinit(dev);
  }
  return status;
}

bool hal_hc595_init(hal_hc595_t *dev, const hal_hc595_config_t *cfg) {
  return hal_status_to_bool(hal_hc595_init_ex(dev, cfg));
}

void hal_hc595_deinit(hal_hc595_t *dev) {
  if (dev == NULL) {
    return;
  }
  if (dev->mutex != NULL) {
    hal_mutex_destroy(dev->mutex);
  }
  *dev = {};
}

uint8_t hal_hc595_output_count(const hal_hc595_t *dev) {
  return dev == NULL ? 0u : (uint8_t)(hc595_chip_count(dev->cfg.chips) * 8u);
}

hal_status_t hal_hc595_write_all_ex(hal_hc595_t *dev, uint32_t value) {
  if (!hc595_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint8_t count = hal_hc595_output_count(dev);
  const uint32_t mask = count >= 32u ? 0xFFFFFFFFu : ((1ul << count) - 1ul);
  hal_mutex_lock(dev->mutex);
  dev->outputs = value & mask;
  const hal_status_t status = hc595_flush_locked(dev);
  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_hc595_write_all(hal_hc595_t *dev, uint32_t value) {
  return hal_status_to_bool(hal_hc595_write_all_ex(dev, value));
}

hal_status_t hal_hc595_write_pin_ex(hal_hc595_t *dev, uint8_t pin, bool on) {
  if (!hc595_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (pin >= hal_hc595_output_count(dev)) {
    return HAL_EINVAL;
  }
  const uint32_t bit = 1ul << pin;
  bool effective = on;
  if ((dev->inverted & bit) != 0u) {
    effective = !effective;
  }
  uint32_t next = dev->outputs;
  if (effective) {
    next |= bit;
  } else {
    next &= ~bit;
  }
  return hal_hc595_write_all_ex(dev, next);
}

bool hal_hc595_write_pin(hal_hc595_t *dev, uint8_t pin, bool on) {
  return hal_status_to_bool(hal_hc595_write_pin_ex(dev, pin, on));
}

hal_status_t hal_hc595_config_pin_ex(hal_hc595_t *dev, uint8_t pin,
                                     bool inverted) {
  if (!hc595_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (pin >= hal_hc595_output_count(dev)) {
    return HAL_EINVAL;
  }
  const uint32_t bit = 1ul << pin;
  if (inverted) {
    dev->inverted |= bit;
  } else {
    dev->inverted &= ~bit;
  }
  return hal_hc595_write_pin_ex(dev, pin, (dev->outputs & bit) != 0u);
}

bool hal_hc595_config_pin(hal_hc595_t *dev, uint8_t pin, bool inverted) {
  return hal_status_to_bool(hal_hc595_config_pin_ex(dev, pin, inverted));
}

uint32_t hal_hc595_output_latch(const hal_hc595_t *dev) {
  return dev == NULL ? 0u : dev->outputs;
}
#endif /* HAL_ENABLE_HC595 */

#if defined(HAL_ENABLE_MCP3221)
#include "hal/analog/hal_mcp3221.h"

static bool mcp3221_valid(const hal_mcp3221_t *dev) {
  return dev != NULL && dev->initialized && dev->mutex != NULL;
}

hal_mcp3221_config_t hal_mcp3221_default_config(void) {
  hal_mcp3221_config_t cfg = {0u, HAL_MCP3221_I2C_ADDR_DEFAULT};
  return cfg;
}

hal_status_t hal_mcp3221_init_ex(hal_mcp3221_t *dev,
                                 const hal_mcp3221_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  *dev = {};
  dev->cfg = cfg != NULL ? *cfg : hal_mcp3221_default_config();
  dev->mutex = jh_hal_mutex_create_once(&dev->mutex);
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }
  dev->initialized = true;
  return HAL_OK;
}

bool hal_mcp3221_init(hal_mcp3221_t *dev, const hal_mcp3221_config_t *cfg) {
  return hal_status_to_bool(hal_mcp3221_init_ex(dev, cfg));
}

void hal_mcp3221_deinit(hal_mcp3221_t *dev) {
  if (dev == NULL) {
    return;
  }
  if (dev->mutex != NULL) {
    hal_mutex_destroy(dev->mutex);
  }
  *dev = {};
}

hal_status_t hal_mcp3221_read_ex(hal_mcp3221_t *dev, uint16_t *out_value) {
  if (!mcp3221_valid(dev)) {
    return HAL_EUNINIT;
  }
  if (out_value == NULL) {
    return HAL_EINVAL;
  }
  uint8_t data[2] = {0u, 0u};
  hal_mutex_lock(dev->mutex);
  const hal_status_t status = status_from_i2c(hal_i2c_read_bytes_bus(
      dev->cfg.i2c_bus, dev->cfg.i2c_addr, data, sizeof(data)));
  if (status == HAL_OK) {
    dev->value = jh_load_be16(data);
    *out_value = dev->value;
  }
  hal_mutex_unlock(dev->mutex);
  return status;
}

uint16_t hal_mcp3221_read(hal_mcp3221_t *dev) {
  uint16_t value = 0u;
  (void)hal_mcp3221_read_ex(dev, &value);
  return value;
}
#endif /* HAL_ENABLE_MCP3221 */

#if defined(HAL_ENABLE_MCP4725)
#include "hal/analog/hal_mcp4725.h"

static bool mcp4725_valid(const hal_mcp4725_t *dev) {
  return dev != NULL && dev->initialized && dev->mutex != NULL;
}

hal_mcp4725_config_t hal_mcp4725_default_config(void) {
  hal_mcp4725_config_t cfg = {0u, HAL_MCP4725_I2C_ADDR_DEFAULT, true};
  return cfg;
}

hal_status_t hal_mcp4725_init_ex(hal_mcp4725_t *dev,
                                 const hal_mcp4725_config_t *cfg) {
  if (dev == NULL) {
    return HAL_EINVAL;
  }
  *dev = {};
  dev->cfg = cfg != NULL ? *cfg : hal_mcp4725_default_config();
  dev->mutex = jh_hal_mutex_create_once(&dev->mutex);
  if (dev->mutex == NULL) {
    return HAL_ENOMEM;
  }

  hal_mutex_lock(dev->mutex);
  hal_status_t status = HAL_OK;
  if (dev->cfg.wake_on_init) {
    const uint8_t reset = 0x06u;
    const uint8_t wake = 0x09u;
    status = i2c_write_bytes(dev->cfg.i2c_bus, 0u, &reset, 1u);
    if (status == HAL_OK) {
      status = i2c_write_bytes(dev->cfg.i2c_bus, 0u, &wake, 1u);
    }
    if (status == HAL_OK) {
      hal_delay_ms(2u);
    }
  }
  uint8_t data[5] = {};
  if (status == HAL_OK) {
    status = status_from_i2c(hal_i2c_read_bytes_bus(
        dev->cfg.i2c_bus, dev->cfg.i2c_addr, data, sizeof(data)));
  }
  if (status == HAL_OK) {
    dev->value = (uint16_t)(jh_load_be16(&data[1]) >> 4u);
    dev->initialized = true;
  }
  hal_mutex_unlock(dev->mutex);

  if (status != HAL_OK) {
    hal_mcp4725_deinit(dev);
  }
  return status;
}

bool hal_mcp4725_init(hal_mcp4725_t *dev, const hal_mcp4725_config_t *cfg) {
  return hal_status_to_bool(hal_mcp4725_init_ex(dev, cfg));
}

void hal_mcp4725_deinit(hal_mcp4725_t *dev) {
  if (dev == NULL) {
    return;
  }
  if (dev->mutex != NULL) {
    hal_mutex_destroy(dev->mutex);
  }
  *dev = {};
}

hal_status_t hal_mcp4725_write_ex(hal_mcp4725_t *dev, uint16_t value) {
  if (!mcp4725_valid(dev)) {
    return HAL_EUNINIT;
  }
  const uint16_t raw = (uint16_t)(value & HAL_MCP4725_MAX_VALUE);
  const uint8_t data[3] = {0x40u, (uint8_t)((raw & 0x0FF0u) >> 4),
                           (uint8_t)((raw & 0x000Fu) << 4)};

  hal_mutex_lock(dev->mutex);
  const hal_status_t status =
      i2c_write_bytes(dev->cfg.i2c_bus, dev->cfg.i2c_addr, data, sizeof(data));
  if (status == HAL_OK) {
    dev->value = raw;
  }
  hal_mutex_unlock(dev->mutex);
  return status;
}

bool hal_mcp4725_write(hal_mcp4725_t *dev, uint16_t value) {
  return hal_status_to_bool(hal_mcp4725_write_ex(dev, value));
}

uint16_t hal_mcp4725_output_latch(const hal_mcp4725_t *dev) {
  return dev == NULL ? 0u : dev->value;
}
#endif /* HAL_ENABLE_MCP4725 */

#endif
#endif
