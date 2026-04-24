#include "../../hal_adc.h"
#include "../../hal_sync.h"

static uint8_t s_resolution = 12u;
static int s_adc_values[128] = {};
static hal_mutex_t s_adc_mutex = nullptr;

static void adc_ensure_mutex(void) {
    if (s_adc_mutex == nullptr) {
        hal_critical_section_enter();
        if (s_adc_mutex == nullptr) {
            s_adc_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }
}

void hal_adc_set_resolution(uint8_t bits) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    s_resolution = bits;
    (void)s_resolution;
    hal_mutex_unlock(s_adc_mutex);
}

int hal_adc_read(uint8_t pin) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    int val = (pin < 128u) ? s_adc_values[pin] : 0;
    hal_mutex_unlock(s_adc_mutex);
    return val;
}
