#ifndef JASZCZUR_HAL_STMPE610_H
#define JASZCZUR_HAL_STMPE610_H

#include "hal_status.h"
#include "hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAL_ENABLE_STMPE610

#define HAL_STMPE610_I2C_ADDR_DEFAULT 0x41u
#define HAL_STMPE610_CHIP_ID 0x0811u
#define HAL_STMPE610_SPI_CLOCK_HZ 1000000ul
#define HAL_STMPE610_PIN_NONE 0xFFu

#define HAL_STMPE610_REG_CHIP_ID_H 0x00u
#define HAL_STMPE610_REG_CHIP_ID_L 0x01u
#define HAL_STMPE610_REG_SYS_CTRL1 0x03u
#define HAL_STMPE610_SYS_CTRL1_RESET 0x02u
#define HAL_STMPE610_REG_SYS_CTRL2 0x04u

#define HAL_STMPE610_REG_INT_CTRL 0x09u
#define HAL_STMPE610_INT_CTRL_POL_HIGH 0x04u
#define HAL_STMPE610_INT_CTRL_POL_LOW 0x00u
#define HAL_STMPE610_INT_CTRL_EDGE 0x02u
#define HAL_STMPE610_INT_CTRL_LEVEL 0x00u
#define HAL_STMPE610_INT_CTRL_ENABLE 0x01u
#define HAL_STMPE610_INT_CTRL_DISABLE 0x00u

#define HAL_STMPE610_REG_INT_EN 0x0Au
#define HAL_STMPE610_INT_EN_TOUCHDET 0x01u
#define HAL_STMPE610_INT_EN_FIFOTH 0x02u
#define HAL_STMPE610_INT_EN_FIFOOF 0x04u
#define HAL_STMPE610_INT_EN_FIFOFULL 0x08u
#define HAL_STMPE610_INT_EN_FIFOEMPTY 0x10u
#define HAL_STMPE610_INT_EN_ADC 0x40u
#define HAL_STMPE610_INT_EN_GPIO 0x80u

#define HAL_STMPE610_REG_INT_STA 0x0Bu
#define HAL_STMPE610_INT_STA_TOUCHDET 0x01u

#define HAL_STMPE610_REG_GPIO_SET_PIN 0x10u
#define HAL_STMPE610_REG_GPIO_CLR_PIN 0x11u
#define HAL_STMPE610_REG_GPIO_DIR 0x13u
#define HAL_STMPE610_REG_GPIO_ALT_FUNCT 0x17u

#define HAL_STMPE610_REG_ADC_CTRL1 0x20u
#define HAL_STMPE610_ADC_CTRL1_12BIT 0x08u
#define HAL_STMPE610_ADC_CTRL1_10BIT 0x00u
#define HAL_STMPE610_REG_ADC_CTRL2 0x21u
#define HAL_STMPE610_ADC_CTRL2_1_625MHZ 0x00u
#define HAL_STMPE610_ADC_CTRL2_3_25MHZ 0x01u
#define HAL_STMPE610_ADC_CTRL2_6_5MHZ 0x02u

#define HAL_STMPE610_REG_TSC_CTRL 0x40u
#define HAL_STMPE610_TSC_CTRL_EN 0x01u
#define HAL_STMPE610_TSC_CTRL_XYZ 0x00u
#define HAL_STMPE610_TSC_CTRL_XY 0x02u
#define HAL_STMPE610_TSC_CTRL_STA 0x80u

#define HAL_STMPE610_REG_TSC_CFG 0x41u
#define HAL_STMPE610_TSC_CFG_1SAMPLE 0x00u
#define HAL_STMPE610_TSC_CFG_2SAMPLE 0x40u
#define HAL_STMPE610_TSC_CFG_4SAMPLE 0x80u
#define HAL_STMPE610_TSC_CFG_8SAMPLE 0xC0u
#define HAL_STMPE610_TSC_CFG_DELAY_10US 0x00u
#define HAL_STMPE610_TSC_CFG_DELAY_50US 0x08u
#define HAL_STMPE610_TSC_CFG_DELAY_100US 0x10u
#define HAL_STMPE610_TSC_CFG_DELAY_500US 0x18u
#define HAL_STMPE610_TSC_CFG_DELAY_1MS 0x20u
#define HAL_STMPE610_TSC_CFG_DELAY_5MS 0x28u
#define HAL_STMPE610_TSC_CFG_DELAY_10MS 0x30u
#define HAL_STMPE610_TSC_CFG_DELAY_50MS 0x38u
#define HAL_STMPE610_TSC_CFG_SETTLE_10US 0x00u
#define HAL_STMPE610_TSC_CFG_SETTLE_100US 0x01u
#define HAL_STMPE610_TSC_CFG_SETTLE_500US 0x02u
#define HAL_STMPE610_TSC_CFG_SETTLE_1MS 0x03u
#define HAL_STMPE610_TSC_CFG_SETTLE_5MS 0x04u
#define HAL_STMPE610_TSC_CFG_SETTLE_10MS 0x05u
#define HAL_STMPE610_TSC_CFG_SETTLE_50MS 0x06u
#define HAL_STMPE610_TSC_CFG_SETTLE_100MS 0x07u

