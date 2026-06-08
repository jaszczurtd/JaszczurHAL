/*
 * MCP9600/MCP9601 transaction and register logic is modeled after the
 * Adafruit MCP9600 Arduino library by Kevin Townsend and Limor Fried for
 * Adafruit Industries. This implementation was rewritten as an Arduino-free
 * JaszczurHAL shared driver: it keeps the proven register map, device-ID
 * validation, reset/config sequence, fixed-point register scaling and alert
 * bit layout, but uses only HAL I2C and synchronization primitives.
 *
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2012, Adafruit Industries
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "../../../hal_config.h"
#if defined(HAL_ENABLE_MCP9600) && defined(HAL_ENABLE_I2C)

#include "mcp9600_driver.h"

#include "../../../hal_i2c.h"

#include <math.h>
#include <stddef.h>

#define MCP9600_HOTJUNCTION   0x00u
#define MCP9600_JUNCTIONDELTA 0x01u
#define MCP9600_COLDJUNCTION  0x02u
#define MCP9600_RAWDATAADC    0x03u
#define MCP9600_STATUS        0x04u
#define MCP9600_SENSORCONFIG  0x05u
#define MCP9600_DEVICECONFIG  0x06u
#define MCP9600_ALERTCONFIG_1 0x08u
#define MCP9600_ALERTLIMIT_1  0x10u
#define MCP9600_DEVICEID      0x20u

#define MCP9600_DEVICE_ID     0x40u
#define MCP9601_DEVICE_ID     0x41u
#define MCP9600_RESET_CONFIG  0x80u

static bool mcp9600_valid(hal_mcp9600_t *dev) {
    return dev != NULL && dev->mutex != NULL;
}

static bool mcp9600_read_register(hal_mcp9600_t *dev,
                                  uint8_t reg,
                                  uint8_t *data,
                                  uint8_t len) {
    if (!mcp9600_valid(dev) || data == NULL) {
        return false;
    }
    return hal_i2c_write_read_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr,
                                  &reg, 1u, data, len);
}

static bool mcp9600_write_register(hal_mcp9600_t *dev,
                                   uint8_t reg,
                                   const uint8_t *data,
                                   uint8_t len) {
    if (!mcp9600_valid(dev) || (len > 0u && data == NULL)) {
        return false;
    }

    hal_i2c_begin_transmission_bus(dev->cfg.i2c_bus, dev->cfg.i2c_addr);
    if (hal_i2c_write_bus(dev->cfg.i2c_bus, reg) != 1u) {
        (void)hal_i2c_end_transmission_bus(dev->cfg.i2c_bus);
        return false;
    }
    for (uint8_t i = 0; i < len; ++i) {
        if (hal_i2c_write_bus(dev->cfg.i2c_bus, data[i]) != 1u) {
            (void)hal_i2c_end_transmission_bus(dev->cfg.i2c_bus);
            return false;
        }
    }
    return hal_i2c_end_transmission_bus(dev->cfg.i2c_bus) == 0u;
}

static bool mcp9600_read_u8(hal_mcp9600_t *dev, uint8_t reg, uint8_t *value) {
    return mcp9600_read_register(dev, reg, value, 1u);
}

static bool mcp9600_write_u8(hal_mcp9600_t *dev, uint8_t reg, uint8_t value) {
    return mcp9600_write_register(dev, reg, &value, 1u);
}

bool hal_mcp9600_init(hal_mcp9600_t *dev, const hal_mcp9600_config_t *cfg) {
    if (dev == NULL || cfg == NULL) {
        return false;
    }

    dev->cfg = *cfg;
    dev->device_id = MCP9600_DEVICE_ID;
    dev->mutex = hal_mutex_create();
    if (dev->mutex == NULL) {
        return false;
    }

    hal_mutex_lock(dev->mutex);

    uint8_t id_raw[2] = {0u, 0u};
    if (!mcp9600_read_register(dev, MCP9600_DEVICEID, id_raw, 2u)) {
        hal_mutex_unlock(dev->mutex);
        hal_mcp9600_deinit(dev);
        return false;
    }

    if ((id_raw[0] != MCP9600_DEVICE_ID) && (id_raw[0] != MCP9601_DEVICE_ID)) {
        hal_mutex_unlock(dev->mutex);
        hal_mcp9600_deinit(dev);
        return false;
    }
    dev->device_id = id_raw[0];

    if (!mcp9600_write_u8(dev, MCP9600_DEVICECONFIG, MCP9600_RESET_CONFIG)) {
        hal_mutex_unlock(dev->mutex);
        hal_mcp9600_deinit(dev);
        return false;
    }

    hal_mutex_unlock(dev->mutex);
    return true;
}

void hal_mcp9600_deinit(hal_mcp9600_t *dev) {
    if (dev == NULL || dev->mutex == NULL) {
        return;
    }
    hal_mutex_destroy(dev->mutex);
    dev->mutex = NULL;
}

uint8_t hal_mcp9600_device_id(hal_mcp9600_t *dev) {
    return mcp9600_valid(dev) ? dev->device_id : 0u;
}

float hal_mcp9600_read_thermocouple(hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return NAN;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config) ||
        (config & 0x03u) != 0u) {
        hal_mutex_unlock(dev->mutex);
        return NAN;
    }

    uint8_t raw[2] = {0u, 0u};
    if (!mcp9600_read_register(dev, MCP9600_HOTJUNCTION, raw, 2u)) {
        hal_mutex_unlock(dev->mutex);
        return NAN;
    }

    hal_mutex_unlock(dev->mutex);

    int16_t therm = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return (float)therm * 0.0625f;
}

float hal_mcp9600_read_ambient(hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return NAN;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config) ||
        (config & 0x03u) != 0u) {
        hal_mutex_unlock(dev->mutex);
        return NAN;
    }

    uint8_t raw[2] = {0u, 0u};
    if (!mcp9600_read_register(dev, MCP9600_COLDJUNCTION, raw, 2u)) {
        hal_mutex_unlock(dev->mutex);
        return NAN;
    }

    hal_mutex_unlock(dev->mutex);

    int16_t cold = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return (float)cold * 0.0625f;
}

void hal_mcp9600_enable(hal_mcp9600_t *dev, bool flag) {
    if (!mcp9600_valid(dev)) {
        return;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config)) {
        hal_mutex_unlock(dev->mutex);
        return;
    }

    config &= (uint8_t)~0x03u;
    config |= flag ? 0x00u : 0x01u;
    (void)mcp9600_write_u8(dev, MCP9600_DEVICECONFIG, config);

    hal_mutex_unlock(dev->mutex);
}

bool hal_mcp9600_enabled(hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return false;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    bool ok = mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config);

    hal_mutex_unlock(dev->mutex);
    return ok && ((config & 0x03u) == 0x00u);
}

void hal_mcp9600_set_adc_resolution(hal_mcp9600_t *dev,
                                    hal_mcp9600_adc_resolution_t resolution) {
    if (!mcp9600_valid(dev)) {
        return;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config)) {
        hal_mutex_unlock(dev->mutex);
        return;
    }

    config &= (uint8_t)~(0x03u << 5);
    config |= ((uint8_t)resolution & 0x03u) << 5;
    (void)mcp9600_write_u8(dev, MCP9600_DEVICECONFIG, config);

    hal_mutex_unlock(dev->mutex);
}

hal_mcp9600_adc_resolution_t hal_mcp9600_get_adc_resolution(
    hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return HAL_MCP9600_ADC_RESOLUTION_18;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    bool ok = mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config);

    hal_mutex_unlock(dev->mutex);
    if (!ok) {
        return HAL_MCP9600_ADC_RESOLUTION_18;
    }
    return (hal_mcp9600_adc_resolution_t)((config >> 5) & 0x03u);
}

int32_t hal_mcp9600_read_adc(hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return 0;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t raw[3] = {0u, 0u, 0u};
    bool ok = mcp9600_read_register(dev, MCP9600_RAWDATAADC, raw, 3u);

    hal_mutex_unlock(dev->mutex);
    if (!ok) {
        return 0;
    }

    int32_t reading = ((int32_t)raw[0] << 16) |
                      ((int32_t)raw[1] << 8) |
                      raw[2];
    if ((reading & 0x800000) != 0) {
        reading |= (int32_t)0xFF000000;
    }
    return reading;
}

hal_mcp9600_thermocouple_type_t hal_mcp9600_get_thermocouple_type(
    hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return HAL_MCP9600_TYPE_K;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t sensor_config = 0u;
    bool ok = mcp9600_read_u8(dev, MCP9600_SENSORCONFIG, &sensor_config);

    hal_mutex_unlock(dev->mutex);
    if (!ok) {
        return HAL_MCP9600_TYPE_K;
    }
    return (hal_mcp9600_thermocouple_type_t)((sensor_config >> 4) & 0x07u);
}

void hal_mcp9600_set_thermocouple_type(
    hal_mcp9600_t *dev,
    hal_mcp9600_thermocouple_type_t type) {
    if (!mcp9600_valid(dev)) {
        return;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t sensor_config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_SENSORCONFIG, &sensor_config)) {
        hal_mutex_unlock(dev->mutex);
        return;
    }

    sensor_config &= (uint8_t)~(0x07u << 4);
    sensor_config |= ((uint8_t)type & 0x07u) << 4;
    (void)mcp9600_write_u8(dev, MCP9600_SENSORCONFIG, sensor_config);

    hal_mutex_unlock(dev->mutex);
}

uint8_t hal_mcp9600_get_filter_coefficient(hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return 0u;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t sensor_config = 0u;
    bool ok = mcp9600_read_u8(dev, MCP9600_SENSORCONFIG, &sensor_config);

    hal_mutex_unlock(dev->mutex);
    return ok ? (sensor_config & 0x07u) : 0u;
}

void hal_mcp9600_set_filter_coefficient(hal_mcp9600_t *dev,
                                        uint8_t filter_count) {
    if (!mcp9600_valid(dev)) {
        return;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t sensor_config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_SENSORCONFIG, &sensor_config)) {
        hal_mutex_unlock(dev->mutex);
        return;
    }

    sensor_config &= (uint8_t)~0x07u;
    sensor_config |= filter_count & 0x07u;
    (void)mcp9600_write_u8(dev, MCP9600_SENSORCONFIG, sensor_config);

    hal_mutex_unlock(dev->mutex);
}

float hal_mcp9600_get_alert_temperature(hal_mcp9600_t *dev, uint8_t alert) {
    if (!mcp9600_valid(dev) || alert < 1u || alert > 4u) {
        return NAN;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t raw[2] = {0u, 0u};
    bool ok = mcp9600_read_register(dev,
                                    (uint8_t)(MCP9600_ALERTLIMIT_1 + alert - 1u),
                                    raw,
                                    2u);

    hal_mutex_unlock(dev->mutex);
    if (!ok) {
        return NAN;
    }

    int16_t therm = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return (float)therm * 0.0625f;
}

void hal_mcp9600_set_alert_temperature(hal_mcp9600_t *dev,
                                       uint8_t alert,
                                       float temp) {
    if (!mcp9600_valid(dev) || alert < 1u || alert > 4u) {
        return;
    }

    int16_t therm = (int16_t)(temp / 0.0625f);
    uint8_t raw[2] = {
        (uint8_t)((therm >> 8) & 0xFF),
        (uint8_t)(therm & 0xFF),
    };

    hal_mutex_lock(dev->mutex);
    (void)mcp9600_write_register(dev,
                                 (uint8_t)(MCP9600_ALERTLIMIT_1 + alert - 1u),
                                 raw,
                                 2u);
    hal_mutex_unlock(dev->mutex);
}

void hal_mcp9600_configure_alert(hal_mcp9600_t *dev,
                                 uint8_t alert,
                                 bool enabled,
                                 bool rising,
                                 bool alert_cold_junction,
                                 bool active_high,
                                 bool interrupt_mode) {
    if (!mcp9600_valid(dev) || alert < 1u || alert > 4u) {
        return;
    }

    uint8_t c = 0u;
    if (enabled) {
        c |= 0x01u;
    }
    if (interrupt_mode) {
        c |= 0x02u;
    }
    if (active_high) {
        c |= 0x04u;
    }
    if (rising) {
        c |= 0x08u;
    }
    if (alert_cold_junction) {
        c |= 0x10u;
    }

    hal_mutex_lock(dev->mutex);
    (void)mcp9600_write_u8(dev, (uint8_t)(MCP9600_ALERTCONFIG_1 + alert - 1u), c);
    hal_mutex_unlock(dev->mutex);
}

uint8_t hal_mcp9600_get_status(hal_mcp9600_t *dev) {
    if (!mcp9600_valid(dev)) {
        return 0u;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t status = 0u;
    bool ok = mcp9600_read_u8(dev, MCP9600_STATUS, &status);

    hal_mutex_unlock(dev->mutex);
    return ok ? status : 0u;
}

void hal_mcp9600_set_ambient_resolution(
    hal_mcp9600_t *dev,
    hal_mcp9600_ambient_resolution_t resolution) {
    if (!mcp9600_valid(dev)) {
        return;
    }
    hal_mutex_lock(dev->mutex);

    uint8_t config = 0u;
    if (!mcp9600_read_u8(dev, MCP9600_DEVICECONFIG, &config)) {
        hal_mutex_unlock(dev->mutex);
        return;
    }

    /* MCP9600 datasheet, Device Configuration register bit7:
     * 0 = 0.0625 C, 1 = 0.25 C. Keep ADC resolution bits [6:5] untouched. */
    config &= (uint8_t)~0x80u;
    if (resolution == HAL_MCP9600_AMBIENT_RES_0_25) {
        config |= 0x80u;
    }
    (void)mcp9600_write_u8(dev, MCP9600_DEVICECONFIG, config);

    hal_mutex_unlock(dev->mutex);
}

#endif /* HAL_ENABLE_MCP9600 && HAL_ENABLE_I2C */
#endif /* supported target */
