#include "../../hal_config.h"
#ifndef HAL_DISABLE_EXTERNAL_ADC

#include "../../hal_external_adc.h"
#include "../../hal_i2c.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "drivers/ADS1X15/ADS1X15.h"
#include <Wire.h>

/* Address placeholder - overwritten by hal_ext_adc_init(). */
static ADS1115 ads_instance(0);
static ADS1115 *ads = NULL;
static float s_adc_range = 1.0f;
static uint8_t s_i2c_bus = 0;
static hal_mutex_t s_ext_adc_mutex = NULL;

static void ext_adc_ensure_mutex(void) {
    if (!s_ext_adc_mutex) {
        hal_critical_section_enter();
        if (!s_ext_adc_mutex) {
            s_ext_adc_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_ext_adc_init(uint8_t address, float adc_range) {
    hal_ext_adc_init_bus(0, address, adc_range);
}

static TwoWire *ext_adc_wire_from_bus(uint8_t bus) {
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    return bus == 1 ? &Wire1 : &Wire;
#else
    (void)bus;
    return &Wire;
#endif
}

void hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range) {
    s_i2c_bus = (i2c_bus == 1) ? 1 : 0;
    ads_instance = ADS1115(address, ext_adc_wire_from_bus(s_i2c_bus));
    s_adc_range = adc_range;

    hal_i2c_lock_bus(s_i2c_bus);
    const bool ok = ads_instance.begin();
    hal_i2c_unlock_bus(s_i2c_bus);

    if (!ok) {
        ads = NULL;
        hal_derr("hal_ext_adc_init_bus: ADS1115 not detected at 0x%02X on I2C bus %u",
                 (unsigned)address, (unsigned)s_i2c_bus);
        return;
    }

    ads = &ads_instance;
}

int16_t hal_ext_adc_read(uint8_t channel) {
    HAL_ASSERT(ads != NULL, "hal_ext_adc_read: ADC not initialised, call hal_ext_adc_init() first");
    if (channel > 3u) {
        hal_derr("hal_ext_adc_read: invalid channel %u (expected 0..3)", (unsigned)channel);
        return 0;
    }

    ext_adc_ensure_mutex();
    hal_mutex_lock(s_ext_adc_mutex);
    uint8_t bus = s_i2c_bus;
    hal_i2c_lock_bus(bus);
    ads->setGain(0);
    int16_t v = ads->readADC(channel);
    int8_t err = ads->getError();
    hal_i2c_unlock_bus(bus);
    hal_mutex_unlock(s_ext_adc_mutex);

    if (err != ADS1X15_OK) {
        hal_derr("hal_ext_adc_read: ADS1115 read failed on channel %u (error=%d)",
                 (unsigned)channel, (int)err);
        return 0;
    }

    return v;
}

float hal_ext_adc_read_scaled(uint8_t channel) {
    HAL_ASSERT(ads != NULL, "hal_ext_adc_read_scaled: ADC not initialised, call hal_ext_adc_init() first");
    ext_adc_ensure_mutex();
    hal_mutex_lock(s_ext_adc_mutex);
    float range = s_adc_range;
    hal_mutex_unlock(s_ext_adc_mutex);
    return (hal_ext_adc_read(channel) * range) / 1000.0f;
}


#endif /* HAL_DISABLE_EXTERNAL_ADC */
