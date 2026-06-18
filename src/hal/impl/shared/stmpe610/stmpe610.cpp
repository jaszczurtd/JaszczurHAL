#include "../../../hal_target.h"

#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_STM32G474) ||             \
    defined(HAL_TARGET_MOCK)

#include "../../../hal_config.h"

#if defined(HAL_ENABLE_STMPE610)

#include "../../../hal_gpio.h"
#include "../../../hal_i2c.h"
#include "../../../hal_spi.h"
#include "../../../hal_stmpe610.h"
#include "../../../hal_system.h"
#include "../hal_mutex_once.h"

#include <stddef.h>

/*
 * STMPE610 register transactions and touch-controller setup flow are based on
 * the Adafruit STMPE610 library written by Limor Fried/Ladyada for Adafruit
 * Industries. This implementation preserves that behavior while routing
 * transport, timing, and synchronization through JaszczurHAL.
 */

static hal_stmpe610_config_t
stmpe610_normalized_config(const hal_stmpe610_config_t *cfg) {
  hal_stmpe610_config_t out = hal_stmpe610_default_config();

  if (cfg != NULL) {
    out = *cfg;
  }

  if (out.i2c_addr == 0u) {
    out.i2c_addr = HAL_STMPE610_I2C_ADDR_DEFAULT;
  }

  return out;
}

hal_stmpe610_config_t hal_stmpe610_default_config(void) {
  hal_stmpe610_config_t cfg;

  cfg.transport = HAL_STMPE610_TRANSPORT_I2C;
  cfg.i2c_bus = 0u;
  cfg.i2c_addr = HAL_STMPE610_I2C_ADDR_DEFAULT;
  cfg.spi_bus = 0u;
  cfg.cs_pin = HAL_STMPE610_PIN_NONE;
  cfg.mosi_pin = HAL_STMPE610_PIN_NONE;
  cfg.miso_pin = HAL_STMPE610_PIN_NONE;
  cfg.sck_pin = HAL_STMPE610_PIN_NONE;

  return cfg;
}

hal_stmpe610_config_t hal_stmpe610_i2c_config(uint8_t bus, uint8_t addr) {
  hal_stmpe610_config_t cfg = hal_stmpe610_default_config();

  cfg.transport = HAL_STMPE610_TRANSPORT_I2C;
  cfg.i2c_bus = bus;
  cfg.i2c_addr = (addr != 0u) ? addr : HAL_STMPE610_I2C_ADDR_DEFAULT;

  return cfg;
}

hal_stmpe610_config_t hal_stmpe610_spi_config(uint8_t bus, uint8_t cs_pin) {
  hal_stmpe610_config_t cfg = hal_stmpe610_default_config();

  cfg.transport = HAL_STMPE610_TRANSPORT_SPI;
  cfg.spi_bus = bus;
  cfg.cs_pin = cs_pin;

  return cfg;
}

hal_stmpe610_config_t hal_stmpe610_soft_spi_config(uint8_t cs_pin,
                                                   uint8_t mosi_pin,
                                                   uint8_t miso_pin,
                                                   uint8_t sck_pin) {
  hal_stmpe610_config_t cfg = hal_stmpe610_default_config();

  cfg.transport = HAL_STMPE610_TRANSPORT_SOFT_SPI;
  cfg.cs_pin = cs_pin;
  cfg.mosi_pin = mosi_pin;
  cfg.miso_pin = miso_pin;
  cfg.sck_pin = sck_pin;

  return cfg;
}

static bool stmpe610_valid_config(const hal_stmpe610_config_t *cfg) {
  if (cfg == NULL) {
    return false;
  }

  switch (cfg->transport) {
  case HAL_STMPE610_TRANSPORT_I2C:
    return cfg->i2c_bus <= 1u;

  case HAL_STMPE610_TRANSPORT_SPI:
    return (cfg->spi_bus <= 1u) && (cfg->cs_pin != HAL_STMPE610_PIN_NONE);

  case HAL_STMPE610_TRANSPORT_SOFT_SPI:
    return (cfg->cs_pin != HAL_STMPE610_PIN_NONE) &&
           (cfg->mosi_pin != HAL_STMPE610_PIN_NONE) &&
           (cfg->miso_pin != HAL_STMPE610_PIN_NONE) &&
           (cfg->sck_pin != HAL_STMPE610_PIN_NONE);

  default:
    return false;
  }
}

static bool stmpe610_ensure_mutex(hal_stmpe610_t *dev) {
  return (dev != NULL) && jh_hal_mutex_create_once(&dev->mutex);
}

