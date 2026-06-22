#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_gpio.h"
#include <Arduino.h>
#include <hardware/irq.h>

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT (-1)
#endif

static bool s_open_drain_mode[256] = {};

static bool arduino_pin_valid(uint8_t pin) {
#if defined(NUM_DIGITAL_PINS)
  return pin < NUM_DIGITAL_PINS;
#elif defined(PINS_COUNT)
  return pin < PINS_COUNT;
#else
  (void)pin;
  return true;
#endif
}

static bool gpio_mode_valid(hal_gpio_mode_t mode) {
  return mode >= HAL_GPIO_INPUT && mode <= HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

static bool gpio_irq_mode_valid(hal_gpio_irq_mode_t mode) {
  return mode >= HAL_GPIO_IRQ_FALLING && mode <= HAL_GPIO_IRQ_CHANGE;
}

static void set_open_drain(uint8_t pin, bool high) {
  if (high) {
    pinMode(pin, INPUT);
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  if (!arduino_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid pin");
    return;
  }
  if (!gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return;
  }

  s_open_drain_mode[pin] = false;
  switch (mode) {
  case HAL_GPIO_OUTPUT:
  case HAL_GPIO_OUTPUT_LOW:
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    break;
  case HAL_GPIO_OUTPUT_HIGH:
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    break;
  case HAL_GPIO_INPUT_PULLUP:
    pinMode(pin, INPUT_PULLUP);
    break;
  case HAL_GPIO_INPUT_PULLDOWN:
#if defined(INPUT_PULLDOWN)
    pinMode(pin, INPUT_PULLDOWN);
#else
    HAL_ASSERT(false, "hal_gpio_set_mode: INPUT_PULLDOWN is not supported");
    pinMode(pin, INPUT);
#endif
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN:
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH:
    s_open_drain_mode[pin] = true;
    set_open_drain(pin, true);
    break;
  case HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW:
    s_open_drain_mode[pin] = true;
    set_open_drain(pin, false);
    break;
  case HAL_GPIO_INPUT:
  default:
    pinMode(pin, INPUT);
    break;
  }
}

void hal_gpio_write(uint8_t pin, bool high) {
  if (!arduino_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_write: invalid pin");
    return;
  }
  if (s_open_drain_mode[pin]) {
    set_open_drain(pin, high);
    return;
  }
  digitalWrite(pin, high ? HIGH : LOW);
}

bool hal_gpio_read(uint8_t pin) {
  if (!arduino_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_read: invalid pin");
    return false;
  }
  return digitalRead(pin) == HIGH;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  if (!arduino_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_attach_interrupt: invalid pin");
    return;
  }
  if (callback == nullptr) {
    HAL_ASSERT(false, "hal_gpio_attach_interrupt: callback is NULL");
    return;
  }
  if (!gpio_irq_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_attach_interrupt: invalid IRQ mode");
    return;
  }
  const int irq_pin = digitalPinToInterrupt(pin);
  if (irq_pin == NOT_AN_INTERRUPT) {
    HAL_ASSERT(false, "hal_gpio_attach_interrupt: pin has no interrupt");
    return;
  }

  PinStatus arduino_mode;
  switch (mode) {
  case HAL_GPIO_IRQ_FALLING:
    arduino_mode = FALLING;
    break;
  case HAL_GPIO_IRQ_RISING:
    arduino_mode = RISING;
    break;
  default:
    arduino_mode = CHANGE;
    break;
  }
  attachInterrupt(irq_pin, callback, arduino_mode);
}

void hal_gpio_detach_interrupt(uint8_t pin) {
  if (!arduino_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_detach_interrupt: invalid pin");
    return;
  }
  const int irq_pin = digitalPinToInterrupt(pin);
  if (irq_pin == NOT_AN_INTERRUPT) {
    HAL_ASSERT(false, "hal_gpio_detach_interrupt: pin has no interrupt");
    return;
  }
  detachInterrupt(irq_pin);
}

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  // RP2040 Cortex-M0+ has 4 priority levels (top 2 bits of 8-bit field):
  //   0x00 (highest), 0x40, 0x80 (default), 0xC0 (lowest).
  static const uint8_t prio_map[] = {
      [HAL_IRQ_PRIORITY_HIGHEST] = 0x00,
      [HAL_IRQ_PRIORITY_HIGH] = 0x40,
      [HAL_IRQ_PRIORITY_DEFAULT] = PICO_DEFAULT_IRQ_PRIORITY, // 0x80
      [HAL_IRQ_PRIORITY_LOW] = 0xC0,
  };
  uint8_t hw_prio = (priority <= HAL_IRQ_PRIORITY_LOW)
                        ? prio_map[priority]
                        : PICO_DEFAULT_IRQ_PRIORITY;
  irq_set_priority(IO_IRQ_BANK0, hw_prio);
}
#endif // HAL_TARGET_IS_RP2040
