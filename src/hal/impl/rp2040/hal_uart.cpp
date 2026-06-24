#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_UART

#include "../../hal_sync.h"
#include "../../hal_uart.h"

#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/regs/uart.h>
#include <hardware/uart.h>
#include <pico/critical_section.h>
#include <string.h>

#define HAL_RP2040_UART_BUF_SIZE 512
#define HAL_RP2040_UART_PIN_NONE 255u
#define HAL_RP2040_UART_DR_ERROR_BITS                                          \
  (UART_UARTDR_OE_BITS | UART_UARTDR_BE_BITS | UART_UARTDR_PE_BITS |           \
   UART_UARTDR_FE_BITS)
#define HAL_RP2040_UART_DR_CORRUPT_BITS                                        \
  (UART_UARTDR_BE_BITS | UART_UARTDR_PE_BITS | UART_UARTDR_FE_BITS)
#define HAL_RP2040_UART_IMSC_ERROR_BITS                                        \
  (UART_UARTIMSC_OEIM_BITS | UART_UARTIMSC_BEIM_BITS |                         \
   UART_UARTIMSC_PEIM_BITS | UART_UARTIMSC_FEIM_BITS)
#define HAL_RP2040_UART_ICR_RX_BITS                                            \
  (UART_UARTICR_OEIC_BITS | UART_UARTICR_BEIC_BITS | UART_UARTICR_PEIC_BITS |  \
   UART_UARTICR_FEIC_BITS | UART_UARTICR_RTIC_BITS | UART_UARTICR_RXIC_BITS)

struct hal_uart_impl_s {
  uint8_t rx_buf[HAL_RP2040_UART_BUF_SIZE];
  uart_inst_t *uart;
  hal_uart_port_t port;
  uint8_t rx_pin;
  uint8_t tx_pin;
  int head;
  int tail;
  int in_use;
  bool running;
  uint32_t actual_baud;
  hal_uart_error_counters_t errors;
  critical_section_t rx_lock;
  hal_mutex_t mutex;
};

typedef struct {
  uint data_bits;
  uint stop_bits;
  uart_parity_t parity;
} rp2040_uart_format_t;

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES] = {};
static hal_uart_impl_t *s_irq_handles[2] = {};

static void uart_irq_handler_0(void);
static void uart_irq_handler_1(void);

static uart_inst_t *hal_uart_select_port(hal_uart_port_t port) {
  switch (port) {
  case HAL_UART_PORT_1:
    return uart0;
  case HAL_UART_PORT_2:
    return uart1;
  default:
    return NULL;
  }
}

static bool uart_pin_bit_is_set(uint8_t pin, uint64_t mask) {
  return pin < 64u && ((mask & (1ull << pin)) != 0u);
}

static bool uart_rx_pin_valid(uart_inst_t *uart, uint8_t pin) {
  if (pin == HAL_RP2040_UART_PIN_NONE) {
    return true;
  }
#if defined(PICO_RP2350) && !PICO_RP2350A
  const uint64_t valid[2] = {
      (1ull << 1) | (1ull << 3) | (1ull << 13) | (1ull << 15) | (1ull << 17) |
          (1ull << 19) | (1ull << 29) | (1ull << 31) | (1ull << 33) |
          (1ull << 35) | (1ull << 45) | (1ull << 47),
      (1ull << 5) | (1ull << 7) | (1ull << 9) | (1ull << 11) | (1ull << 21) |
          (1ull << 23) | (1ull << 25) | (1ull << 27) | (1ull << 37) |
          (1ull << 39) | (1ull << 41) | (1ull << 43),
  };
#elif defined(PICO_RP2350)
  const uint64_t valid[2] = {
      (1ull << 1) | (1ull << 3) | (1ull << 13) | (1ull << 15) | (1ull << 17) |
          (1ull << 19) | (1ull << 29),
      (1ull << 5) | (1ull << 7) | (1ull << 9) | (1ull << 11) | (1ull << 21) |
          (1ull << 23) | (1ull << 25) | (1ull << 27),
  };
#else
  const uint64_t valid[2] = {
      (1ull << 1) | (1ull << 13) | (1ull << 17) | (1ull << 29),
      (1ull << 5) | (1ull << 9) | (1ull << 21) | (1ull << 25),
  };
#endif
  return uart_pin_bit_is_set(pin, valid[uart_get_index(uart)]);
}

