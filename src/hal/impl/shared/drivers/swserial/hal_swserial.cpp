#include "hal/hal_config.h"
#ifdef HAL_ENABLE_SWSERIAL

#include "hal/hal_swserial.h"

#include "hal/hal_gpio.h"
#include "hal/hal_sync.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <array>
#include <new>
#include <string.h>
#include <utility>

/*
 * Software UART for JaszczurHAL.
 *
 * The framing and RX/TX flow are based on the Serial-over-PIO implementation
 * by Earle F. Philhower, III. This refactor keeps the same UART frame handling
 * ideas while moving the driver onto JaszczurHAL GPIO, timing and sync
 * primitives so RP2040, STM32G474 and mock builds share one implementation.
 */

#define HAL_SWSERIAL_RX_BUF_SIZE 64u
#define HAL_SWSERIAL_TX_CAPTURE_SIZE 512u
#define HAL_SWSERIAL_MAX_GPIO_PIN 255u
#define HAL_SWSERIAL_IRQ_PIN_COUNT 128u

typedef enum {
  HAL_SWSERIAL_PARITY_NONE = 0,
  HAL_SWSERIAL_PARITY_EVEN = 1,
  HAL_SWSERIAL_PARITY_ODD = 2,
} hal_swserial_parity_t;

struct hal_swserial_impl_s {
  uint8_t rx_pin;
  uint8_t tx_pin;
  uint32_t baud;
  uint32_t bit_us;
  uint8_t bits;
  uint8_t stop_bits;
  hal_swserial_parity_t parity;
  bool started;
  bool overflow;
  uint8_t rx_buf[HAL_SWSERIAL_RX_BUF_SIZE];
  uint8_t head;
  uint8_t tail;
  hal_mutex_t mutex;
#if HAL_TARGET_IS_MOCK
  char last_write[HAL_SWSERIAL_TX_CAPTURE_SIZE];
#endif
};

static hal_swserial_impl_t s_pool[HAL_SWSERIAL_MAX_INSTANCES];
static bool s_used[HAL_SWSERIAL_MAX_INSTANCES];
static hal_mutex_t s_pool_mutex = NULL;
static hal_swserial_t s_rx_by_pin[HAL_SWSERIAL_MAX_GPIO_PIN + 1u];

static inline uint8_t next_index(uint8_t index) {
  return (uint8_t)((index + 1u) % HAL_SWSERIAL_RX_BUF_SIZE);
}

static int swserial_parity(uint32_t data) {
  data ^= data >> 4u;
  data &= 0x0Fu;
  return (int)((0x6996u >> data) & 1u);
}

static uint8_t swserial_data_bits(uint16_t config) {
  switch (config & 0x0700u) {
  case HAL_UART_DATA_5:
    return 5u;
  case HAL_UART_DATA_6:
    return 6u;
  case HAL_UART_DATA_7:
    return 7u;
  case HAL_UART_DATA_8:
  default:
    return 8u;
  }
}

static uint8_t swserial_stop_bits(uint16_t config) {
  return ((config & 0x0030u) == HAL_UART_STOP_BIT_2) ? 2u : 1u;
}

static hal_swserial_parity_t swserial_parity_mode(uint16_t config) {
  switch (config & 0x0003u) {
  case HAL_UART_PARITY_EVEN:
    return HAL_SWSERIAL_PARITY_EVEN;
  case HAL_UART_PARITY_ODD:
    return HAL_SWSERIAL_PARITY_ODD;
  default:
    return HAL_SWSERIAL_PARITY_NONE;
  }
}

static void swserial_push_rx_isr(hal_swserial_t h, uint8_t value) {
  uint8_t next = next_index(h->tail);
  if (next == h->head) {
    h->overflow = true;
    return;
  }
  h->rx_buf[h->tail] = value;
  h->tail = next;
}

static void swserial_rx_edge(hal_swserial_t h) {
  if (h == NULL || !h->started) {
    return;
  }

  const uint8_t rx_pin = h->rx_pin;
  const uint32_t bit_us = h->bit_us;
  const uint8_t bits = h->bits;
  const hal_swserial_parity_t parity = h->parity;

  if (hal_gpio_read(rx_pin)) {
    return;
  }

  hal_delay_us(bit_us + (bit_us / 2u));

  uint32_t value = 0u;
  for (uint8_t bit = 0u; bit < bits; ++bit) {
    if (hal_gpio_read(rx_pin)) {
      value |= (1u << bit);
    }
    hal_delay_us(bit_us);
  }

  if (parity != HAL_SWSERIAL_PARITY_NONE) {
    const bool parity_bit = hal_gpio_read(rx_pin);
    const int calc = swserial_parity(value);
    if ((parity == HAL_SWSERIAL_PARITY_EVEN && calc != (int)parity_bit) ||
        (parity == HAL_SWSERIAL_PARITY_ODD && calc == (int)parity_bit)) {
      return;
    }
    hal_delay_us(bit_us);
  }

  if (!hal_gpio_read(rx_pin)) {
    return;
  }

  hal_critical_section_enter();
  swserial_push_rx_isr(h, (uint8_t)(value & ((1u << bits) - 1u)));
  hal_critical_section_exit();
}

