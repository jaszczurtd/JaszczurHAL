#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#ifdef HAL_ENABLE_UART

#include "../../hal_uart.h"

#include <string.h>

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

#define HAL_UART_BUF_SIZE 512

#ifdef JH_STM32G474_HW
#define HAL_STM32_UART_ISR_ERROR_FLAGS                                         \
  (USART_ISR_PE_F | USART_ISR_FE_F | USART_ISR_NE_F | USART_ISR_ORE_F |        \
   USART_ISR_LBDF_F)
#define HAL_STM32_UART_ISR_CORRUPT_RX_FLAGS                                    \
  (USART_ISR_PE_F | USART_ISR_FE_F | USART_ISR_NE_F | USART_ISR_LBDF_F)
#endif

struct hal_uart_impl_s {
  uint8_t rx_buf[HAL_UART_BUF_SIZE];
  char last_write[HAL_UART_BUF_SIZE];
  hal_uart_port_t port;
  uint8_t rx_pin;
  uint8_t tx_pin;
  int head;
  int tail;
  int in_use;
  hal_uart_error_counters_t errors;
};

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES] = {};

#ifdef JH_STM32G474_HW
/* ── Real USART backend (polled): PORT_1 -> USART1, PORT_2 -> USART2 ───────
 * RX is drained from RDR into the ring on every available() call, so a caller
 * that polls in a tight loop (the GPS update loop) never loses bytes at the
 * typical 9600 baud. USART AF is 7 for both instances; the kernel clock is the
 * 16 MHz HSI bring-up clock. Pending on-silicon validation. */

static inline uint32_t usart_base(hal_uart_port_t port) {
  return (port == HAL_UART_PORT_2) ? USART2_BASE : USART1_BASE;
}

static inline void usart_clock_enable(hal_uart_port_t port) {
  if (port == HAL_UART_PORT_2) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART2EN;
  } else {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
  }
}

/* Route a pin (JaszczurHAL id = port*16 + pin) to USART alternate function 7.
 */
static void gpio_af7(uint8_t pin) {
  const uint32_t port = (uint32_t)(pin >> 4);
  const uint32_t n = (uint32_t)(pin & 0x0Fu);
  if (port > 6u) {
    return;
  }
  RCC_AHB2ENR |= (1u << port);
  GPIO_MODER(port) =
      (GPIO_MODER(port) & ~(0x3u << (n * 2u))) | (GPIO_MODE_AF << (n * 2u));
  GPIO_OSPEEDR(port) |= (0x3u << (n * 2u));
  if (n < 8u) {
    GPIO_AFRL(port) =
        (GPIO_AFRL(port) & ~(0xFu << (n * 4u))) | (7u << (n * 4u));
  } else {
    const uint32_t p = n - 8u;
    GPIO_AFRH(port) =
        (GPIO_AFRH(port) & ~(0xFu << (p * 4u))) | (7u << (p * 4u));
  }
}

static bool uart_ring_push(hal_uart_impl_t *h, uint8_t data) {
  const int next = (h->tail + 1) % HAL_UART_BUF_SIZE;
  if (next == h->head) {
    h->errors.rx_buffer_overflow++;
    return false;
  }
  h->rx_buf[h->tail] = data;
  h->tail = next;
  return true;
}

static uint32_t usart_error_clear_mask(uint32_t isr) {
  uint32_t clear = 0u;
  if ((isr & USART_ISR_PE_F) != 0u)
    clear |= USART_ICR_PECF_F;
  if ((isr & USART_ISR_FE_F) != 0u)
    clear |= USART_ICR_FECF_F;
  if ((isr & USART_ISR_NE_F) != 0u)
    clear |= USART_ICR_NECF_F;
  if ((isr & USART_ISR_ORE_F) != 0u)
    clear |= USART_ICR_ORECF_F;
  if ((isr & USART_ISR_LBDF_F) != 0u)
    clear |= USART_ICR_LBDCF_F;
  return clear;
}

static void usart_record_errors(hal_uart_impl_t *h, uint32_t isr) {
  if ((isr & USART_ISR_ORE_F) != 0u) {
    h->errors.rx_overrun++;
  }
  if ((isr & USART_ISR_PE_F) != 0u) {
    h->errors.rx_parity++;
  }
  if ((isr & USART_ISR_FE_F) != 0u) {
    h->errors.rx_framing++;
  }
  if ((isr & USART_ISR_NE_F) != 0u) {
    h->errors.rx_framing++;
  }
  if ((isr & USART_ISR_LBDF_F) != 0u) {
    h->errors.rx_break++;
  }
}