static bool uart_tx_pin_valid(uart_inst_t *uart, uint8_t pin) {
  if (pin == HAL_RP2040_UART_PIN_NONE) {
    return true;
  }
#if defined(PICO_RP2350) && !PICO_RP2350A
  const uint64_t valid[2] = {
      (1ull << 0) | (1ull << 2) | (1ull << 12) | (1ull << 14) | (1ull << 16) |
          (1ull << 18) | (1ull << 28) | (1ull << 30) | (1ull << 32) |
          (1ull << 34) | (1ull << 44) | (1ull << 46),
      (1ull << 4) | (1ull << 6) | (1ull << 8) | (1ull << 10) | (1ull << 20) |
          (1ull << 22) | (1ull << 24) | (1ull << 26) | (1ull << 36) |
          (1ull << 38) | (1ull << 40) | (1ull << 42),
  };
#elif defined(PICO_RP2350)
  const uint64_t valid[2] = {
      (1ull << 0) | (1ull << 2) | (1ull << 12) | (1ull << 14) | (1ull << 16) |
          (1ull << 18) | (1ull << 28),
      (1ull << 4) | (1ull << 6) | (1ull << 8) | (1ull << 10) | (1ull << 20) |
          (1ull << 22) | (1ull << 24) | (1ull << 26),
  };
#else
  const uint64_t valid[2] = {
      (1ull << 0) | (1ull << 12) | (1ull << 16) | (1ull << 28),
      (1ull << 4) | (1ull << 8) | (1ull << 20) | (1ull << 24),
  };
#endif
  return uart_pin_bit_is_set(pin, valid[uart_get_index(uart)]);
}

static gpio_function_t uart_gpio_function(uint8_t pin) {
#if defined(PICO_RP2350)
  switch (pin) {
  case 2:
  case 3:
  case 6:
  case 7:
  case 10:
  case 11:
  case 14:
  case 15:
  case 18:
  case 19:
  case 22:
  case 23:
  case 26:
  case 27:
  case 30:
  case 31:
  case 34:
  case 35:
  case 38:
  case 39:
  case 42:
  case 43:
  case 46:
  case 47:
    return GPIO_FUNC_UART_AUX;
  default:
    break;
  }
#else
  (void)pin;
#endif
  return GPIO_FUNC_UART;
}

static rp2040_uart_format_t uart_decode_format(uint16_t config) {
  rp2040_uart_format_t fmt = {8u, 1u, UART_PARITY_NONE};

  switch (config & 0x000Fu) {
  case HAL_UART_PARITY_EVEN:
    fmt.parity = UART_PARITY_EVEN;
    break;
  case HAL_UART_PARITY_ODD:
    fmt.parity = UART_PARITY_ODD;
    break;
  default:
    fmt.parity = UART_PARITY_NONE;
    break;
  }

  switch (config & 0x00F0u) {
  case HAL_UART_STOP_BIT_2:
    fmt.stop_bits = 2u;
    break;
  default:
    fmt.stop_bits = 1u;
    break;
  }

  switch (config & 0x0F00u) {
  case HAL_UART_DATA_5:
    fmt.data_bits = 5u;
    break;
  case HAL_UART_DATA_6:
    fmt.data_bits = 6u;
    break;
  case HAL_UART_DATA_7:
    fmt.data_bits = 7u;
    break;
  default:
    fmt.data_bits = 8u;
    break;
  }

  return fmt;
}

static int uart_ring_available(const hal_uart_impl_t *h) {
  return (h->tail - h->head + HAL_RP2040_UART_BUF_SIZE) %
         HAL_RP2040_UART_BUF_SIZE;
}

static void uart_ring_reset(hal_uart_impl_t *h) {
  h->head = 0;
  h->tail = 0;
}

static bool uart_ring_push(hal_uart_impl_t *h, uint8_t data) {
  const int next = (h->tail + 1) % HAL_RP2040_UART_BUF_SIZE;
  if (next == h->head) {
    return false;
  }
  h->rx_buf[h->tail] = data;
  h->tail = next;
  return true;
}