#define HAL_STMPE610_REG_FIFO_TH 0x4Au
#define HAL_STMPE610_REG_FIFO_STA 0x4Bu
#define HAL_STMPE610_REG_FIFO_SIZE 0x4Cu
#define HAL_STMPE610_FIFO_STA_RESET 0x01u
#define HAL_STMPE610_FIFO_STA_THTRIG 0x10u
#define HAL_STMPE610_FIFO_STA_EMPTY 0x20u
#define HAL_STMPE610_FIFO_STA_FULL 0x40u
#define HAL_STMPE610_FIFO_STA_OFLOW 0x80u

#define HAL_STMPE610_REG_TSC_DATA_X 0x4Du
#define HAL_STMPE610_REG_TSC_DATA_Y 0x4Fu
#define HAL_STMPE610_REG_TSC_FRACTION_Z 0x56u
#define HAL_STMPE610_REG_TSC_I_DRIVE 0x58u
#define HAL_STMPE610_TSC_I_DRIVE_20MA 0x00u
#define HAL_STMPE610_TSC_I_DRIVE_50MA 0x01u
#define HAL_STMPE610_REG_TSC_DATA_FIFO 0xD7u

typedef enum {
  HAL_STMPE610_TRANSPORT_I2C = 0,
  HAL_STMPE610_TRANSPORT_SPI = 1,
  HAL_STMPE610_TRANSPORT_SOFT_SPI = 2
} hal_stmpe610_transport_t;

typedef struct {
  hal_stmpe610_transport_t transport;
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  uint8_t spi_bus;
  uint8_t cs_pin;
  uint8_t mosi_pin;
  uint8_t miso_pin;
  uint8_t sck_pin;
} hal_stmpe610_config_t;

typedef struct {
  hal_stmpe610_config_t cfg;
  bool initialized;
  hal_mutex_t mutex;
  uint8_t spi_mode;
} hal_stmpe610_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;
} hal_stmpe610_point_t;

hal_stmpe610_config_t hal_stmpe610_default_config(void);
hal_stmpe610_config_t hal_stmpe610_i2c_config(uint8_t bus, uint8_t addr);
hal_stmpe610_config_t hal_stmpe610_spi_config(uint8_t bus, uint8_t cs_pin);
hal_stmpe610_config_t hal_stmpe610_soft_spi_config(uint8_t cs_pin,
                                                   uint8_t mosi_pin,
                                                   uint8_t miso_pin,
                                                   uint8_t sck_pin);

hal_status_t hal_stmpe610_init_ex(hal_stmpe610_t *dev,
                                  const hal_stmpe610_config_t *cfg);
bool hal_stmpe610_init(hal_stmpe610_t *dev, const hal_stmpe610_config_t *cfg);
void hal_stmpe610_deinit(hal_stmpe610_t *dev);

uint16_t hal_stmpe610_get_version(hal_stmpe610_t *dev);
bool hal_stmpe610_touched(hal_stmpe610_t *dev);
bool hal_stmpe610_buffer_empty(hal_stmpe610_t *dev);
uint8_t hal_stmpe610_buffer_size(hal_stmpe610_t *dev);
hal_status_t hal_stmpe610_read_data_ex(hal_stmpe610_t *dev, uint16_t *x,
                                       uint16_t *y, uint8_t *z);
void hal_stmpe610_read_data(hal_stmpe610_t *dev, uint16_t *x, uint16_t *y,
                            uint8_t *z);
hal_stmpe610_point_t hal_stmpe610_get_point(hal_stmpe610_t *dev);

uint8_t hal_stmpe610_read_register8(hal_stmpe610_t *dev, uint8_t reg);
uint16_t hal_stmpe610_read_register16(hal_stmpe610_t *dev, uint8_t reg);
void hal_stmpe610_write_register8(hal_stmpe610_t *dev, uint8_t reg,
                                  uint8_t value);

#endif

#ifdef __cplusplus
}
#endif

#endif
