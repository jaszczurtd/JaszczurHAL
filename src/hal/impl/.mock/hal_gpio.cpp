#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_gpio.h"
#include "hal_mock.h"

#include <stddef.h>
#include <string.h>

#define MOCK_GPIO_MAX_PINS 64u
#define MOCK_GPIO_CORE_COUNT 2u
#define MOCK_GPIO_READ_SCRIPT_MAX 128u
#define MOCK_GPIO_TRACE_MAX 768u

static bool s_state[64] = {};
static hal_gpio_mode_t s_mode[64] = {};
static void (*s_callback[64])(void) = {};
static hal_gpio_irq_mode_t s_irq_mode[64] = {};
static bool s_irq_attached[64] = {};
static uint8_t s_irq_owner[64] = {};
static uint8_t s_current_core = 0u;
static bool s_read_script[MOCK_GPIO_MAX_PINS][MOCK_GPIO_READ_SCRIPT_MAX] = {};
static size_t s_read_script_len[MOCK_GPIO_MAX_PINS] = {};
static size_t s_read_script_pos[MOCK_GPIO_MAX_PINS] = {};
static hal_mock_gpio_event_t s_trace[MOCK_GPIO_TRACE_MAX] = {};
static size_t s_trace_count = 0u;

static bool gpio_pin_valid(uint8_t pin) { return pin < MOCK_GPIO_MAX_PINS; }

static bool gpio_mode_valid(hal_gpio_mode_t mode) {
  return mode >= HAL_GPIO_INPUT && mode <= HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

static bool gpio_irq_mode_valid(hal_gpio_irq_mode_t mode) {
  return mode >= HAL_GPIO_IRQ_FALLING && mode <= HAL_GPIO_IRQ_CHANGE;
}

static bool gpio_mode_is_output(hal_gpio_mode_t mode) {
  return mode == HAL_GPIO_OUTPUT || mode == HAL_GPIO_OUTPUT_LOW ||
         mode == HAL_GPIO_OUTPUT_HIGH || mode == HAL_GPIO_OUTPUT_OPEN_DRAIN ||
         mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW ||
         mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH;
}

static void hal_mock_gpio_trace_push(hal_mock_gpio_event_type_t type,
                                     uint8_t pin, int value) {
  if (s_trace_count >= MOCK_GPIO_TRACE_MAX) {
    return;
  }
  s_trace[s_trace_count].type = type;
  s_trace[s_trace_count].pin = pin;
  s_trace[s_trace_count].value = value;
  s_trace_count++;
}

/* ── "write-before-mode" antipattern: faithful latch modeling ─────────────────
 * On RP2040, pinMode(OUTPUT) calls gpio_init(), which UNCONDITIONALLY resets
 * the pin's output latch to 0 *before* enabling the driver - it re-inits the
 * pin regardless of its previous mode. So code that writes a HIGH level and
 * only then switches the pin to OUTPUT (expecting it to drive HIGH) actually
 * drives LOW: the written value is silently discarded. Direct-register Arduino
 * code (DIRECT_WRITE/DIRECT_MODE) never hit this, so it slips through ports -
 * it was the OneWire onewire_drive_high() bug.
 *
 * We model this faithfully: set_mode(OUTPUT) clobbers the latch to 0. A pure
 * GPIO-layer "violation counter" cannot reliably flag the bug (the offending
 * write often happens while the pin is already OUTPUT, and a benign drive-low
 * after a drive-high looks identical at this layer). The robust check is
 * behavioural: drive a pin and assert the resulting level via
 * hal_mock_gpio_get_state(). Because the mock now matches the hardware, ANY
 * driver test that asserts pin levels catches the antipattern. */

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid pin");
    return;
  }
  if (!gpio_mode_valid(mode)) {
    HAL_ASSERT(false, "hal_gpio_set_mode: invalid mode");
    return;
  }
  if (mode == HAL_GPIO_OUTPUT || mode == HAL_GPIO_OUTPUT_LOW ||
      mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_LOW) {
    /* Mirror gpio_init(): entering OUTPUT mode resets the latch to 0. A HIGH
     * written before this call is therefore discarded (drives LOW). */
    s_state[pin] = false;
  } else if (mode == HAL_GPIO_OUTPUT_HIGH ||
             mode == HAL_GPIO_OUTPUT_OPEN_DRAIN ||
             mode == HAL_GPIO_OUTPUT_OPEN_DRAIN_HIGH) {
    s_state[pin] = true;
  }
  s_mode[pin] = mode;
  hal_mock_gpio_trace_push(HAL_MOCK_GPIO_EVENT_SET_MODE, pin, (int)mode);
}

void hal_gpio_write(uint8_t pin, bool high) {
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_write: invalid pin");
    return;
  }
  s_state[pin] = high;
  hal_mock_gpio_trace_push(HAL_MOCK_GPIO_EVENT_WRITE, pin, high ? 1 : 0);
}

bool hal_gpio_read(uint8_t pin) {
  if (!gpio_pin_valid(pin)) {
    HAL_ASSERT(false, "hal_gpio_read: invalid pin");
    return false;
  }
  if (s_read_script_pos[pin] < s_read_script_len[pin]) {
    return s_read_script[pin][s_read_script_pos[pin]++];
  }
  return s_state[pin];
}