static void uart_rx_lock(hal_uart_impl_t *h) {
  critical_section_enter_blocking(&h->rx_lock);
}

static void uart_rx_unlock(hal_uart_impl_t *h) {
  critical_section_exit(&h->rx_lock);
}

static uint32_t uart_record_dr_errors(hal_uart_impl_t *h, uint32_t raw) {
  uint32_t rsr_seen = 0u;
  if ((raw & UART_UARTDR_OE_BITS) != 0u) {
    ++h->errors.rx_overrun;
    rsr_seen |= UART_UARTRSR_OE_BITS;
  }
  if ((raw & UART_UARTDR_BE_BITS) != 0u) {
    ++h->errors.rx_break;
    rsr_seen |= UART_UARTRSR_BE_BITS;
  }
  if ((raw & UART_UARTDR_PE_BITS) != 0u) {
    ++h->errors.rx_parity;
    rsr_seen |= UART_UARTRSR_PE_BITS;
  }
  if ((raw & UART_UARTDR_FE_BITS) != 0u) {
    ++h->errors.rx_framing;
    rsr_seen |= UART_UARTRSR_FE_BITS;
  }
  return rsr_seen;
}

static void uart_record_rsr_errors(hal_uart_impl_t *h, uint32_t rsr) {
  if ((rsr & UART_UARTRSR_OE_BITS) != 0u) {
    ++h->errors.rx_overrun;
  }
  if ((rsr & UART_UARTRSR_BE_BITS) != 0u) {
    ++h->errors.rx_break;
  }
  if ((rsr & UART_UARTRSR_PE_BITS) != 0u) {
    ++h->errors.rx_parity;
  }
  if ((rsr & UART_UARTRSR_FE_BITS) != 0u) {
    ++h->errors.rx_framing;
  }
}

static void uart_clear_rx_errors(uart_hw_t *hw) {
  hw->rsr = UART_UARTRSR_BITS;
  hw->icr = HAL_RP2040_UART_ICR_RX_BITS;
}

static void uart_drain_rx_locked(hal_uart_impl_t *h) {
  if (!h || !h->running || !h->uart) {
    return;
  }

  uart_hw_t *hw = uart_get_hw(h->uart);
  const uint32_t sticky_errors = hw->rsr & UART_UARTRSR_BITS;
  uint32_t errors_seen = 0u;
  while (uart_is_readable(h->uart)) {
    const uint32_t raw = hw->dr;
    if ((raw & HAL_RP2040_UART_DR_ERROR_BITS) != 0u) {
      errors_seen |= uart_record_dr_errors(h, raw);
      if ((raw & HAL_RP2040_UART_DR_CORRUPT_BITS) != 0u) {
        continue;
      }
    }
    if (!uart_ring_push(h, (uint8_t)(raw & 0xFFu))) {
      ++h->errors.rx_buffer_overflow;
    }
  }

  const uint32_t sticky_only = sticky_errors & ~errors_seen;
  if (sticky_only != 0u) {
    uart_record_rsr_errors(h, sticky_only);
  }
  if ((sticky_errors | errors_seen) != 0u) {
    uart_clear_rx_errors(hw);
  }
}

static void uart_lock(hal_uart_impl_t *h) {
  if (h && h->mutex) {
    hal_mutex_lock(h->mutex);
  }
}

static void uart_unlock(hal_uart_impl_t *h) {
  if (h && h->mutex) {
    hal_mutex_unlock(h->mutex);
  }
}

static irq_handler_t uart_irq_handler_for(uint idx) {
  return idx == 0u ? uart_irq_handler_0 : uart_irq_handler_1;
}

static void uart_handle_irq(uint idx) {
  hal_uart_impl_t *h = idx < 2u ? s_irq_handles[idx] : NULL;
  if (!h) {
    return;
  }

  uart_rx_lock(h);
  uart_drain_rx_locked(h);
  uart_rx_unlock(h);
}

static void uart_irq_handler_0(void) { uart_handle_irq(0u); }

static void uart_irq_handler_1(void) { uart_handle_irq(1u); }

