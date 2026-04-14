#include "../../hal_adc.h"
#include "../../hal_sync.h"
#include "hal_mock.h"

static uint8_t s_resolution = 12;
static int s_adc_values[64] = {};
static hal_mutex_t s_adc_mutex = NULL;

static void adc_ensure_mutex(void) {
    if (s_adc_mutex == NULL) {
        s_adc_mutex = hal_mutex_create();
    }
}

void hal_adc_set_resolution(uint8_t bits) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    s_resolution = bits;
    hal_mutex_unlock(s_adc_mutex);
}

int hal_adc_read(uint8_t pin) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    int val = (pin < 64) ? s_adc_values[pin] : 0;
    hal_mutex_unlock(s_adc_mutex);
    return val;
}

// ── Mock helpers ──────────────────────────────────────────────────────────────

uint8_t hal_mock_adc_get_resolution(void) {
    return s_resolution;
}

void hal_mock_adc_inject(uint8_t pin, int value) {
    if (pin < 64) s_adc_values[pin] = value;
}
