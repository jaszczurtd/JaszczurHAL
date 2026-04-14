#include "../../hal_external_adc.h"
#include "hal_mock.h"

static float   s_adc_range = 0.1875f;
static int16_t s_raw[4]    = {};
static float   s_scaled[4] = {};

void hal_ext_adc_init(uint8_t address, float adc_range) {
    hal_ext_adc_init_bus(0, address, adc_range);
}

void hal_ext_adc_init_bus(uint8_t i2c_bus, uint8_t address, float adc_range) {
    (void)i2c_bus;
    (void)address;
    s_adc_range = adc_range;
}

int16_t hal_ext_adc_read(uint8_t channel) {
    return (channel < 4) ? s_raw[channel] : 0;
}

float hal_ext_adc_read_scaled(uint8_t channel) {
    return (channel < 4) ? s_scaled[channel] : 0.0f;
}

/* ── Mock helpers ─────────────────────────────────────────────────────── */

void hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value) {
    if (channel < 4) s_raw[channel] = value;
}

void hal_mock_ext_adc_inject_scaled(uint8_t channel, float value) {
    if (channel < 4) s_scaled[channel] = value;
}

float hal_mock_ext_adc_get_range(void) {
    return s_adc_range;
}
