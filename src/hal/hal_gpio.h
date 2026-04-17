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

/** @brief Hardware interrupt priority levels (lower value = higher priority). */
typedef enum {
    HAL_IRQ_PRIORITY_HIGHEST = 0,    /**< Highest (most urgent) priority. */
    HAL_IRQ_PRIORITY_HIGH    = 1,    /**< Above-default priority.         */
    HAL_IRQ_PRIORITY_DEFAULT = 2,    /**< Default SDK startup priority.   */
    HAL_IRQ_PRIORITY_LOW     = 3,    /**< Below-default priority.         */
} hal_irq_priority_t;

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

/**
 * @brief Set the NVIC priority of the GPIO interrupt bank.
 *
 * On RP2040 all GPIO pins share a single IRQ (IO_IRQ_BANK0).  Raising its
 * priority above other peripherals (e.g. I2C, SPI) prevents those ISRs
 * from blocking GPIO edge counting and causing pulse loss.
 *
 * Call this @b after hal_gpio_attach_interrupt().
 *
 * @param priority  Desired priority level.
 *
 * @note On platforms without configurable IRQ priorities this is a no-op.
 */
void hal_gpio_set_irq_priority(hal_irq_priority_t priority);

#ifdef __cplusplus
}
#endif