template <size_t Pin> static void swserial_irq_trampoline(void) {
  swserial_rx_edge(s_rx_by_pin[Pin]);
}

template <size_t... Pins>
static constexpr std::array<void (*)(void), sizeof...(Pins)>
swserial_make_trampolines(std::index_sequence<Pins...>) {
  return {swserial_irq_trampoline<Pins>...};
}

static constexpr auto s_pin_trampoline = swserial_make_trampolines(
    std::make_index_sequence<HAL_SWSERIAL_IRQ_PIN_COUNT>{});

static void swserial_reset_handle(hal_swserial_t h, uint8_t rx_pin,
                                  uint8_t tx_pin) {
  memset(h, 0, sizeof(*h));
  h->rx_pin = rx_pin;
  h->tx_pin = tx_pin;
  h->bits = 8u;
  h->stop_bits = 1u;
  h->parity = HAL_SWSERIAL_PARITY_NONE;
}

hal_swserial_t hal_swserial_create(uint8_t rx_pin, uint8_t tx_pin) {
  if (jh_hal_mutex_create_once(&s_pool_mutex) == NULL) {
    return NULL;
  }

  hal_mutex_lock(s_pool_mutex);
  for (int i = 0; i < hal_get_config()->swserial_max_instances; i++) {
    if (!s_used[i]) {
      s_used[i] = true;
      hal_swserial_t h = &s_pool[i];
      swserial_reset_handle(h, rx_pin, tx_pin);
      h->mutex = hal_mutex_create();
      hal_mutex_unlock(s_pool_mutex);
      return h;
    }
  }
  hal_mutex_unlock(s_pool_mutex);

  HAL_ASSERT(
      0, "hal_swserial: pool exhausted - increase HAL_SWSERIAL_MAX_INSTANCES");
  return NULL;
}