static bool uart_enable_rx_irq(hal_uart_impl_t *h) {
  const uint idx = uart_get_index(h->uart);
  const uint irqn = UART_IRQ_NUM(h->uart);
  irq_handler_t handler = uart_irq_handler_for(idx);

  if (irq_has_handler(irqn) && irq_get_exclusive_handler(irqn) != handler) {
    HAL_ASSERT(0, "hal_uart_begin: UART IRQ is already in use");
    return false;
  }

  uart_hw_t *hw = uart_get_hw(h->uart);
  uart_clear_rx_errors(hw);
  s_irq_handles[idx] = h;
  irq_set_exclusive_handler(irqn, handler);
  irq_set_enabled(irqn, true);
  uart_set_irq_enables(h->uart, true, false);
  hw->imsc |= HAL_RP2040_UART_IMSC_ERROR_BITS;
  return true;
}

static void uart_disable_rx_irq(hal_uart_impl_t *h) {
  if (!h || !h->uart) {
    return;
  }

  const uint idx = uart_get_index(h->uart);
  const uint irqn = UART_IRQ_NUM(h->uart);
  uart_set_irq_enables(h->uart, false, false);
  irq_set_enabled(irqn, false);

  uart_rx_lock(h);
  if (s_irq_handles[idx] == h) {
    s_irq_handles[idx] = NULL;
  }
  h->running = false;
  uart_rx_unlock(h);

  irq_handler_t handler = uart_irq_handler_for(idx);
  if (irq_get_exclusive_handler(irqn) == handler) {
    irq_remove_handler(irqn, handler);
  }
}

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin,
                           uint8_t tx_pin) {
  uart_inst_t *uart = hal_uart_select_port(port);
  HAL_ASSERT(uart != NULL,
             "hal_uart: selected UART port is not available on this target");
  if (!uart) {
    return NULL;
  }

  if (!uart_rx_pin_valid(uart, rx_pin) || !uart_tx_pin_valid(uart, tx_pin)) {
    HAL_ASSERT(0, "hal_uart: invalid RX/TX pin for selected UART port");
    return NULL;
  }

  for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
    if (s_pool[i].in_use && s_pool[i].port == port) {
      HAL_ASSERT(0, "hal_uart: port already in use");
      return NULL;
    }
  }
  for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
    if (!s_pool[i].in_use) {
      hal_mutex_t mutex = hal_mutex_create();
      if (!mutex) {
        return NULL;
      }
      memset(&s_pool[i], 0, sizeof(s_pool[i]));
      critical_section_init(&s_pool[i].rx_lock);
      s_pool[i].uart = uart;
      s_pool[i].port = port;
      s_pool[i].rx_pin = rx_pin;
      s_pool[i].tx_pin = tx_pin;
      s_pool[i].in_use = 1;
      s_pool[i].mutex = mutex;
      return &s_pool[i];
    }
  }

  HAL_ASSERT(0, "hal_uart: pool exhausted - increase HAL_UART_MAX_INSTANCES");
  return NULL;
}

bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin) {
  if (!h || !h->uart || !h->mutex || !uart_rx_pin_valid(h->uart, rx_pin)) {
    return false;
  }
  uart_lock(h);
  if (h->running && h->rx_pin != rx_pin) {
    uart_unlock(h);
    return false;
  }
  h->rx_pin = rx_pin;
  uart_unlock(h);
  return true;
}

bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin) {
  if (!h || !h->uart || !h->mutex || !uart_tx_pin_valid(h->uart, tx_pin)) {
    return false;
  }
  uart_lock(h);
  if (h->running && h->tx_pin != tx_pin) {
    uart_unlock(h);
    return false;
  }
  h->tx_pin = tx_pin;
  uart_unlock(h);
  return true;
}

