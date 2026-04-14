#include "../../hal_adc.h"
#include "../../hal_sync.h"
#include <Arduino.h>

static hal_mutex_t s_adc_mutex = NULL;

static void adc_ensure_mutex(void) {
    if (!s_adc_mutex) {
        hal_critical_section_enter();
        if (!s_adc_mutex) {
            s_adc_mutex = hal_mutex_create();
        }
        hal_critical_section_exit();
    }

}

void hal_adc_set_resolution(uint8_t bits) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    analogReadResolution(bits);
    hal_mutex_unlock(s_adc_mutex);
}

int hal_adc_read(uint8_t pin) {
    adc_ensure_mutex();
    hal_mutex_lock(s_adc_mutex);
    int val = analogRead(pin);
    hal_mutex_unlock(s_adc_mutex);
    return val;
}