static void usart_drain_rx(hal_uart_impl_t *h) {
  const uint32_t base = usart_base(h->port);
  for (;;) {
    const uint32_t isr = USART_ISR(base);
    if ((isr & (USART_ISR_RXNE_F | HAL_STM32_UART_ISR_ERROR_FLAGS)) == 0u) {
      break;
    }

    if ((isr & HAL_STM32_UART_ISR_ERROR_FLAGS) != 0u) {
      usart_record_errors(h, isr);
    }

    if ((isr & USART_ISR_RXNE_F) != 0u) {
      const uint8_t data = (uint8_t)USART_RDR(base);
      if ((isr & HAL_STM32_UART_ISR_CORRUPT_RX_FLAGS) == 0u) {
        (void)uart_ring_push(h, data);
      }
    }

    const uint32_t clear = usart_error_clear_mask(isr);
    if (clear != 0u) {
      USART_ICR(base) = clear;
    }
  }
}
#endif /* JH_STM32G474_HW */

hal_uart_t hal_uart_create(hal_uart_port_t port, uint8_t rx_pin,
                           uint8_t tx_pin) {
  for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
    if (s_pool[i].in_use && s_pool[i].port == port) {
      HAL_ASSERT(0, "hal_uart: port already in use");
      return NULL;
    }
  }

  for (int i = 0; i < hal_get_config()->uart_max_instances; i++) {
    if (!s_pool[i].in_use) {
      memset(&s_pool[i], 0, sizeof(s_pool[i]));
      s_pool[i].port = port;
      s_pool[i].rx_pin = rx_pin;
      s_pool[i].tx_pin = tx_pin;
      s_pool[i].in_use = 1;
      return &s_pool[i];
    }
  }

  HAL_ASSERT(0, "hal_uart: pool exhausted - increase HAL_UART_MAX_INSTANCES");
  return NULL;
}

hal_status_t hal_uart_set_rx_ex(hal_uart_t h, uint8_t rx_pin) {
  if (!h) {
    return HAL_EINVAL;
  }
  h->rx_pin = rx_pin;
  return HAL_OK;
}

bool hal_uart_set_rx(hal_uart_t h, uint8_t rx_pin) {
  return hal_status_to_bool(hal_uart_set_rx_ex(h, rx_pin));
}

hal_status_t hal_uart_set_tx_ex(hal_uart_t h, uint8_t tx_pin) {
  if (!h) {
    return HAL_EINVAL;
  }
  h->tx_pin = tx_pin;
  return HAL_OK;
}

bool hal_uart_set_tx(hal_uart_t h, uint8_t tx_pin) {
  return hal_status_to_bool(hal_uart_set_tx_ex(h, tx_pin));
}

hal_status_t hal_uart_begin_ex(hal_uart_t h, uint32_t baud, uint16_t config) {
  (void)config;
  if (!h) {
    return HAL_EINVAL;
  }
  if (baud == 0u) {
    return HAL_EINVAL;
  }
  h->head = 0;
  h->tail = 0;
  h->errors = {};
#ifdef JH_STM32G474_HW
  const uint32_t base = usart_base(h->port);
  usart_clock_enable(h->port);
  gpio_af7(h->tx_pin);
  gpio_af7(h->rx_pin);

  USART_CR1(base) &= ~USART_CR1_UE; /* disable to program BRR */
  USART_BRR(base) = (JH_G474_CORE_CLOCK_HZ + baud / 2u) / baud; /* OVER16 */
  USART_CR1(base) = USART_CR1_RE_BIT | USART_CR1_TE_BIT | USART_CR1_UE;
#else
  (void)baud;
#endif
  return HAL_OK;
}

void hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
  (void)hal_uart_begin_ex(h, baud, config);
}

int hal_uart_available(hal_uart_t h) {
  if (!h) {
    return 0;
  }
#ifdef JH_STM32G474_HW
  usart_drain_rx(h);
#endif
  return (h->tail - h->head + HAL_UART_BUF_SIZE) % HAL_UART_BUF_SIZE;
}

