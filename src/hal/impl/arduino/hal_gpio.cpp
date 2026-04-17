#include "../../hal_gpio.h"
#include <Arduino.h>
#include <hardware/irq.h>

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

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
    // RP2040 Cortex-M0+ has 4 priority levels (top 2 bits of 8-bit field):
    //   0x00 (highest), 0x40, 0x80 (default), 0xC0 (lowest).
    static const uint8_t prio_map[] = {
        [HAL_IRQ_PRIORITY_HIGHEST] = 0x00,
        [HAL_IRQ_PRIORITY_HIGH]    = 0x40,
        [HAL_IRQ_PRIORITY_DEFAULT] = PICO_DEFAULT_IRQ_PRIORITY,  // 0x80
        [HAL_IRQ_PRIORITY_LOW]     = 0xC0,
    };
    uint8_t hw_prio = (priority <= HAL_IRQ_PRIORITY_LOW)
                    ? prio_map[priority]
                    : PICO_DEFAULT_IRQ_PRIORITY;
    irq_set_priority(IO_IRQ_BANK0, hw_prio);
}