bool hal_swserial_set_rx(hal_swserial_t h, uint8_t rx_pin) {
  if (h == NULL) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  if (h->rx_pin == rx_pin) {
    hal_mutex_unlock(h->mutex);
    return true;
  }
  if (h->started) {
    hal_mutex_unlock(h->mutex);
    return false;
  }
  h->rx_pin = rx_pin;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_swserial_set_tx(hal_swserial_t h, uint8_t tx_pin) {
  if (h == NULL) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  if (h->tx_pin == tx_pin) {
    hal_mutex_unlock(h->mutex);
    return true;
  }
  if (h->started) {
    hal_mutex_unlock(h->mutex);
    return false;
  }
  h->tx_pin = tx_pin;
  hal_mutex_unlock(h->mutex);
  return true;
}

void hal_swserial_begin(hal_swserial_t h, uint32_t baud, uint16_t config) {
  if (h == NULL || baud == 0u) {
    return;
  }

  hal_mutex_lock(h->mutex);
  h->baud = baud;
  h->bit_us = (1000000u + (baud / 2u)) / baud;
  if (h->bit_us == 0u) {
    h->bit_us = 1u;
  }
  h->bits = swserial_data_bits(config);
  h->stop_bits = swserial_stop_bits(config);
  h->parity = swserial_parity_mode(config);
  h->head = 0u;
  h->tail = 0u;
  h->overflow = false;

  hal_gpio_set_mode(h->tx_pin, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_set_mode(h->rx_pin, HAL_GPIO_INPUT_PULLUP);
  if (h->rx_pin < s_pin_trampoline.size()) {
    s_rx_by_pin[h->rx_pin] = h;
    hal_gpio_attach_interrupt(h->rx_pin, s_pin_trampoline[h->rx_pin],
                              HAL_GPIO_IRQ_FALLING);
    hal_gpio_set_irq_priority(HAL_IRQ_PRIORITY_HIGH);
  } else {
    HAL_ASSERT(false, "hal_swserial: RX pin has no IRQ trampoline");
  }
  h->started = true;
  hal_mutex_unlock(h->mutex);
}

int hal_swserial_available(hal_swserial_t h) {
  if (h == NULL) {
    return 0;
  }
  hal_critical_section_enter();
  int available = (int)((h->tail + HAL_SWSERIAL_RX_BUF_SIZE - h->head) %
                        HAL_SWSERIAL_RX_BUF_SIZE);
  hal_critical_section_exit();
  return available;
}

int hal_swserial_read(hal_swserial_t h) {
  if (h == NULL) {
    return -1;
  }
  hal_critical_section_enter();
  if (h->head == h->tail) {
    hal_critical_section_exit();
    return -1;
  }
  int value = h->rx_buf[h->head];
  h->head = next_index(h->head);
  hal_critical_section_exit();
  return value;
}

static size_t swserial_write_byte_locked(hal_swserial_t h, uint8_t byte) {
  uint32_t value = byte & ((1u << h->bits) - 1u);

  hal_critical_section_enter();
  hal_gpio_write(h->tx_pin, false);
  hal_delay_us(h->bit_us);

  for (uint8_t bit = 0u; bit < h->bits; ++bit) {
    hal_gpio_write(h->tx_pin, (value & (1u << bit)) != 0u);
    hal_delay_us(h->bit_us);
  }

  if (h->parity == HAL_SWSERIAL_PARITY_EVEN) {
    hal_gpio_write(h->tx_pin, swserial_parity(value) != 0);
    hal_delay_us(h->bit_us);
  } else if (h->parity == HAL_SWSERIAL_PARITY_ODD) {
    hal_gpio_write(h->tx_pin, swserial_parity(value) == 0);
    hal_delay_us(h->bit_us);
  }

  hal_gpio_write(h->tx_pin, true);
  for (uint8_t stop = 0u; stop < h->stop_bits; ++stop) {
    hal_delay_us(h->bit_us);
  }
  hal_critical_section_exit();
  return 1u;
}

size_t hal_swserial_write(hal_swserial_t h, const uint8_t *data, size_t len) {
  if (h == NULL || data == NULL || len == 0u) {
    return 0u;
  }

  hal_mutex_lock(h->mutex);
  if (!h->started) {
    hal_mutex_unlock(h->mutex);
    return 0u;
  }

#if HAL_TARGET_IS_MOCK
  size_t copy_len = len;
  if (copy_len >= sizeof(h->last_write)) {
    copy_len = sizeof(h->last_write) - 1u;
  }
  memcpy(h->last_write, data, copy_len);
  h->last_write[copy_len] = '\0';
#endif

  size_t written = 0u;
  for (size_t i = 0u; i < len; ++i) {
    written += swserial_write_byte_locked(h, data[i]);
  }
  hal_mutex_unlock(h->mutex);
  return written;
}

size_t hal_swserial_println(hal_swserial_t h, const char *s) {
  const char *text = (s != NULL) ? s : "";
  const size_t len = strlen(text);
  size_t written = hal_swserial_write(h, (const uint8_t *)text, len);
  static const uint8_t crlf[] = {'\r', '\n'};
  (void)hal_swserial_write(h, crlf, sizeof(crlf));
#if HAL_TARGET_IS_MOCK
  if (h != NULL) {
    size_t copy_len = len;
    if (copy_len >= sizeof(h->last_write)) {
      copy_len = sizeof(h->last_write) - 1u;
    }
    memcpy(h->last_write, text, copy_len);
    h->last_write[copy_len] = '\0';
  }
#endif
  return written;
}

void hal_swserial_flush(hal_swserial_t h) {
  if (h != NULL && h->started) {
    hal_delay_us((uint32_t)(h->bit_us * (h->bits + h->stop_bits + 2u)));
  }
}

void hal_swserial_destroy(hal_swserial_t h) {
  if (h == NULL) {
    return;
  }
  (void)jh_hal_mutex_create_once(&s_pool_mutex);

  hal_mutex_lock(s_pool_mutex);
  for (int i = 0; i < hal_get_config()->swserial_max_instances; i++) {
    if (h == &s_pool[i]) {
      hal_mutex_lock(h->mutex);
      if (h->started && h->rx_pin < s_pin_trampoline.size()) {
        hal_gpio_detach_interrupt(h->rx_pin);
        s_rx_by_pin[h->rx_pin] = NULL;
      }
      hal_mutex_t mutex = h->mutex;
      h->started = false;
      s_used[i] = false;
      hal_mutex_unlock(mutex);
      hal_mutex_destroy(mutex);
      memset(h, 0, sizeof(*h));
      hal_mutex_unlock(s_pool_mutex);
      return;
    }
  }
  hal_mutex_unlock(s_pool_mutex);
}

#if HAL_TARGET_IS_MOCK
void hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len) {
  if (h == NULL || data == NULL || len <= 0) {
    return;
  }
  hal_critical_section_enter();
  for (int i = 0; i < len; ++i) {
    swserial_push_rx_isr(h, data[i]);
  }
  hal_critical_section_exit();
}

void hal_mock_swserial_reset(hal_swserial_t h) {
  if (h == NULL) {
    return;
  }
  hal_critical_section_enter();
  h->head = 0u;
  h->tail = 0u;
  h->overflow = false;
  h->last_write[0] = '\0';
  hal_critical_section_exit();
}

const char *hal_mock_swserial_last_write(hal_swserial_t h) {
  return (h != NULL) ? h->last_write : "";
}
#endif

#endif /* HAL_ENABLE_SWSERIAL */
