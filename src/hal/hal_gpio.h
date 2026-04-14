#pragma once

/**
 * @file hal_gpio.h
 * @brief Hardware abstraction for general-purpose I/O pins.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO pin direction / pull-up modes. */
typedef enum {
    HAL_GPIO_INPUT        = 0,   /**< Input without pull resistor. */
    HAL_GPIO_OUTPUT       = 1,   /**< Output. */
    HAL_GPIO_INPUT_PULLUP = 2,   /**< Input with internal pull-up. */
} hal_gpio_mode_t;

/** @brief Interrupt trigger edge modes. */
typedef enum {
    HAL_GPIO_IRQ_FALLING = 0,    /**< Trigger on falling edge. */
    HAL_GPIO_IRQ_RISING  = 1,    /**< Trigger on rising edge. */
    HAL_GPIO_IRQ_CHANGE  = 2,    /**< Trigger on any edge. */
} hal_gpio_irq_mode_t;

/**
 * @brief Configure a GPIO pin mode.
 * @param pin  Pin number.
 * @param mode Direction / pull-up mode.
 */
void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode);

/**
 * @brief Write a digital value to a GPIO pin.
 * @param pin  Pin number.
 * @param high true = HIGH, false = LOW.
 */
void hal_gpio_write(uint8_t pin, bool high);

/**
 * @brief Read the digital state of a GPIO pin.
 * @param pin Pin number.
 * @return true if HIGH, false if LOW.
 */
bool hal_gpio_read(uint8_t pin);

/**
 * @brief Attach an interrupt handler to a GPIO pin.
 * @param pin      Pin number.
 * @param callback Function called on interrupt (must be ISR-safe).
 * @param mode     Edge trigger mode.
 */
void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void), hal_gpio_irq_mode_t mode);

#ifdef __cplusplus
}
#endif
