#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_system.h>
#include <hal/usb/hal_usb.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Context-aware GPIO interrupts on real silicon.
 *
 * Edges come from switching a pin's internal pull between down and up, so the
 * probe never drives a pad and stays safe on a board with unknown wiring. The
 * pin keeps its input direction the whole time; only the weak pull moves.
 */

#define PIN_CTX_A 2u
#define PIN_CTX_B 3u
#define PIN_PLAIN 4u
#define PIN_SWAP 5u

#define SETTLE_MS 3u
#define CHECK_COUNT 16u

typedef struct {
  volatile uint32_t hits;
  volatile uint8_t last_pin;
} irq_probe_t;

static irq_probe_t s_probe_a;
static irq_probe_t s_probe_b;
static irq_probe_t s_probe_swap;
static volatile uint32_t s_plain_hits;

static uint8_t s_response[512];
static size_t s_response_length;
static size_t s_response_offset;

static uint32_t s_failed_mask;
static uint8_t s_check_index;
static hal_status_t s_attach_status;
static hal_status_t s_owner_status;
static uint8_t s_owner_core;
static hal_status_t s_detached_status;

/* One handler for every context-aware pin: the whole point is that it tells
 * the instances apart without a per-pin trampoline. */
static void ctx_isr(uint8_t pin, void *context) {
  irq_probe_t *probe = (irq_probe_t *)context;
  probe->hits++;
  probe->last_pin = pin;
}

static void plain_isr(void) { s_plain_hits++; }

static void release_pin(uint8_t pin) {
  hal_gpio_set_mode(pin, HAL_GPIO_INPUT_PULLUP);
  hal_delay_ms(SETTLE_MS);
}

/* Pull down, then up: one rising edge per call. */
static void pulse_pin(uint8_t pin) {
  hal_gpio_set_mode(pin, HAL_GPIO_INPUT_PULLDOWN);
  hal_delay_ms(SETTLE_MS);
  hal_gpio_set_mode(pin, HAL_GPIO_INPUT_PULLUP);
  hal_delay_ms(SETTLE_MS);
}

static void check(bool passed) {
  if (!passed && s_check_index < 32u) {
    s_failed_mask |= (uint32_t)1u << s_check_index;
  }
  s_check_index++;
}

static void reset_counters(void) {
  s_probe_a.hits = 0u;
  s_probe_a.last_pin = UINT8_MAX;
  s_probe_b.hits = 0u;
  s_probe_b.last_pin = UINT8_MAX;
  s_probe_swap.hits = 0u;
  s_probe_swap.last_pin = UINT8_MAX;
  s_plain_hits = 0u;
}

static hal_status_t attach_all(void) {
  hal_status_t status = hal_gpio_attach_interrupt_ctx(
      PIN_CTX_A, ctx_isr, &s_probe_a, HAL_GPIO_IRQ_RISING);
  if (status == HAL_OK) {
    status = hal_gpio_attach_interrupt_ctx(PIN_CTX_B, ctx_isr, &s_probe_b,
                                           HAL_GPIO_IRQ_RISING);
  }
  if (status == HAL_OK) {
    status = hal_gpio_attach_interrupt_ctx(PIN_SWAP, ctx_isr, &s_probe_swap,
                                           HAL_GPIO_IRQ_RISING);
  }
  if (status == HAL_OK) {
    hal_gpio_attach_interrupt(PIN_PLAIN, plain_isr, HAL_GPIO_IRQ_RISING);
  }
  return status;
}

/* Each context reaches its own probe and reports its own pin. */
static void run_routing_phase(void) {
  const uint32_t plain_before = s_plain_hits;
  pulse_pin(PIN_CTX_A);
  check(s_probe_a.hits > 0u);
  check(s_probe_a.last_pin == PIN_CTX_A);
  check(s_probe_b.hits == 0u);
  check(s_plain_hits == plain_before);

  const uint32_t a_hits = s_probe_a.hits;
  pulse_pin(PIN_CTX_B);
  check(s_probe_b.hits > 0u);
  check(s_probe_b.last_pin == PIN_CTX_B);
  check(s_probe_a.hits == a_hits);

  const uint32_t b_hits = s_probe_b.hits;
  pulse_pin(PIN_PLAIN);
  check(s_plain_hits > plain_before);
  check(s_probe_a.hits == a_hits);
  check(s_probe_b.hits == b_hits);
}

