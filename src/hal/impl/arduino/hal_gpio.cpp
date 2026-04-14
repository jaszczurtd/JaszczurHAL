#include "../../hal_gpio.h"
#include <Arduino.h>

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
    switch (mode) {
        case HAL_GPIO_OUTPUT:       pinMode(pin, OUTPUT);       break;
        case HAL_GPIO_INPUT_PULLUP: pinMode(pin, INPUT_PULLUP); break;
        default:                    pinMode(pin, INPUT);        break;
    }
}

void hal_gpio_write(uint8_t pin, bool high) {
    digitalWrite(pin, high ? HIGH : LOW);
}

bool hal_gpio_read(uint8_t pin) {
    return digitalRead(pin) == HIGH;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode) {
    PinStatus arduino_mode;
    switch (mode) {
        case HAL_GPIO_IRQ_FALLING: arduino_mode = FALLING; break;
        case HAL_GPIO_IRQ_RISING:  arduino_mode = RISING;  break;
        default:                   arduino_mode = CHANGE;  break;
    }
    attachInterrupt(digitalPinToInterrupt(pin), callback, arduino_mode);
}
