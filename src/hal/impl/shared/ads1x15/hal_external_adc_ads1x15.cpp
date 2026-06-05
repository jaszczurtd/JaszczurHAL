#include "../../../hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474)

#include "../../../hal_config.h"
#ifdef HAL_ENABLE_EXTERNAL_ADC

#include "../../../hal_external_adc.h"

#include "ads1x15_driver.h"

#include "../../../hal_i2c.h"
#include "../../../hal_serial.h"
#include "../../../hal_sync.h"

#include <new>

alignas(ADS1115) static uint8_t s_ads_storage[sizeof(ADS1115)];
static ADS1115 *s_ads = NULL;
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

void hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range) {
    ext_adc_ensure_mutex();
    hal_mutex_lock(s_ext_adc_mutex);

    s_i2c_bus = (i2c_bus == 1u) ? 1u : 0u;
    s_adc_range = adc_range;
    s_ads = new(s_ads_storage) ADS1115(address, s_i2c_bus);

    const bool ok = s_ads->begin();
    if (!ok) {
        s_ads = NULL;
    }

    hal_mutex_unlock(s_ext_adc_mutex);

    if (!ok) {
        hal_derr("hal_ext_adc_init_bus: ADS1115 not detected at 0x%02X on I2C bus %u",
                 (unsigned)address, (unsigned)s_i2c_bus);
    }
}

int16_t hal_ext_adc_read(uint8_t channel) {
    HAL_ASSERT(s_ads != NULL, "hal_ext_adc_read: ADC not initialised, call hal_ext_adc_init() first");
    if (channel > 3u) {
        hal_derr("hal_ext_adc_read: invalid channel %u (expected 0..3)", (unsigned)channel);
        return 0;
    }

    ext_adc_ensure_mutex();
    hal_mutex_lock(s_ext_adc_mutex);

    ADS1115 *ads = s_ads;
    int16_t v = 0;
    int8_t err = ADS1X15_ERROR_I2C;
    if (ads != NULL) {
        ads->setGain(0);
        v = ads->readADC(channel);
        err = ads->getError();
    }

    hal_mutex_unlock(s_ext_adc_mutex);

    if (ads == NULL) {
        hal_derr("hal_ext_adc_read: ADC not initialised, call hal_ext_adc_init() first");
        return 0;
    }

    if (err != ADS1X15_OK) {
        hal_derr("hal_ext_adc_read: ADS1115 read failed on channel %u (error=%d)",
                 (unsigned)channel, (int)err);
        return 0;
    }

    return v;
}

float hal_ext_adc_read_scaled(uint8_t channel) {
    HAL_ASSERT(s_ads != NULL, "hal_ext_adc_read_scaled: ADC not initialised, call hal_ext_adc_init() first");
    ext_adc_ensure_mutex();
    hal_mutex_lock(s_ext_adc_mutex);
    float range = s_adc_range;
    hal_mutex_unlock(s_ext_adc_mutex);
    return (hal_ext_adc_read(channel) * range) / 1000.0f;
}

#endif /* HAL_ENABLE_EXTERNAL_ADC */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 */