void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
  if (!h || !h->uart || !h->mutex) {
    return;
  }
  uart_lock(h);
  if (!uart_rx_pin_valid(h->uart, h->rx_pin) ||
      !uart_tx_pin_valid(h->uart, h->tx_pin)) {
    HAL_ASSERT(0, "hal_uart_begin: invalid RX/TX pin for selected UART port");
    uart_unlock(h);
    return;
  }

  if (h->running) {
    uart_disable_rx_irq(h);
    uart_tx_wait_blocking(h->uart);
    uart_deinit(h->uart);
  }

  uart_rx_lock(h);
  uart_ring_reset(h);
  h->errors = {};
  uart_rx_unlock(h);
  if (h->tx_pin != HAL_RP2040_UART_PIN_NONE) {
    gpio_set_function(h->tx_pin, uart_gpio_function(h->tx_pin));
  }
  if (h->rx_pin != HAL_RP2040_UART_PIN_NONE) {
    gpio_set_function(h->rx_pin, uart_gpio_function(h->rx_pin));
  }

  h->actual_baud = uart_init(h->uart, baud);
  const rp2040_uart_format_t fmt = uart_decode_format(config);
  uart_set_format(h->uart, fmt.data_bits, fmt.stop_bits, fmt.parity);
  uart_set_hw_flow(h->uart, false, false);
  uart_set_fifo_enabled(h->uart, true);
  h->running = true;
  if (!uart_enable_rx_irq(h)) {
    h->running = false;
    uart_deinit(h->uart);
  }
  uart_unlock(h);
}

int hal_uart_available(hal_uart_t h) {
  if (!h || !h->uart || !h->mutex || !h->running) {
    return 0;
  }
  uart_lock(h);
  uart_rx_lock(h);
  uart_drain_rx_locked(h);
  const int available = uart_ring_available(h);
  uart_rx_unlock(h);
  uart_unlock(h);
  return available;
}

int hal_uart_read(hal_uart_t h) {
  if (!h || !h->uart || !h->mutex || !h->running) {
    return -1;
  }
  uart_lock(h);
  uart_rx_lock(h);
  uart_drain_rx_locked(h);
  if (uart_ring_available(h) == 0) {
    uart_rx_unlock(h);
    uart_unlock(h);
    return -1;
  }
  const int val = h->rx_buf[h->head];
  h->head = (h->head + 1) % HAL_RP2040_UART_BUF_SIZE;
  uart_rx_unlock(h);
  uart_unlock(h);
  return val;
}

size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len) {
  if (!h || !h->uart || !h->mutex || !h->running || !data || len == 0u) {
    return 0u;
  }
  uart_lock(h);
  uart_write_blocking(h->uart, data, len);
  uart_unlock(h);
  return len;
}

size_t hal_uart_println(hal_uart_t h, const char *s) {
  if (!h || !h->uart || !h->mutex || !h->running) {
    return 0u;
  }
  const char *text = s ? s : "";
  const size_t text_len = strlen(text);
  uart_lock(h);
  if (text_len > 0u) {
    uart_write_blocking(h->uart, (const uint8_t *)text, text_len);
  }
  uart_write_blocking(h->uart, (const uint8_t *)"\r\n", 2u);
  uart_unlock(h);
  return text_len + 2u;
}

void hal_uart_flush(hal_uart_t h) {
  if (!h || !h->uart || !h->mutex || !h->running) {
    return;
  }
  uart_lock(h);
  uart_tx_wait_blocking(h->uart);
  uart_unlock(h);
}

bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters) {
  if (!h || !h->mutex || !counters) {
    return false;
  }

  uart_lock(h);
  uart_rx_lock(h);
  uart_drain_rx_locked(h);
  *counters = h->errors;
  uart_rx_unlock(h);
  uart_unlock(h);
  return true;
}

void hal_uart_destroy(hal_uart_t h) {
  if (!h) {
    return;
  }
  hal_mutex_t mutex = h->mutex;
  uart_lock(h);
  if (h->running && h->uart) {
    uart_disable_rx_irq(h);
    uart_tx_wait_blocking(h->uart);
    uart_deinit(h->uart);
  }
  uart_unlock(h);
  if (critical_section_is_initialized(&h->rx_lock)) {
    critical_section_deinit(&h->rx_lock);
  }
  memset(h, 0, sizeof(*h));
  if (mutex) {
    hal_mutex_destroy(mutex);
  }
}

#endif /* HAL_ENABLE_UART */
#endif // HAL_TARGET_IS_RP2040
