#ifndef JH_HAL_GPIO_COMMON_H
#define JH_HAL_GPIO_COMMON_H

#include "hal/core/hal_compiler.h"
#include "hal/gpio/hal_gpio.h"

#include <stddef.h>

/**
 * @brief Check whether the active backend accepts a GPIO identifier.
 * @param pin GPIO identifier to check.
 * @return true when digital GPIO operations accept @p pin.
 */
bool jh_hal_gpio_pin_valid(uint8_t pin);

/**
 * @brief Check whether a GPIO mode belongs to the public mode range.
 * @return true for a supported mode value.
 */
static inline bool jh_hal_gpio_mode_valid(hal_gpio_mode_t mode) {
  return mode >= HAL_GPIO_INPUT && mode <= HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

/**
 * @brief Check whether a GPIO interrupt mode belongs to the public range.
 * @param mode Interrupt mode to validate.
 * @return true for a supported interrupt mode value.
 */
static inline bool jh_hal_gpio_irq_mode_valid(hal_gpio_irq_mode_t mode) {
  return mode >= HAL_GPIO_IRQ_FALLING && mode <= HAL_GPIO_IRQ_CHANGE;
}

/**
 * @brief One pin's interrupt registration, shared by every GPIO backend.
 *
 * A backend keeps an array of these instead of bare callback pointers, so the
 * dispatch path is the same for both public handler kinds. Fill it through
 * jh_gpio_irq_slot_make() or jh_gpio_irq_slot_make_plain() and publish it with
 * jh_gpio_irq_slot_apply(); never assign the members directly.
 */
typedef struct {
  hal_gpio_irq_callback_t handler; /**< Dispatch entry, NULL when detached. */
  void *context;                   /**< Second argument passed to @p handler. */
  void (*plain)(void);             /**< Set only for a context-free callback. */
} jh_gpio_irq_slot_t;

/**
 * @brief Dispatch adapter that runs a context-free callback.
 * @param pin     Triggering pin, unused by this handler kind.
 * @param context Address of the slot's @c plain member.
 */
static inline void jh_gpio_irq_slot_run_plain(uint8_t pin, void *context) {
  (void)pin;
  void (*const *callback)(void) = (void (*const *)(void))context;
  if (callback != NULL && *callback != NULL) {
    (*callback)();
  }
}

/**
 * @brief Build a registration for a handler taking pin and context.
 * @param handler Handler to run, NULL builds a detached registration.
 * @param context Caller pointer handed to @p handler, may be NULL.
 * @return Registration value to pass to jh_gpio_irq_slot_apply().
 */
static inline jh_gpio_irq_slot_t
jh_gpio_irq_slot_make(hal_gpio_irq_callback_t handler, void *context) {
  jh_gpio_irq_slot_t slot;
  slot.handler = handler;
  slot.context = context;
  slot.plain = NULL;
  return slot;
}

/**
 * @brief Build a registration for a context-free callback.
 * @param callback Callback to run, NULL builds a detached registration.
 * @return Registration value to pass to jh_gpio_irq_slot_apply().
 */
static inline jh_gpio_irq_slot_t
jh_gpio_irq_slot_make_plain(void (*callback)(void)) {
  jh_gpio_irq_slot_t slot;
  slot.handler = NULL;
  slot.context = NULL;
  slot.plain = callback;
  if (callback != NULL) {
    slot.handler = jh_gpio_irq_slot_run_plain;
  }
  return slot;
}

/** @brief Report whether a slot currently dispatches anything. */
static inline bool jh_gpio_irq_slot_armed(const jh_gpio_irq_slot_t *slot) {
  return slot != NULL && slot->handler != NULL;
}

/**
 * @brief Detach a slot, keeping the dispatch path consistent throughout.
 *
 * Clears the handler before the context, so an interrupt racing the call sees
 * either the complete previous registration or nothing.
 *
 * @param slot Slot to clear.
 */
static inline void jh_gpio_irq_slot_clear(jh_gpio_irq_slot_t *slot) {
  HAL_ATOMIC_STORE(&slot->handler, (hal_gpio_irq_callback_t)NULL,
                   HAL_ATOMIC_RELEASE);
  slot->context = NULL;
  slot->plain = NULL;
}

/**
 * @brief Publish a registration into a live slot.
 *
 * Writes the context before the handler, so an interrupt that already observes
 * the new handler always observes its matching context. Context-free
 * registrations are re-pointed at the destination slot, which makes a saved
 * copy safe to apply back after a failed reconfiguration.
 *
 * @param slot    Destination slot owned by the backend.
 * @param request Registration built by jh_gpio_irq_slot_make() or
 *                jh_gpio_irq_slot_make_plain().
 */
static inline void jh_gpio_irq_slot_apply(jh_gpio_irq_slot_t *slot,
                                          const jh_gpio_irq_slot_t *request) {
  if (request == NULL || request->handler == NULL) {
    jh_gpio_irq_slot_clear(slot);
    return;
  }
  if (request->plain != NULL) {
    slot->plain = request->plain;
    slot->context = &slot->plain;
    /* The builtin takes a value, so the function designator needs an explicit
     * address-of here. */
    HAL_ATOMIC_STORE(&slot->handler, &jh_gpio_irq_slot_run_plain,
                     HAL_ATOMIC_RELEASE);
    return;
  }
  slot->plain = NULL;
  slot->context = request->context;
  HAL_ATOMIC_STORE(&slot->handler, request->handler, HAL_ATOMIC_RELEASE);
}

/**
 * @brief Run a slot's handler from ISR context.
 * @param slot Slot to dispatch, ignored when detached.
 * @param pin  Pin reported to the handler.
 */
static inline void jh_gpio_irq_slot_invoke(const jh_gpio_irq_slot_t *slot,
                                           uint8_t pin) {
  const hal_gpio_irq_callback_t handler =
      HAL_ATOMIC_LOAD(&slot->handler, HAL_ATOMIC_ACQUIRE);
  if (handler != NULL) {
    handler(pin, slot->context);
  }
}

/**
 * @brief Validate and store mock/backend GPIO mode state.
 * @param pin Pin to update.
 * @param mode New GPIO mode.
 * @param state Per-pin logical output state.
 * @param stored_mode Per-pin mode storage.
 * @param pin_valid Callable pin validator.
 * @return true when the pin and mode are valid and state was updated.
 */
template <typename PinValidator>
static bool jh_hal_gpio_store_mode(uint8_t pin, hal_gpio_mode_t mode,
                                   bool *state, hal_gpio_mode_t *stored_mode,
                                   PinValidator pin_valid) {
  if (!pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid pin");
    return false;
  }
  if (!jh_hal_gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return false;
  }
  if (mode == HAL_GPIO_OUTPUT || mode == HAL_GPIO_OUTPUT_LOW ||
      mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW) {
    state[pin] = false;
  } else if (mode == HAL_GPIO_OUTPUT_HIGH ||
             mode == HAL_GPIO_OUTPUT_OPEN_DRAIN ||
             mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH) {
    state[pin] = true;
  }
  stored_mode[pin] = mode;
  return true;
}

#endif
