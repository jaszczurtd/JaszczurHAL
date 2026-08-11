#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_MCP23017

#include "hal/core/hal_status.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_MCP23017_I2C_ADDR_DEFAULT 0x20u
#define HAL_MCP23017_PIN_COUNT 16u

typedef enum {
  HAL_MCP23017_MODE_8_IN_8_OUT = 1,
  HAL_MCP23017_MODE_16_OUT = 2,
  HAL_MCP23017_MODE_16_IN = 3,
} hal_mcp23017_mode_t;

typedef enum {
  HAL_MCP23017_IRQ_NONE = 0,
  HAL_MCP23017_IRQ_CHANGE,
  HAL_MCP23017_IRQ_RISING,
  HAL_MCP23017_IRQ_FALLING,
  HAL_MCP23017_IRQ_HIGH,
  HAL_MCP23017_IRQ_LOW,
} hal_mcp23017_irq_mode_t;

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
  hal_mcp23017_mode_t mode;
} hal_mcp23017_config_t;

typedef struct {
  hal_mcp23017_config_t cfg;
  hal_mutex_t mutex;
  uint16_t outputs;
  uint16_t output_inverted;
  uint16_t input_inverted;
  uint16_t input_pullup;
  uint16_t irq_enabled;
  uint16_t irq_change;
  uint16_t irq_level;
  uint16_t irq_flag;
  uint16_t irq_value;
  bool initialized;
} hal_mcp23017_t;

hal_mcp23017_config_t hal_mcp23017_default_config(void);
hal_status_t hal_mcp23017_init_ex(hal_mcp23017_t *dev,
                                  const hal_mcp23017_config_t *cfg);
bool hal_mcp23017_init(hal_mcp23017_t *dev, const hal_mcp23017_config_t *cfg);
void hal_mcp23017_deinit(hal_mcp23017_t *dev);

uint8_t hal_mcp23017_input_count(const hal_mcp23017_t *dev);
uint8_t hal_mcp23017_output_count(const hal_mcp23017_t *dev);

hal_status_t hal_mcp23017_write_pin_ex(hal_mcp23017_t *dev, uint8_t pin,
                                       bool on);
bool hal_mcp23017_write_pin(hal_mcp23017_t *dev, uint8_t pin, bool on);
hal_status_t hal_mcp23017_write_all_ex(hal_mcp23017_t *dev, uint16_t value);
bool hal_mcp23017_write_all(hal_mcp23017_t *dev, uint16_t value);
uint16_t hal_mcp23017_output_latch(const hal_mcp23017_t *dev);

hal_status_t hal_mcp23017_read_pin_ex(hal_mcp23017_t *dev, uint8_t pin,
                                      bool *out_value);
bool hal_mcp23017_read_pin(hal_mcp23017_t *dev, uint8_t pin);
hal_status_t hal_mcp23017_read_all_ex(hal_mcp23017_t *dev, uint16_t *out_value);
uint16_t hal_mcp23017_read_all(hal_mcp23017_t *dev);

hal_status_t hal_mcp23017_config_input_ex(hal_mcp23017_t *dev, uint8_t pin,
                                          bool inverted, bool pullup);
bool hal_mcp23017_config_input(hal_mcp23017_t *dev, uint8_t pin, bool inverted,
                               bool pullup);
hal_status_t hal_mcp23017_config_output_ex(hal_mcp23017_t *dev, uint8_t pin,
                                           bool inverted);
bool hal_mcp23017_config_output(hal_mcp23017_t *dev, uint8_t pin,
                                bool inverted);

hal_status_t hal_mcp23017_config_irq_ex(hal_mcp23017_t *dev, uint8_t pin,
                                        hal_mcp23017_irq_mode_t mode);
hal_status_t hal_mcp23017_read_irq_ex(hal_mcp23017_t *dev, uint16_t *out_flags,
                                      uint16_t *out_captured);

#endif /* HAL_ENABLE_MCP23017 */
#ifdef __cplusplus
}
#endif