/* Both handler kinds replace each other on one pin. */
static void run_swap_phase(void) {
  pulse_pin(PIN_SWAP);
  const uint32_t swap_ctx = s_probe_swap.hits;
  const uint32_t plain_before = s_plain_hits;
  check(swap_ctx > 0u);

  hal_gpio_attach_interrupt(PIN_SWAP, plain_isr, HAL_GPIO_IRQ_RISING);
  pulse_pin(PIN_SWAP);
  check(s_plain_hits > plain_before && s_probe_swap.hits == swap_ctx);

  (void)hal_gpio_attach_interrupt_ctx(PIN_SWAP, ctx_isr, &s_probe_swap,
                                      HAL_GPIO_IRQ_RISING);
  pulse_pin(PIN_SWAP);
  check(s_probe_swap.hits > swap_ctx);
}

/* Nothing fires once every pin is detached. */
static void run_detach_phase(void) {
  s_owner_status = hal_gpio_get_interrupt_owner_ex(PIN_CTX_A, &s_owner_core);

  hal_gpio_detach_interrupt(PIN_CTX_A);
  hal_gpio_detach_interrupt(PIN_CTX_B);
  hal_gpio_detach_interrupt(PIN_PLAIN);
  hal_gpio_detach_interrupt(PIN_SWAP);

  uint8_t detached_core = 0u;
  s_detached_status =
      hal_gpio_get_interrupt_owner_ex(PIN_CTX_A, &detached_core);

  const uint32_t a_hits = s_probe_a.hits;
  const uint32_t b_hits = s_probe_b.hits;
  const uint32_t swap_hits = s_probe_swap.hits;
  const uint32_t plain_hits = s_plain_hits;
  pulse_pin(PIN_CTX_A);
  pulse_pin(PIN_CTX_B);
  pulse_pin(PIN_PLAIN);
  pulse_pin(PIN_SWAP);
  check(s_probe_a.hits == a_hits && s_probe_b.hits == b_hits &&
        s_probe_swap.hits == swap_hits && s_plain_hits == plain_hits);
}

static void run_probe(void) {
  s_failed_mask = 0u;
  s_check_index = 0u;
  s_owner_status = HAL_NONE;
  s_owner_core = HAL_GPIO_IRQ_CORE_NONE;
  s_detached_status = HAL_NONE;
  reset_counters();

  release_pin(PIN_CTX_A);
  release_pin(PIN_CTX_B);
  release_pin(PIN_PLAIN);
  release_pin(PIN_SWAP);

  s_attach_status = attach_all();
  check(s_attach_status == HAL_OK);
  if (s_attach_status != HAL_OK) {
    s_check_index = CHECK_COUNT;
    return;
  }

  run_routing_phase();
  run_swap_phase();
  run_detach_phase();

  check(s_owner_status == HAL_OK && s_owner_core == 0u &&
        s_detached_status == HAL_ENOENT);
}

static void prepare_result(void) {
  run_probe();
  const int length = snprintf(
      (char *)s_response, sizeof(s_response),
      "JHGPIOIRQ1 checks=%u failed=0x%08lx attach=%d owner=%d "
      "owner_core=%u detached=%d a=%lu/%u b=%lu/%u swap=%lu/%u "
      "plain=%lu\n",
      (unsigned)s_check_index, (unsigned long)s_failed_mask,
      (int)s_attach_status, (int)s_owner_status, (unsigned)s_owner_core,
      (int)s_detached_status, (unsigned long)s_probe_a.hits,
      (unsigned)s_probe_a.last_pin, (unsigned long)s_probe_b.hits,
      (unsigned)s_probe_b.last_pin, (unsigned long)s_probe_swap.hits,
      (unsigned)s_probe_swap.last_pin, (unsigned long)s_plain_hits);
  s_response_length =
      length > 0 && (size_t)length < sizeof(s_response) ? (size_t)length : 0u;
  s_response_offset = 0u;
}

void app_start(void) {}

void app_task0(void) {
  if (s_response_offset < s_response_length) {
    size_t written = 0u;
    (void)hal_usb_cdc_write(s_response + s_response_offset,
                            s_response_length - s_response_offset, 100u,
                            &written);
    s_response_offset += written;
    return;
  }

  uint8_t command = 0u;
  size_t received = 0u;
  if (hal_usb_cdc_read(&command, 1u, &received) != HAL_OK || received != 1u) {
    hal_delay_ms(1u);
    return;
  }
  if (command == (uint8_t)'T') {
    prepare_result();
  }
}