hal_status_t hal_gpio_attach_interrupt_ex(uint8_t pin, void (*callback)(void),
                                          hal_gpio_irq_mode_t mode,
                                          uint8_t owner_core) {
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (callback == NULL) {
    return HAL_EINVAL;
  }
  if (!gpio_irq_mode_valid(mode)) {
    return HAL_EINVAL;
  }
  if (owner_core >= MOCK_GPIO_CORE_COUNT) {
    return HAL_EINVAL;
  }
  if (s_current_core != owner_core) {
    return HAL_ESTATE;
  }
  if (s_irq_attached[pin] && s_irq_owner[pin] != owner_core) {
    return HAL_ESTATE;
  }
  s_callback[pin] = callback;
  s_irq_mode[pin] = mode;
  s_irq_owner[pin] = owner_core;
  s_irq_attached[pin] = true;
  return HAL_OK;
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  const hal_status_t status =
      hal_gpio_attach_interrupt_ex(pin, callback, mode, s_current_core);
  HAL_ASSERT(status == HAL_OK, "hal_gpio_attach_interrupt: attach failed");
}

hal_status_t hal_gpio_detach_interrupt_ex(uint8_t pin) {
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (!s_irq_attached[pin]) {
    return HAL_ENOENT;
  }
  if (s_irq_owner[pin] != s_current_core) {
    return HAL_ESTATE;
  }
  s_callback[pin] = NULL;
  s_irq_attached[pin] = false;
  s_irq_owner[pin] = HAL_GPIO_IRQ_CORE_NONE;
  return HAL_OK;
}

void hal_gpio_detach_interrupt(uint8_t pin) {
  const hal_status_t status = hal_gpio_detach_interrupt_ex(pin);
  HAL_ASSERT(status == HAL_OK || status == HAL_ENOENT,
             "hal_gpio_detach_interrupt: detach failed");
}

hal_status_t hal_gpio_get_interrupt_owner_ex(uint8_t pin,
                                             uint8_t *out_owner_core) {
  if (out_owner_core == NULL) {
    return HAL_EINVAL;
  }
  *out_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  if (!gpio_pin_valid(pin)) {
    return HAL_EINVAL;
  }
  if (!s_irq_attached[pin]) {
    return HAL_ENOENT;
  }
  *out_owner_core = s_irq_owner[pin];
  return HAL_OK;
}

static hal_irq_priority_t s_gpio_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  s_gpio_irq_priority = priority;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

bool hal_mock_gpio_get_state(uint8_t pin) {
  return gpio_pin_valid(pin) ? s_state[pin] : false;
}

bool hal_mock_gpio_is_output(uint8_t pin) {
  return gpio_pin_valid(pin) ? gpio_mode_is_output(s_mode[pin]) : false;
}

hal_gpio_mode_t hal_mock_gpio_get_mode(uint8_t pin) {
  return gpio_pin_valid(pin) ? s_mode[pin] : HAL_GPIO_INPUT;
}

void hal_mock_gpio_inject_level(uint8_t pin, bool high) {
  if (gpio_pin_valid(pin))
    s_state[pin] = high;
}

void hal_mock_gpio_trace_reset(void) { s_trace_count = 0u; }

size_t hal_mock_gpio_trace_count(void) { return s_trace_count; }

bool hal_mock_gpio_trace_get(size_t index, hal_mock_gpio_event_t *out_event) {
  if (out_event == NULL || index >= s_trace_count) {
    return false;
  }
  *out_event = s_trace[index];
  return true;
}

void hal_mock_gpio_push_read_sequence(uint8_t pin, const bool *levels,
                                      size_t len) {
  if (!gpio_pin_valid(pin)) {
    return;
  }
  s_read_script_len[pin] = 0u;
  s_read_script_pos[pin] = 0u;
  if (levels == NULL) {
    return;
  }
  if (len > MOCK_GPIO_READ_SCRIPT_MAX) {
    len = MOCK_GPIO_READ_SCRIPT_MAX;
  }
  memcpy(s_read_script[pin], levels, len * sizeof(levels[0]));
  s_read_script_len[pin] = len;
}

void hal_mock_gpio_clear_read_sequence(uint8_t pin) {
  if (gpio_pin_valid(pin)) {
    s_read_script_len[pin] = 0u;
    s_read_script_pos[pin] = 0u;
  }
}

void hal_mock_gpio_fire_interrupt(uint8_t pin) {
  if (gpio_pin_valid(pin) && s_irq_attached[pin] && s_callback[pin]) {
    const uint8_t saved_core = s_current_core;
    s_current_core = s_irq_owner[pin];
    s_callback[pin]();
    s_current_core = saved_core;
  }
}

void hal_mock_gpio_set_current_core(uint8_t core) { s_current_core = core; }

uint8_t hal_mock_gpio_get_current_core(void) { return s_current_core; }

hal_irq_priority_t hal_mock_gpio_get_irq_priority(void) {
  return s_gpio_irq_priority;
}
#endif // HAL_TARGET_IS_MOCK