static bool stmpe610_ready(hal_stmpe610_t *dev) {
  return (dev != NULL) && dev->initialized && (dev->mutex != NULL);
}

static hal_spi_settings_t stmpe610_spi_settings(const hal_stmpe610_t *dev) {
  hal_spi_settings_t settings;

  settings.clock_hz = HAL_STMPE610_SPI_CLOCK_HZ;
  settings.bit_order = HAL_SPI_MSBFIRST;
  settings.data_mode = (dev != NULL) ? dev->spi_mode : HAL_SPI_MODE0;

  return settings;
}

static void stmpe610_setup_pins(hal_stmpe610_t *dev) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SPI) {
    hal_gpio_set_mode(dev->cfg.cs_pin, HAL_GPIO_OUTPUT);
    hal_gpio_write(dev->cfg.cs_pin, true);
    return;
  }

  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SOFT_SPI) {
    hal_gpio_set_mode(dev->cfg.sck_pin, HAL_GPIO_OUTPUT);
    hal_gpio_set_mode(dev->cfg.cs_pin, HAL_GPIO_OUTPUT);
    hal_gpio_set_mode(dev->cfg.mosi_pin, HAL_GPIO_OUTPUT);
    hal_gpio_set_mode(dev->cfg.miso_pin, HAL_GPIO_INPUT);
    hal_gpio_write(dev->cfg.sck_pin, false);
    hal_gpio_write(dev->cfg.cs_pin, true);
  }
}

static void stmpe610_soft_spi_out(const hal_stmpe610_t *dev, uint8_t value) {
  uint8_t mask = 0x80u;

  while (mask != 0u) {
    hal_gpio_write(dev->cfg.mosi_pin, (value & mask) != 0u);
    hal_gpio_write(dev->cfg.sck_pin, true);
    hal_gpio_write(dev->cfg.sck_pin, false);
    mask = (uint8_t)(mask >> 1u);
  }
}

static uint8_t stmpe610_soft_spi_in(const hal_stmpe610_t *dev) {
  uint8_t value = 0u;

  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    value = (uint8_t)(value << 1u);
    hal_gpio_write(dev->cfg.sck_pin, true);
    if (hal_gpio_read(dev->cfg.miso_pin)) {
      value |= 0x01u;
    }
    hal_gpio_write(dev->cfg.sck_pin, false);
  }

  return value;
}

static void stmpe610_spi_begin(const hal_stmpe610_t *dev) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SPI) {
    const hal_spi_settings_t settings = stmpe610_spi_settings(dev);
    hal_spi_lock(dev->cfg.spi_bus);
    hal_spi_begin_transaction(dev->cfg.spi_bus, &settings);
  }

  hal_gpio_write(dev->cfg.cs_pin, false);
}

static void stmpe610_spi_end(const hal_stmpe610_t *dev) {
  hal_gpio_write(dev->cfg.cs_pin, true);

  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SPI) {
    hal_spi_end_transaction(dev->cfg.spi_bus);
    hal_spi_unlock(dev->cfg.spi_bus);
  }
}

static void stmpe610_spi_out(const hal_stmpe610_t *dev, uint8_t value) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SPI) {
    (void)hal_spi_transfer(dev->cfg.spi_bus, value);
    return;
  }

  stmpe610_soft_spi_out(dev, value);
}

static uint8_t stmpe610_spi_in(const hal_stmpe610_t *dev) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SPI) {
    return hal_spi_transfer(dev->cfg.spi_bus, 0u);
  }

  return stmpe610_soft_spi_in(dev);
}

static uint8_t stmpe610_read8_unlocked(hal_stmpe610_t *dev, uint8_t reg) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_I2C) {
    uint8_t value = 0u;
    const uint8_t addr = reg;

    if (!hal_i2c_write_read_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &addr, 1u,
                                &value, 1u)) {
      return 0u;
    }

    return value;
  }

  stmpe610_spi_begin(dev);
  stmpe610_spi_out(dev, (uint8_t)(0x80u | reg));
  stmpe610_spi_out(dev, 0u);
  const uint8_t value = stmpe610_spi_in(dev);
  stmpe610_spi_end(dev);

  return value;
}

static uint16_t stmpe610_read16_unlocked(hal_stmpe610_t *dev, uint8_t reg) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_I2C) {
    uint8_t data[2] = {0u, 0u};
    const uint8_t addr = reg;

    if (!hal_i2c_write_read_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr, &addr, 1u,
                                data, sizeof(data))) {
      return 0u;
    }

    return (uint16_t)(((uint16_t)data[0] << 8u) | data[1]);
  }

  stmpe610_spi_begin(dev);
  stmpe610_spi_out(dev, (uint8_t)(0x80u | reg));
  stmpe610_spi_out(dev, 0u);
  const uint8_t high = stmpe610_spi_in(dev);
  const uint8_t low = stmpe610_spi_in(dev);
  stmpe610_spi_end(dev);

  return (uint16_t)(((uint16_t)high << 8u) | low);
}

