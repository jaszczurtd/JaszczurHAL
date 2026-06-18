#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_gpio.h"
#include "hal_mock.h"

#include <stddef.h>
#include <string.h>

#define MOCK_GPIO_MAX_PINS 64u
#define MOCK_GPIO_READ_SCRIPT_MAX 128u
#define MOCK_GPIO_TRACE_MAX 768u

static bool s_state[64] = {};
static hal_gpio_mode_t s_mode[64] = {};
static void (*s_callback[64])(void) = {};
static hal_gpio_irq_mode_t s_irq_mode[64] = {};
static bool s_read_script[MOCK_GPIO_MAX_PINS][MOCK_GPIO_READ_SCRIPT_MAX] = {};
static size_t s_read_script_len[MOCK_GPIO_MAX_PINS] = {};
static size_t s_read_script_pos[MOCK_GPIO_MAX_PINS] = {};
static hal_mock_gpio_event_t s_trace[MOCK_GPIO_TRACE_MAX] = {};
static size_t s_trace_count = 0u;

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
  if (pin >= 64)
    return;
  if (mode == HAL_GPIO_OUTPUT) {
    /* Mirror gpio_init(): entering OUTPUT mode resets the latch to 0. A HIGH
     * written before this call is therefore discarded (drives LOW). */
    s_state[pin] = false;
  }
  s_mode[pin] = mode;
  hal_mock_gpio_trace_push(HAL_MOCK_GPIO_EVENT_SET_MODE, pin, (int)mode);
}

void hal_gpio_write(uint8_t pin, bool high) {
  if (pin < 64) {
    s_state[pin] = high;
    hal_mock_gpio_trace_push(HAL_MOCK_GPIO_EVENT_WRITE, pin, high ? 1 : 0);
  }
}

bool hal_gpio_read(uint8_t pin) {
  if (pin >= MOCK_GPIO_MAX_PINS) {
    return false;
  }
  if (s_read_script_pos[pin] < s_read_script_len[pin]) {
    return s_read_script[pin][s_read_script_pos[pin]++];
  }
  return s_state[pin];
}

void hal_gpio_attach_interrupt(uint8_t pin, void (*callback)(void),
                               hal_gpio_irq_mode_t mode) {
  if (pin < 64) {
    s_callback[pin] = callback;
    s_irq_mode[pin] = mode;
  }
}

void hal_gpio_detach_interrupt(uint8_t pin) {
  if (pin < 64) {
    s_callback[pin] = NULL;
  }
}

static hal_irq_priority_t s_gpio_irq_priority = HAL_IRQ_PRIORITY_DEFAULT;

void hal_gpio_set_irq_priority(hal_irq_priority_t priority) {
  s_gpio_irq_priority = priority;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

bool hal_mock_gpio_get_state(uint8_t pin) {
  return (pin < 64) ? s_state[pin] : false;
}

bool hal_mock_gpio_is_output(uint8_t pin) {
  return (pin < 64) ? (s_mode[pin] == HAL_GPIO_OUTPUT) : false;
}

hal_gpio_mode_t hal_mock_gpio_get_mode(uint8_t pin) {
  return (pin < 64) ? s_mode[pin] : HAL_GPIO_INPUT;
}

void hal_mock_gpio_inject_level(uint8_t pin, bool high) {
  if (pin < 64)
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
  if (pin >= MOCK_GPIO_MAX_PINS) {
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
  if (pin < MOCK_GPIO_MAX_PINS) {
    s_read_script_len[pin] = 0u;
    s_read_script_pos[pin] = 0u;
  }
}

void hal_mock_gpio_fire_interrupt(uint8_t pin) {
  if (pin < 64 && s_callback[pin]) {
    s_callback[pin]();
  }
}

hal_irq_priority_t hal_mock_gpio_get_irq_priority(void) {
  return s_gpio_irq_priority;
}
#endif // HAL_TARGET_IS_MOCK
