#include "../../hal_pwm.h"
#include <Arduino.h>

void hal_pwm_set_resolution(uint8_t bits) {
    analogWriteResolution(bits);
}

void hal_pwm_write(uint8_t pin, uint32_t value) {
    analogWrite(pin, value);
}