static void stmpe610_write8_unlocked(hal_stmpe610_t *dev, uint8_t reg,
                                     uint8_t value) {
  if (dev->cfg.transport == HAL_STMPE610_TRANSPORT_I2C) {
    hal_i2c_begin_transmission_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr);
    (void)hal_i2c_write_bus(dev->cfg.i2c_bus, reg);
    (void)hal_i2c_write_bus(dev->cfg.i2c_bus, value);
    (void)hal_i2c_end_transmission_bus(dev->cfg.i2c_bus);
    return;
  }

  stmpe610_spi_begin(dev);
  stmpe610_spi_out(dev, reg);
  stmpe610_spi_out(dev, value);
  stmpe610_spi_end(dev);
}

static uint16_t stmpe610_get_version_unlocked(hal_stmpe610_t *dev) {
  const uint8_t high = stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_CHIP_ID_H);
  const uint8_t low = stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_CHIP_ID_L);

  return (uint16_t)(((uint16_t)high << 8u) | low);
}

static void stmpe610_read_data_unlocked(hal_stmpe610_t *dev, uint16_t *x,
                                        uint16_t *y, uint8_t *z) {
  uint8_t data[4];

  for (size_t i = 0u; i < sizeof(data); ++i) {
    data[i] = stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_TSC_DATA_FIFO);
  }

  *x = (uint16_t)(((uint16_t)data[0] << 4u) | (data[1] >> 4u));
  *y = (uint16_t)((((uint16_t)data[1] & 0x0Fu) << 8u) | data[2]);
  *z = data[3];
}

bool hal_stmpe610_init(hal_stmpe610_t *dev, const hal_stmpe610_config_t *cfg) {
  if (dev == NULL) {
    return false;
  }

  const hal_stmpe610_config_t effective = stmpe610_normalized_config(cfg);

  if (!stmpe610_valid_config(&effective) || !stmpe610_ensure_mutex(dev)) {
    return false;
  }

  hal_mutex_lock(dev->mutex);

  dev->cfg = effective;
  dev->initialized = false;
  dev->spi_mode = HAL_SPI_MODE0;
  stmpe610_setup_pins(dev);

  bool ok = stmpe610_get_version_unlocked(dev) == HAL_STMPE610_CHIP_ID;
  if (!ok && (dev->cfg.transport == HAL_STMPE610_TRANSPORT_SPI)) {
    dev->spi_mode = HAL_SPI_MODE1;
    ok = stmpe610_get_version_unlocked(dev) == HAL_STMPE610_CHIP_ID;
  }

  if (ok) {
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_SYS_CTRL1,
                             HAL_STMPE610_SYS_CTRL1_RESET);
    hal_delay_ms(10u);

    for (uint8_t reg = 0u; reg < 65u; ++reg) {
      (void)stmpe610_read8_unlocked(dev, reg);
    }

    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_SYS_CTRL2, 0u);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_TSC_CTRL,
                             HAL_STMPE610_TSC_CTRL_EN);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_INT_EN,
                             HAL_STMPE610_INT_EN_TOUCHDET);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_ADC_CTRL1,
                             (uint8_t)(0x6u << 4u));
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_ADC_CTRL2,
                             HAL_STMPE610_ADC_CTRL2_6_5MHZ);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_TSC_CFG,
                             (uint8_t)(HAL_STMPE610_TSC_CFG_4SAMPLE |
                                       HAL_STMPE610_TSC_CFG_DELAY_1MS |
                                       HAL_STMPE610_TSC_CFG_SETTLE_5MS));
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_TSC_FRACTION_Z, 0x06u);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_FIFO_TH, 1u);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_FIFO_STA,
                             HAL_STMPE610_FIFO_STA_RESET);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_FIFO_STA, 0u);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_TSC_I_DRIVE,
                             HAL_STMPE610_TSC_I_DRIVE_50MA);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_INT_STA, 0xFFu);
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_INT_CTRL,
                             (uint8_t)(HAL_STMPE610_INT_CTRL_POL_HIGH |
                                       HAL_STMPE610_INT_CTRL_ENABLE));
    dev->initialized = true;
  }

  hal_mutex_unlock(dev->mutex);

  if (!ok) {
    hal_stmpe610_deinit(dev);
  }

  return ok;
}

