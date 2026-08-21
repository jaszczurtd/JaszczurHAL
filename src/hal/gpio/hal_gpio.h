#pragma once

/**
 * @file hal_gpio.h
 * @brief Hardware abstraction for general-purpose I/O pins.
 */

#include "hal/core/hal_config.h"
#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO pin direction / pull-up modes. */
typedef enum {
  HAL_GPIO_INPUT = 0,          /**< Input without pull resistor. */
  HAL_GPIO_OUTPUT = 1,         /**< Push-pull output, initial low. */
  HAL_GPIO_INPUT_PULLUP = 2,   /**< Input with internal pull-up. */
  HAL_GPIO_INPUT_PULLDOWN = 3, /**< Input with internal pull-down. */
  HAL_GPIO_OUTPUT_LOW = 4,     /**< Push-pull output, initial low. */
  HAL_GPIO_OUTPUT_HIGH = 5,    /**< Push-pull output, initial high. */
  HAL_GPIO_OUTPUT_OPEN_DRAIN =
      6, /**< Open-drain output, released/high initial state. */
  HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW = 7, /**< Open-drain output, initial low. */
  HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH =
      8, /**< Open-drain output, released/high initial state. */
} hal_gpio_mode_t;

/** @brief Interrupt trigger edge modes. */
typedef enum {
  HAL_GPIO_IRQ_FALLING = 0, /**< Trigger on falling edge. */
  HAL_GPIO_IRQ_RISING = 1,  /**< Trigger on rising edge. */
  HAL_GPIO_IRQ_CHANGE = 2,  /**< Trigger on any edge. */
} hal_gpio_irq_mode_t;

/** @brief Hardware interrupt priority levels (lower value = higher priority).
 */
typedef enum {
  HAL_IRQ_PRIORITY_HIGHEST = 0, /**< Highest (most urgent) priority. */
  HAL_IRQ_PRIORITY_HIGH = 1,    /**< Above-default priority.         */
  HAL_IRQ_PRIORITY_DEFAULT = 2, /**< Default SDK startup priority.   */
  HAL_IRQ_PRIORITY_LOW = 3,     /**< Below-default priority.         */
} hal_irq_priority_t;

/** @brief Sentinel returned when a GPIO pin has no interrupt owner. */
#define HAL_GPIO_IRQ_CORE_NONE UINT8_MAX

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
void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode);

/**
 * @brief Attach a GPIO interrupt owned by an explicit processor core.
 *
 * The operation succeeds only when the calling code is currently executing on
 * @p owner_core. Reconfiguring an interrupt already owned by another core is
 * rejected. Reconfiguration by the existing owner is allowed.
 *
 * @param pin        Pin number.
 * @param callback   Function called from ISR context.
 * @param mode       Edge trigger mode.
 * @param owner_core Required core id for registration.
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments,
 *         HAL_EUNSUPPORTED when the pin/backend cannot provide GPIO IRQs, or
 *         HAL_ESTATE when the caller or existing owner does not match
 *         @p owner_core.
 * @note Call from initialization/task context, not from an ISR.
 */
hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin, void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core);

/**
 * @brief Detach the interrupt handler from a GPIO pin.
 * @param pin Pin number.
 */
void hal_gpio_detach_interrupt(uint8_t pin);

/**
 * @brief Detach a GPIO interrupt from its owning core.
 *
 * The caller must execute on the core recorded when the interrupt was
 * attached.
 *
 * @param pin Pin number.
 * @return HAL_OK on success, HAL_ENOENT when no interrupt is attached,
 *         HAL_ESTATE when called from a non-owning core, or another
 *         argument/backend status.
 * @note Call from initialization/task context, not from an ISR.
 */
hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin);

/**
 * @brief Query the core that owns a GPIO interrupt.
 *
 * Diagnostic reads are allowed from any core.
 *
 * @param pin            Pin number.
 * @param out_owner_core Receives the owner core or HAL_GPIO_IRQ_CORE_NONE when
 *                       no interrupt is attached.
 * @return HAL_OK when an owner is present, HAL_ENOENT when detached, or an
 *         argument/backend status.
 * @note Call from task/diagnostic context, not from an ISR.
 */
hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core);

/**
 * @brief Set the NVIC priority of the GPIO interrupt bank.
 *
 * On RP2040 all GPIO pins share a single IRQ (IO_IRQ_BANK0).
 * On STM32G474 GPIO interrupts are split across EXTI0..4, EXTI9_5 and
 * EXTI15_10; this call applies the same priority to all GPIO EXTI IRQ groups.
 * On ESP32-S3 all HAL GPIO callbacks share one ESP-IDF ISR service allocated
 * on the core that attaches the first callback; priority changes must run on
 * that owner core.
 * Raising GPIO IRQ priority above other peripherals (e.g. I2C, SPI) can
 * prevent those ISRs from blocking edge counting and causing pulse loss.
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