hal_status_t hal_uart_read_ex(hal_uart_t h, uint8_t *out_value) {
  if (!out_value) {
    return HAL_EINVAL;
  }
  *out_value = 0u;
  if (!h) {
    return HAL_EINVAL;
  }
  if (hal_uart_available(h) == 0) {
    return HAL_EAGAIN;
  }

  *out_value = h->rx_buf[h->head];
  h->head = (h->head + 1) % HAL_UART_BUF_SIZE;
  return HAL_OK;
}

int hal_uart_read(hal_uart_t h) {
  uint8_t value = 0u;
  return hal_status_to_bool(hal_uart_read_ex(h, &value)) ? (int)value : -1;
}

hal_status_t hal_uart_write_ex(hal_uart_t h, const uint8_t *data, size_t len,
                               size_t *out_written) {
  if (out_written) {
    *out_written = 0u;
  }
  if (!h || (len > 0u && !data)) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
#ifdef JH_STM32G474_HW
  const uint32_t base = usart_base(h->port);
  for (size_t i = 0; i < len; ++i) {
    uint32_t to = 200000u;
    while (!(USART_ISR(base) & USART_ISR_TXE_F) && to) {
      --to;
    }
    if (to == 0u) {
      if (out_written) {
        *out_written = i;
      }
      return HAL_ETIMEOUT;
    }
    USART_TDR(base) = data[i];
  }
  if (out_written) {
    *out_written = len;
  }
  return HAL_OK;
#else
  size_t copy_len = len;
  if (copy_len >= sizeof(h->last_write)) {
    copy_len = sizeof(h->last_write) - 1u;
  }
  memcpy(h->last_write, data, copy_len);
  h->last_write[copy_len] = '\0';
  if (out_written) {
    *out_written = copy_len;
  }
  return (copy_len == len) ? HAL_OK : HAL_EOVERFLOW;
#endif
}

size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len) {
  size_t written = 0u;
  (void)hal_uart_write_ex(h, data, len, &written);
  return written;
}

hal_status_t hal_uart_println_ex(hal_uart_t h, const char *s,
                                 size_t *out_written) {
  if (out_written) {
    *out_written = 0u;
  }
  if (!h) {
    return HAL_EINVAL;
  }
  const char *text = s ? s : "";
  size_t n = strlen(text);
#ifdef JH_STM32G474_HW
  size_t text_written = 0u;
  hal_status_t status =
      hal_uart_write_ex(h, (const uint8_t *)text, n, &text_written);
  if (!hal_status_to_bool(status)) {
    if (out_written) {
      *out_written = text_written;
    }
    return status;
  }
  size_t newline_written = 0u;
  status = hal_uart_write_ex(h, (const uint8_t *)"\r\n", 2u, &newline_written);
  if (out_written) {
    *out_written = text_written + newline_written;
  }
  return status;
#else
  size_t total = n + 2u;
  if (total >= sizeof(h->last_write)) {
    total = sizeof(h->last_write) - 1u;
  }
  size_t copy_text = (n < total) ? n : total;
  memcpy(h->last_write, text, copy_text);
  if (copy_text + 1u < sizeof(h->last_write))
    h->last_write[copy_text] = '\r';
  if (copy_text + 2u < sizeof(h->last_write))
    h->last_write[copy_text + 1u] = '\n';
  h->last_write[total] = '\0';
  if (out_written) {
    *out_written = total;
  }
  return (total == n + 2u) ? HAL_OK : HAL_EOVERFLOW;
#endif
}

size_t hal_uart_println(hal_uart_t h, const char *s) {
  size_t written = 0u;
  (void)hal_uart_println_ex(h, s, &written);
  return written;
}

hal_status_t hal_uart_flush_ex(hal_uart_t h) {
  if (!h) {
    return HAL_EINVAL;
  }
  return HAL_OK;
}

void hal_uart_flush(hal_uart_t h) { (void)hal_uart_flush_ex(h); }

hal_status_t
hal_uart_get_error_counters_ex(hal_uart_t h,
                               hal_uart_error_counters_t *counters) {
  if (!h || !counters) {
    return HAL_EINVAL;
  }
#ifdef JH_STM32G474_HW
  (void)hal_uart_available(h);
#endif
  *counters = h->errors;
  return HAL_OK;
}

bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters) {
  return hal_status_to_bool(hal_uart_get_error_counters_ex(h, counters));
}

void hal_uart_destroy(hal_uart_t h) {
  if (h) {
    h->in_use = 0;
  }
}

#endif /* HAL_ENABLE_UART */

#endif // HAL_TARGET_IS_STM32G474