void hal_stmpe610_deinit(hal_stmpe610_t *dev) {
  if (dev == NULL) {
    return;
  }

  if (dev->mutex != NULL) {
    hal_mutex_lock(dev->mutex);
    dev->initialized = false;
    hal_mutex_unlock(dev->mutex);
    hal_mutex_destroy(dev->mutex);
  }

  dev->mutex = NULL;
}

uint8_t hal_stmpe610_read_register8(hal_stmpe610_t *dev, uint8_t reg) {
  if (!stmpe610_ready(dev)) {
    return 0u;
  }

  hal_mutex_lock(dev->mutex);
  const uint8_t value = stmpe610_read8_unlocked(dev, reg);
  hal_mutex_unlock(dev->mutex);

  return value;
}

uint16_t hal_stmpe610_read_register16(hal_stmpe610_t *dev, uint8_t reg) {
  if (!stmpe610_ready(dev)) {
    return 0u;
  }

  hal_mutex_lock(dev->mutex);
  const uint16_t value = stmpe610_read16_unlocked(dev, reg);
  hal_mutex_unlock(dev->mutex);

  return value;
}

void hal_stmpe610_write_register8(hal_stmpe610_t *dev, uint8_t reg,
                                  uint8_t value) {
  if (!stmpe610_ready(dev)) {
    return;
  }

  hal_mutex_lock(dev->mutex);
  stmpe610_write8_unlocked(dev, reg, value);
  hal_mutex_unlock(dev->mutex);
}

uint16_t hal_stmpe610_get_version(hal_stmpe610_t *dev) {
  if (!stmpe610_ready(dev)) {
    return 0u;
  }

  hal_mutex_lock(dev->mutex);
  const uint16_t version = stmpe610_get_version_unlocked(dev);
  hal_mutex_unlock(dev->mutex);

  return version;
}

bool hal_stmpe610_touched(hal_stmpe610_t *dev) {
  if (!stmpe610_ready(dev)) {
    return false;
  }

  hal_mutex_lock(dev->mutex);
  const bool touched =
      (stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_TSC_CTRL) &
       HAL_STMPE610_TSC_CTRL_STA) != 0u;
  hal_mutex_unlock(dev->mutex);

  return touched;
}

bool hal_stmpe610_buffer_empty(hal_stmpe610_t *dev) {
  if (!stmpe610_ready(dev)) {
    return true;
  }

  hal_mutex_lock(dev->mutex);
  const bool empty = (stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_FIFO_STA) &
                      HAL_STMPE610_FIFO_STA_EMPTY) != 0u;
  hal_mutex_unlock(dev->mutex);

  return empty;
}

uint8_t hal_stmpe610_buffer_size(hal_stmpe610_t *dev) {
  if (!stmpe610_ready(dev)) {
    return 0u;
  }

  hal_mutex_lock(dev->mutex);
  const uint8_t size = stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_FIFO_SIZE);
  hal_mutex_unlock(dev->mutex);

  return size;
}

void hal_stmpe610_read_data(hal_stmpe610_t *dev, uint16_t *x, uint16_t *y,
                            uint8_t *z) {
  if (x != NULL) {
    *x = 0u;
  }
  if (y != NULL) {
    *y = 0u;
  }
  if (z != NULL) {
    *z = 0u;
  }

  if (!stmpe610_ready(dev) || (x == NULL) || (y == NULL) || (z == NULL)) {
    return;
  }

  hal_mutex_lock(dev->mutex);
  stmpe610_read_data_unlocked(dev, x, y, z);
  hal_mutex_unlock(dev->mutex);
}

hal_stmpe610_point_t hal_stmpe610_get_point(hal_stmpe610_t *dev) {
  hal_stmpe610_point_t point;
  uint16_t x = 0u;
  uint16_t y = 0u;
  uint8_t z = 0u;

  point.x = 0;
  point.y = 0;
  point.z = 0;

  if (!stmpe610_ready(dev)) {
    return point;
  }

  hal_mutex_lock(dev->mutex);

  while ((stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_FIFO_STA) &
          HAL_STMPE610_FIFO_STA_EMPTY) == 0u) {
    stmpe610_read_data_unlocked(dev, &x, &y, &z);
  }

  if ((stmpe610_read8_unlocked(dev, HAL_STMPE610_REG_FIFO_STA) &
       HAL_STMPE610_FIFO_STA_EMPTY) != 0u) {
    stmpe610_write8_unlocked(dev, HAL_STMPE610_REG_INT_STA, 0xFFu);
  }

  hal_mutex_unlock(dev->mutex);

  point.x = (int16_t)x;
  point.y = (int16_t)y;
  point.z = (int16_t)z;

  return point;
}

#endif

#endif
