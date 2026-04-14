#include "../../hal_pwm.h"
#include "hal_mock.h"

static uint8_t  s_resolution = 8;
static uint32_t s_values[64] = {};

void hal_pwm_set_resolution(uint8_t bits) {
    s_resolution = bits;
}

void hal_pwm_write(uint8_t pin, uint32_t value) {
    if (pin < 64) s_values[pin] = value;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

uint32_t hal_mock_pwm_get_value(uint8_t pin) {
    return (pin < 64) ? s_values[pin] : 0;
}

uint8_t hal_mock_pwm_get_resolution(void) {
    return s_resolution;
}
