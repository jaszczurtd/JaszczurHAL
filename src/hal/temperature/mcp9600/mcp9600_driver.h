#pragma once

/**
 * @file mcp9600_driver.h
 * @brief Arduino-free MCP9600/MCP9601 thermocouple ADC driver over HAL I2C.
 */

#include "hal/core/hal_target.h"
#if (HAL_TARGET_IS_RP || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/core/hal_config.h"
#if defined(HAL_ENABLE_MCP9600) && defined(HAL_ENABLE_I2C)

#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MCP9600_I2C_ADDR_DEFAULT 0x67u

#define HAL_MCP9600_STATUS_ALERT1 0x01u
#define HAL_MCP9600_STATUS_ALERT2 0x02u
#define HAL_MCP9600_STATUS_ALERT3 0x04u
#define HAL_MCP9600_STATUS_ALERT4 0x08u
#define HAL_MCP9600_STATUS_INPUTRANGE 0x10u
#define HAL_MCP9600_STATUS_THUPDATE 0x40u
#define HAL_MCP9600_STATUS_BURST 0x80u

#define HAL_MCP9601_STATUS_OPENCIRCUIT 0x10u
#define HAL_MCP9601_STATUS_SHORTCIRCUIT 0x20u

typedef enum {
  HAL_MCP9600_TYPE_K = 0,
  HAL_MCP9600_TYPE_J,
  HAL_MCP9600_TYPE_T,
  HAL_MCP9600_TYPE_N,
  HAL_MCP9600_TYPE_S,
  HAL_MCP9600_TYPE_E,
  HAL_MCP9600_TYPE_B,
  HAL_MCP9600_TYPE_R,
} hal_mcp9600_thermocouple_type_t;

typedef enum {
  HAL_MCP9600_ADC_RESOLUTION_18 = 0,
  HAL_MCP9600_ADC_RESOLUTION_16,
  HAL_MCP9600_ADC_RESOLUTION_14,
  HAL_MCP9600_ADC_RESOLUTION_12,
} hal_mcp9600_adc_resolution_t;

typedef enum {
  HAL_MCP9600_AMBIENT_RES_0_25 = 0,
  HAL_MCP9600_AMBIENT_RES_0_125,
  HAL_MCP9600_AMBIENT_RES_0_0625,
  HAL_MCP9600_AMBIENT_RES_0_03125,
} hal_mcp9600_ambient_resolution_t;

typedef struct {
  uint8_t i2c_bus;
  uint8_t i2c_addr;
} hal_mcp9600_config_t;

typedef struct {
  hal_mcp9600_config_t cfg;
  uint8_t device_id;
  hal_mutex_t mutex;
} hal_mcp9600_t;

bool hal_mcp9600_init(hal_mcp9600_t *dev, const hal_mcp9600_config_t *cfg);
void hal_mcp9600_deinit(hal_mcp9600_t *dev);
uint8_t hal_mcp9600_device_id(hal_mcp9600_t *dev);

float hal_mcp9600_read_thermocouple(hal_mcp9600_t *dev);
float hal_mcp9600_read_ambient(hal_mcp9600_t *dev);
int32_t hal_mcp9600_read_adc(hal_mcp9600_t *dev);

void hal_mcp9600_enable(hal_mcp9600_t *dev, bool flag);
bool hal_mcp9600_enabled(hal_mcp9600_t *dev);

hal_mcp9600_thermocouple_type_t
hal_mcp9600_get_thermocouple_type(hal_mcp9600_t *dev);
void hal_mcp9600_set_thermocouple_type(hal_mcp9600_t *dev,
                                       hal_mcp9600_thermocouple_type_t type);

uint8_t hal_mcp9600_get_filter_coefficient(hal_mcp9600_t *dev);
void hal_mcp9600_set_filter_coefficient(hal_mcp9600_t *dev,
                                        uint8_t filter_count);

void hal_mcp9600_set_adc_resolution(hal_mcp9600_t *dev,
                                    hal_mcp9600_adc_resolution_t resolution);
hal_mcp9600_adc_resolution_t hal_mcp9600_get_adc_resolution(hal_mcp9600_t *dev);

void hal_mcp9600_set_ambient_resolution(
    hal_mcp9600_t *dev, hal_mcp9600_ambient_resolution_t resolution);

void hal_mcp9600_set_alert_temperature(hal_mcp9600_t *dev, uint8_t alert,
                                       float temp);
float hal_mcp9600_get_alert_temperature(hal_mcp9600_t *dev, uint8_t alert);
void hal_mcp9600_configure_alert(hal_mcp9600_t *dev, uint8_t alert,
                                 bool enabled, bool rising,
                                 bool alert_cold_junction, bool active_high,
                                 bool interrupt_mode);

uint8_t hal_mcp9600_get_status(hal_mcp9600_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_MCP9600 && HAL_ENABLE_I2C */
#endif /* supported target */
