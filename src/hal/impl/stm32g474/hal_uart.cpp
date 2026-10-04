#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_UART

#include "hal/core/hal_array.h"
#include "hal/serial/hal_uart.h"
#include "hal/serial/hal_uart_internal.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include <string.h>

/* Register and clock constants also serve the frame and rate decoding of
 * host sanity builds. */
#include "port/stm32g474_regs.h"

#ifdef JH_STM32G474_HW
#include "port/g474_debug_uart.h"
#include "port/stm32g474_gpio_af.h"
#include "port/stm32g474_nvic.h"
#endif

/* PORT_1 is USART1 (PCLK2), PORT_2 is USART2 (PCLK1, also the debug console
 * and the ST-LINK virtual COM port on PA2/PA3). Reception runs on a circular
 * DMA ring: the channel's half and full events count laps, so the reader
 * knows how far the DMA has written and when it overran unread bytes.
 * Transmission runs on DMA from a queue, chunk by chunk up to its wrap. Line
 * errors (overrun, framing, noise, parity) come from the USART interrupt.
 *
 * A receive interrupt held off for too long (interrupts masked) finds both
 * the half and the full event pending and cannot tell one lap from several.
 * The cycle counter bounds how many bytes could have arrived since the lap
 * count was last known right; when that could be more than one and a half
 * rings, the event counts as rx_overrun (the bytes lost are unknown). The
 * counter wraps every 2^32 cycles (about 25 s), so after long quiet
 * stretches the millisecond clock takes over. */

#define RX_SIZE ((uint32_t)HAL_UART_RX_BUFFER_SIZE)
#define TX_SIZE ((uint32_t)HAL_UART_TX_BUFFER_SIZE)
static_assert(RX_SIZE >= 64u && (RX_SIZE & (RX_SIZE - 1u)) == 0u,
              "HAL_UART_RX_BUFFER_SIZE must be a power of two of at least 64");
static_assert(TX_SIZE >= 64u && (TX_SIZE & (TX_SIZE - 1u)) == 0u,
              "HAL_UART_TX_BUFFER_SIZE must be a power of two of at least 64");

/* Bytes the reader keeps clear of the DMA write position: an older byte may
 * be overwritten while it is being copied out. */
#define RX_GUARD 16u
/* Copies a read makes before it gives up on a DMA that keeps overtaking it. */
#define RX_TAKE_ATTEMPTS 3u
/* Below half its wrap time the cycle counter measures without ambiguity. */
#define DWT_TRUST_MS                                                           \
  ((uint32_t)((0x80000000ull * 1000u) / JH_G474_CORE_CLOCK_HZ))
/* Pin id that leaves a direction unconnected (TX-only or RX-only). */
#define PIN_NONE 255u
#define IRQ_PRIORITY 0x80u

struct hal_uart_impl_s {
  hal_uart_port_t port;
  uint8_t rx_pin;
  uint8_t tx_pin;
  int in_use;
  bool running;
  uint8_t data_mask;
  uint32_t baud;
  uint32_t presc; /* USART_PRESC value */
  uint32_t brr;
  /* Reading and writing lock separately, so a writer waiting for room does
   * not hold up the reader; reconfiguration takes both, writer side first. */
  hal_mutex_t rx_mutex; /* rx_read, rx_buffer_overflow */
  hal_mutex_t tx_mutex; /* tx_head and the queue contents */
  hal_uart_error_counters_t errors;
  volatile uint32_t rx_halves; /* DMA half/full events, written by the ISR */
  /* Cycle count (DWT) and millisecond time of the last moment the lap
   * count was known right. */
  volatile uint32_t rx_sure_at;
  volatile uint32_t rx_sure_ms;
  /* Elapsed cycles, or milliseconds, times baud from which the DMA may have
   * gone round more than once between two receive interrupts. */
  uint64_t lap_risk;
  uint64_t lap_risk_ms;
  uint32_t rx_read;          /* bytes consumed since begin() */
  uint32_t tx_head;          /* bytes queued since begin(), atomic */
  volatile uint32_t tx_tail; /* bytes handed to the USART, ISR side */
  volatile uint32_t tx_busy; /* length of the chunk the DMA is sending */
  alignas(4) uint8_t rx_ring[HAL_UART_RX_BUFFER_SIZE];
  alignas(4) uint8_t tx_ring[HAL_UART_TX_BUFFER_SIZE];
};

static hal_uart_impl_t s_pool[HAL_UART_MAX_INSTANCES] = {};

static bool port_valid(hal_uart_port_t port) {
  return port == HAL_UART_PORT_1 || port == HAL_UART_PORT_2;
}

static uint32_t port_index(hal_uart_port_t port) {
  return port == HAL_UART_PORT_2 ? 1u : 0u;
}

/* AF7 pins of each instance (RM0440 / DS12288 alternate function tables);
 * pin id = port * 16 + number. */
static const uint8_t kTxPins[2][5] = {{9u, 22u, 36u, 64u, 105u},
                                      {2u, 14u, 19u, 53u, PIN_NONE}};
static const uint8_t kRxPins[2][5] = {{10u, 23u, 37u, 65u, PIN_NONE},
                                      {3u, 15u, 20u, 54u, PIN_NONE}};

static bool pin_allowed(const uint8_t (*table)[5], hal_uart_port_t port,
                        uint8_t pin) {
  if (pin == PIN_NONE) {
    return true;
  }
  for (uint8_t candidate : table[port_index(port)]) {
    if (candidate == pin) {
      return true;
    }
  }
  return false;
}

static bool pins_allowed(hal_uart_port_t port, uint8_t rx_pin, uint8_t tx_pin) {
  return port_valid(port) && pin_allowed(kRxPins, port, rx_pin) &&
         pin_allowed(kTxPins, port, tx_pin) &&
         (rx_pin != PIN_NONE || tx_pin != PIN_NONE);
}

static bool handle_valid(const hal_uart_impl_t *h) {
  return h != nullptr && h->tx_mutex != nullptr;
}

static void unlock_both(hal_uart_impl_t *h) {
  hal_mutex_unlock(h->rx_mutex);
  hal_mutex_unlock(h->tx_mutex);
}

#ifdef JH_STM32G474_HW
struct uart_hw_t {
  uint32_t base;
  uint32_t rx_channel; /* DMA2 channel index, 0-based */
  uint32_t tx_channel;
  uint8_t rx_request;
  uint8_t tx_request;
  uint8_t usart_irq;
  uint8_t rx_irq;
  uint8_t tx_irq;
};

static const uart_hw_t kHw[2] = {
    {USART1_BASE, 0u, 1u, DMA_REQUEST_USART1_RX, DMA_REQUEST_USART1_TX,
     USART1_IRQn, DMA2_Channel1_IRQn, DMA2_Channel2_IRQn},
    {USART2_BASE, 2u, 3u, DMA_REQUEST_USART2_RX, DMA_REQUEST_USART2_TX,
     USART2_IRQn, DMA2_Channel3_IRQn, DMA2_Channel4_IRQn},
};

/* DMA2 channel n is wired to DMAMUX channel 8 + n. */
#define DMAMUX_CHANNEL(dma2_channel) (8u + (dma2_channel))

static hal_uart_impl_t *volatile s_active[2] = {};

static const uart_hw_t &hw_of(const hal_uart_impl_t *h) {
  return kHw[port_index(h->port)];
}

static void clocks_enable(hal_uart_port_t port) {
  if (port == HAL_UART_PORT_2) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART2EN;
  } else {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
  }
  RCC_AHB1ENR |= RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_DMAMUX1EN;
  (void)RCC_AHB1ENR;
}

static void dma_stop(uint32_t channel) {
  DMA_CCR(DMA2_BASE, channel) = 0u;
  DMA_IFCR(DMA2_BASE) = DMA_IFCR_CLEAR_ALL(channel);
}

static void rx_mark_sure(hal_uart_impl_t *h) {
  h->rx_sure_at = DWT_CYCCNT;
  h->rx_sure_ms = hal_millis();
}

/* Whether more than one and a half rings could have arrived since the lap
 * count was last known right. The cycle counter is exact while it has not
 * wrapped; past half its wrap time the millisecond clock decides, which can
 * only lag by the length of an interrupt mask. */
static bool rx_may_have_lapped(const hal_uart_impl_t *h, uint32_t now_cycles,
                               uint32_t now_ms) {
  const uint32_t elapsed_ms = now_ms - h->rx_sure_ms;
  if (elapsed_ms >= DWT_TRUST_MS) {
    return (uint64_t)elapsed_ms * h->baud >= h->lap_risk_ms;
  }
  return (uint64_t)(now_cycles - h->rx_sure_at) * h->baud >= h->lap_risk;
}

/* Absolute receive position: completed half laps from the interrupt plus
 * the channel's place in the ring. An event that is pending while the
 * position already moved on is counted from the position's half. */
static uint32_t rx_written(hal_uart_impl_t *h) {
  const uint32_t channel = hw_of(h).rx_channel;
  uint32_t halves = 0u;
  uint32_t remaining = 0u;
  do {
    halves = h->rx_halves;
    remaining = JH_REG32_RD(DMA_CNDTR_ADDR(DMA2_BASE, channel));
  } while (halves != h->rx_halves);
  /* With no event waiting for the interrupt the count is right now. */
  if ((DMA_ISR(DMA2_BASE) &
       (DMA_FLAG_HTIF(channel) | DMA_FLAG_TCIF(channel))) == 0u) {
    rx_mark_sure(h);
  }
  const uint32_t position =
      (remaining == 0u || remaining > RX_SIZE) ? 0u : RX_SIZE - remaining;
  const uint32_t half = position >= RX_SIZE / 2u ? 1u : 0u;
  if ((halves & 1u) != half) {
    ++halves;
  }
  return (halves >> 1) * RX_SIZE + position;
}

/* Start the next chunk unless one is in flight. The caller keeps the TX DMA
 * interrupt from running meanwhile (masked, or running inside it). */
static void tx_kick(hal_uart_impl_t *h) {
  if (h->tx_busy != 0u) {
    return;
  }
  const uint32_t pending = h->tx_head - h->tx_tail;
  if (pending == 0u) {
    return;
  }
  const uint32_t index = h->tx_tail & (TX_SIZE - 1u);
  const uint32_t chunk = pending < TX_SIZE - index ? pending : TX_SIZE - index;
  const uint32_t channel = hw_of(h).tx_channel;
  dma_stop(channel);
  DMA_CMAR(DMA2_BASE, channel) = (uint32_t)(uintptr_t)&h->tx_ring[index];
  DMA_CNDTR(DMA2_BASE, channel) = chunk;
  h->tx_busy = chunk;
  /* The queued bytes must be in memory before the DMA reads them. */
  JH_STM32G474_DMB();
  DMA_CCR(DMA2_BASE, channel) =
      DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_EN;
}

/* A chunk that completes while masked keeps its request pending, and the
 * handler chains the next chunk as soon as the mask lifts. */
static void tx_kick_masked(hal_uart_impl_t *h) {
  const uint32_t irq = hw_of(h).tx_irq;
  jh_stm32g474_nvic_disable(irq);
  tx_kick(h);
  jh_stm32g474_nvic_resume(irq);
}

static void usart_irq(uint32_t index) {
  hal_uart_impl_t *h = s_active[index];
  const uint32_t base = kHw[index].base;
  const uint32_t isr = USART_ISR(base);
  if (h != nullptr) {
    if ((isr & USART_ISR_ORE_F) != 0u) {
      ++h->errors.rx_overrun;
    }
    if ((isr & (USART_ISR_FE_F | USART_ISR_NE_F)) != 0u) {
      ++h->errors.rx_framing;
    }
    if ((isr & USART_ISR_PE_F) != 0u) {
      ++h->errors.rx_parity;
    }
    if ((isr & USART_ISR_LBDF_F) != 0u) {
      ++h->errors.rx_break;
    }
  }
  USART_ICR(base) = USART_ICR_ALL_ERRORS_F;
}

static void rx_dma_irq(uint32_t index) {
  hal_uart_impl_t *h = s_active[index];
  const uint32_t channel = kHw[index].rx_channel;
  const uint32_t flags = DMA_ISR(DMA2_BASE);
  DMA_IFCR(DMA2_BASE) = DMA_IFCR_CLEAR_ALL(channel);
  if (h == nullptr) {
    return;
  }
  const uint32_t now = DWT_CYCCNT;
  const uint32_t now_ms = hal_millis();
  const uint32_t both = DMA_FLAG_HTIF(channel) | DMA_FLAG_TCIF(channel);
  if ((flags & both) == both && rx_may_have_lapped(h, now, now_ms)) {
    /* Late enough that whole laps may have gone by uncounted. */
    ++h->errors.rx_overrun;
  }
  h->rx_sure_at = now;
  h->rx_sure_ms = now_ms;
  uint32_t halves = h->rx_halves;
  if ((flags & DMA_FLAG_HTIF(channel)) != 0u) {
    ++halves;
  }
  if ((flags & DMA_FLAG_TCIF(channel)) != 0u) {
    ++halves;
  }
  h->rx_halves = halves;
  if ((flags & DMA_FLAG_TEIF(channel)) != 0u) {
    /* A bus error disables the channel: nothing more can arrive. */
    ++h->errors.rx_overrun;
  }
}

static void tx_dma_irq(uint32_t index) {
  hal_uart_impl_t *h = s_active[index];
  const uint32_t channel = kHw[index].tx_channel;
  const uint32_t flags = DMA_ISR(DMA2_BASE);
  DMA_IFCR(DMA2_BASE) = DMA_IFCR_CLEAR_ALL(channel);
  if (h == nullptr ||
      (flags & (DMA_FLAG_TCIF(channel) | DMA_FLAG_TEIF(channel))) == 0u) {
    return;
  }
  /* On a transfer error the chunk is dropped, so the queue keeps moving. */
  DMA_CCR(DMA2_BASE, channel) = 0u;
  h->tx_tail = h->tx_tail + h->tx_busy;
  h->tx_busy = 0u;
  tx_kick(h);
}

extern "C" void USART1_IRQHandler(void) { usart_irq(0u); }
extern "C" void USART2_IRQHandler(void) { usart_irq(1u); }
extern "C" void DMA2_Channel1_IRQHandler(void) { rx_dma_irq(0u); }
extern "C" void DMA2_Channel2_IRQHandler(void) { tx_dma_irq(0u); }
extern "C" void DMA2_Channel3_IRQHandler(void) { rx_dma_irq(1u); }
extern "C" void DMA2_Channel4_IRQHandler(void) { tx_dma_irq(1u); }

static void hw_stop(hal_uart_impl_t *h) {
  const uart_hw_t &hw = hw_of(h);
  jh_stm32g474_nvic_disable(hw.usart_irq);
  jh_stm32g474_nvic_disable(hw.rx_irq);
  jh_stm32g474_nvic_disable(hw.tx_irq);
  dma_stop(hw.rx_channel);
  dma_stop(hw.tx_channel);
  DMAMUX_CCR(DMAMUX_CHANNEL(hw.rx_channel)) = 0u;
  DMAMUX_CCR(DMAMUX_CHANNEL(hw.tx_channel)) = 0u;
  USART_CR1(hw.base) = 0u;
  USART_CR3(hw.base) = 0u;
  USART_PRESC(hw.base) = 0u;
  s_active[port_index(h->port)] = nullptr;
}

static void hw_start(hal_uart_impl_t *h, uint32_t cr1_frame, uint32_t cr2) {
  const uart_hw_t &hw = hw_of(h);
  clocks_enable(h->port);
  if (h->tx_pin != PIN_NONE) {
    jh_stm32g474_gpio_set_af(h->tx_pin, 7u);
  }
  if (h->rx_pin != PIN_NONE) {
    jh_stm32g474_gpio_set_af(h->rx_pin, 7u);
  }

  USART_CR1(hw.base) = 0u;
  USART_PRESC(hw.base) = h->presc;
  USART_BRR(hw.base) = h->brr;
  USART_CR2(hw.base) = cr2;
  USART_CR3(hw.base) = USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_EIE;
  USART_ICR(hw.base) = USART_ICR_ALL_ERRORS_F;

  DMAMUX_CCR(DMAMUX_CHANNEL(hw.rx_channel)) = hw.rx_request;
  DMAMUX_CCR(DMAMUX_CHANNEL(hw.tx_channel)) = hw.tx_request;
  dma_stop(hw.rx_channel);
  dma_stop(hw.tx_channel);
  DMA_CPAR(DMA2_BASE, hw.rx_channel) = (uint32_t)(uintptr_t)&USART_RDR(hw.base);
  DMA_CMAR(DMA2_BASE, hw.rx_channel) = (uint32_t)(uintptr_t)h->rx_ring;
  DMA_CNDTR(DMA2_BASE, hw.rx_channel) = RX_SIZE;
  DMA_CPAR(DMA2_BASE, hw.tx_channel) = (uint32_t)(uintptr_t)&USART_TDR(hw.base);

  s_active[port_index(h->port)] = h;
  rx_mark_sure(h);
  if (h->rx_pin != PIN_NONE) {
    DMA_CCR(DMA2_BASE, hw.rx_channel) =
        DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_HTIE | DMA_CCR_TCIE |
        DMA_CCR_TEIE | DMA_CCR_PL_HIGH | DMA_CCR_EN;
  }
  jh_stm32g474_nvic_enable(hw.usart_irq, IRQ_PRIORITY);
  jh_stm32g474_nvic_enable(hw.rx_irq, IRQ_PRIORITY);
  jh_stm32g474_nvic_enable(hw.tx_irq, IRQ_PRIORITY);
  USART_CR1(hw.base) =
      cr1_frame | (h->rx_pin != PIN_NONE ? USART_CR1_RE_BIT : 0u) |
      (h->tx_pin != PIN_NONE ? USART_CR1_TE_BIT : 0u) | USART_CR1_UE;
}

static bool tx_idle(const hal_uart_impl_t *h) {
  return h->tx_head == h->tx_tail && h->tx_busy == 0u &&
         (USART_ISR(hw_of(h).base) & USART_ISR_TC) != 0u;
}

static void console_hand_over(hal_uart_port_t port, bool to_app) {
  if (port == HAL_UART_PORT_2) {
    g474_debug_uart_set_app_owned(to_app ? 1 : 0);
  }
}
#else
/* Host sanity builds: the queue drains at once and nothing arrives. */
static uint32_t rx_written(hal_uart_impl_t *h) { return h->rx_read; }
static void tx_kick_masked(hal_uart_impl_t *h) { h->tx_tail = h->tx_head; }
static bool tx_idle(const hal_uart_impl_t *h) {
  return h->tx_head == h->tx_tail;
}
static void hw_stop(hal_uart_impl_t *h) { (void)h; }
static void hw_start(hal_uart_impl_t *h, uint32_t cr1_frame, uint32_t cr2) {
  (void)h;
  (void)cr1_frame;
  (void)cr2;
}
static void console_hand_over(hal_uart_port_t port, bool to_app) {
  (void)port;
  (void)to_app;
}
#endif /* JH_STM32G474_HW */

/* Frame bits for CR1 and CR2, or false for a word the USART cannot send
 * (it carries 7, 8 or 9 bits with the parity bit included). */
static bool decode_frame(uint16_t config, uint32_t *cr1, uint32_t *cr2,
                         uint8_t *data_mask, uint32_t *word_bits) {
  uint32_t data_bits = 8u;
  switch (config & 0x0F00u) {
  case HAL_UART_DATA_5:
    data_bits = 5u;
    break;
  case HAL_UART_DATA_6:
    data_bits = 6u;
    break;
  case HAL_UART_DATA_7:
    data_bits = 7u;
    break;
  default:
    break;
  }
  uint32_t parity_bits = 0u;
  uint32_t bits = 0u;
  switch (config & 0x000Fu) {
  case HAL_UART_PARITY_EVEN:
    parity_bits = 1u;
    bits = USART_CR1_PCE | USART_CR1_PEIE;
    break;
  case HAL_UART_PARITY_ODD:
    parity_bits = 1u;
    bits = USART_CR1_PCE | USART_CR1_PS | USART_CR1_PEIE;
    break;
  default:
    break;
  }
  switch (data_bits + parity_bits) {
  case 7u:
    bits |= USART_CR1_M1;
    break;
  case 8u:
    break;
  case 9u:
    bits |= USART_CR1_M0;
    break;
  default:
    return false;
  }
  *cr1 = bits;
  *cr2 = (config & 0x00F0u) == HAL_UART_STOP_BIT_2 ? USART_CR2_STOP_2 : 0u;
  *data_mask = (uint8_t)((1u << data_bits) - 1u);
  *word_bits = data_bits + parity_bits;
  return true;
}

static uint32_t kernel_clock_hz(hal_uart_port_t port) {
  return port == HAL_UART_PORT_2 ? JH_G474_PCLK1_HZ : JH_G474_PCLK2_HZ;
}

/* Prescaler and divider for @p baud with 16x oversampling, or false when the
 * rate is out of reach: the divider must be 16..65535. */
static bool baud_divider(uint32_t clock, uint32_t baud, uint32_t *presc,
                         uint32_t *brr) {
  static const uint16_t kPrescDiv[] = {1u,  2u,  4u,  6u,  8u,   10u,
                                       12u, 16u, 32u, 64u, 128u, 256u};
  for (uint32_t i = 0u; i < COUNTOF(kPrescDiv); i++) {
    const uint32_t kernel = clock / kPrescDiv[i];
    const uint32_t divider = (uint32_t)(((uint64_t)kernel + baud / 2u) / baud);
    if (divider < 16u) {
      return false;
    }
    if (divider <= 0xFFFFu) {
      *presc = i;
      *brr = divider;
      return true;
    }
  }
  return false;
}

/* Time to send a full queue twice over at the configured rate, plus slack. */
static uint32_t drain_timeout_ms(const hal_uart_impl_t *h) {
  const uint64_t bits = (uint64_t)TX_SIZE * 11u * 2000u;
  return (uint32_t)(bits / (h->baud != 0u ? h->baud : 1u)) + 100u;
}

/* Also read without the writer lock (hal_uart_tx_free_ex), so a writer and
 * the DMA may move both counters between the loads. The head is read again
 * after the tail: when it stood still, both come from one moment. The bound
 * covers begin() setting both counters back to zero. */
static uint32_t tx_free(const hal_uart_impl_t *h) {
  uint32_t head = 0u;
  uint32_t tail = 0u;
  do {
    head = HAL_ATOMIC_LOAD(&h->tx_head, HAL_ATOMIC_ACQUIRE);
    tail = HAL_ATOMIC_LOAD(&h->tx_tail, HAL_ATOMIC_ACQUIRE);
  } while (head != HAL_ATOMIC_LOAD(&h->tx_head, HAL_ATOMIC_ACQUIRE));
  const uint32_t pending = head - tail;
  return pending < TX_SIZE ? TX_SIZE - pending : 0u;
}

/* Copy as much of @p data as fits behind the queue head. */
static size_t tx_enqueue(hal_uart_impl_t *h, const uint8_t *data, size_t len) {
  const uint32_t room = tx_free(h);
  const size_t count = len < room ? len : room;
  const uint32_t index = h->tx_head & (TX_SIZE - 1u);
  const size_t first = count < TX_SIZE - index ? count : TX_SIZE - index;
  memcpy(&h->tx_ring[index], data, first);
  memcpy(h->tx_ring, data + first, count - first);
  HAL_ATOMIC_STORE(&h->tx_head, h->tx_head + (uint32_t)count,
                   HAL_ATOMIC_RELEASE);
  return count;
}

static hal_status_t wait_tx_idle(hal_uart_impl_t *h) {
  const uint32_t started = hal_millis();
  const uint32_t timeout = drain_timeout_ms(h);
  while (!tx_idle(h)) {
    if (hal_millis_deadline_expired(started, timeout)) {
      return HAL_ETIMEOUT;
    }
  }
  return HAL_OK;
}

/* Bytes waiting in the ring; an overrun drops the older half. */
static uint32_t rx_available(hal_uart_impl_t *h) {
  const uint32_t written = rx_written(h);
  uint32_t waiting = written - h->rx_read;
  if (waiting > RX_SIZE - RX_GUARD) {
    const uint32_t keep = RX_SIZE / 2u;
    h->errors.rx_buffer_overflow += waiting - keep;
    h->rx_read = written - keep;
    waiting = keep;
  }
  return waiting;
}

/* Copy what is waiting. A reader preempted during the copy can find that
 * the DMA came round onto bytes it copied, so the position is read again
 * afterwards: bytes the DMA may have reached are dropped and counted as an
 * overflow, and when none is left the read starts over from what the ring
 * holds then. */
static size_t rx_take(hal_uart_impl_t *h, uint8_t *out, size_t size) {
  for (uint32_t attempt = 0u; attempt < RX_TAKE_ATTEMPTS; attempt++) {
    const uint32_t waiting = rx_available(h);
    const size_t count = size < waiting ? size : waiting;
    const uint32_t first = h->rx_read;
    __asm volatile("" ::: "memory");
    for (size_t i = 0u; i < count; i++) {
      out[i] =
          h->rx_ring[(first + (uint32_t)i) & (RX_SIZE - 1u)] & h->data_mask;
    }
    __asm volatile("" ::: "memory");
    /* The first byte the DMA cannot have reached, as rx_available() keeps. */
    const uint32_t safe_from = rx_written(h) + RX_GUARD - RX_SIZE;
    const int32_t at_risk = (int32_t)(safe_from - first);
    h->rx_read = first + (uint32_t)count;
    if (at_risk <= 0) {
      return count;
    }
    const size_t lost = (size_t)at_risk < count ? (size_t)at_risk : count;
    h->errors.rx_buffer_overflow += (uint32_t)lost;
    if (lost < count) {
      memmove(out, out + lost, count - lost);
      return count - lost;
    }
  }
  return 0u;
}

hal_status_t jh_hal_uart_validate_config_for_target(hal_uart_port_t port,
                                                    uint8_t rx_pin,
                                                    uint8_t tx_pin) {
  return pins_allowed(port, rx_pin, tx_pin) ? HAL_OK : HAL_EINVAL;
}

hal_uart_t jh_hal_uart_create_for_target(hal_uart_port_t port, uint8_t rx_pin,
                                         uint8_t tx_pin) {
  if (!pins_allowed(port, rx_pin, tx_pin)) {
    HAL_ASSERT(0, "hal_uart: invalid RX/TX pin for selected UART port");
    return NULL;
  }
  const int capacity = hal_get_config()->uart_max_instances;
  for (int i = 0; i < capacity; ++i) {
    if (s_pool[i].in_use && s_pool[i].port == port) {
      HAL_ASSERT(0, "hal_uart: port already in use");
      return NULL;
    }
  }
  for (int i = 0; i < capacity; ++i) {
    if (!s_pool[i].in_use) {
      hal_mutex_t rx_mutex = hal_mutex_create();
      hal_mutex_t tx_mutex = hal_mutex_create();
      if (rx_mutex == nullptr || tx_mutex == nullptr) {
        if (rx_mutex != nullptr) {
          hal_mutex_destroy(rx_mutex);
        }
        if (tx_mutex != nullptr) {
          hal_mutex_destroy(tx_mutex);
        }
        return NULL;
      }
      memset(&s_pool[i], 0, sizeof(s_pool[i]));
      s_pool[i].port = port;
      s_pool[i].rx_pin = rx_pin;
      s_pool[i].tx_pin = tx_pin;
      s_pool[i].rx_mutex = rx_mutex;
      s_pool[i].tx_mutex = tx_mutex;
      s_pool[i].data_mask = 0xFFu;
      s_pool[i].in_use = 1;
      return &s_pool[i];
    }
  }
  HAL_ASSERT(0, "hal_uart: pool exhausted - increase HAL_UART_MAX_INSTANCES");
  return NULL;
}

hal_status_t jh_hal_uart_set_pin_for_target(hal_uart_t h, uint8_t pin,
                                            bool receive) {
  if (!handle_valid(h) ||
      !pin_allowed(receive ? kRxPins : kTxPins, h->port, pin)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->tx_mutex);
  hal_mutex_lock(h->rx_mutex);
  uint8_t *slot = receive ? &h->rx_pin : &h->tx_pin;
  const hal_status_t status = h->running && *slot != pin ? HAL_ESTATE : HAL_OK;
  if (status == HAL_OK) {
    *slot = pin;
  }
  unlock_both(h);
  return status;
}

hal_status_t hal_uart_begin(hal_uart_t h, uint32_t baud, uint16_t config) {
  if (!handle_valid(h) || baud == 0u) {
    return HAL_EINVAL;
  }
  uint32_t cr1 = 0u;
  uint32_t cr2 = 0u;
  uint8_t data_mask = 0xFFu;
  uint32_t presc = 0u;
  uint32_t brr = 0u;
  uint32_t word_bits = 8u;
  if (!decode_frame(config, &cr1, &cr2, &data_mask, &word_bits) ||
      !baud_divider(kernel_clock_hz(h->port), baud, &presc, &brr)) {
    return HAL_EUNSUPPORTED;
  }
  hal_mutex_lock(h->tx_mutex);
  /* Bytes already queued leave at the old rate before the change; when they
   * cannot, the port stays as it is. */
  if (h->running && wait_tx_idle(h) != HAL_OK) {
    hal_mutex_unlock(h->tx_mutex);
    return HAL_ETIMEOUT;
  }
  hal_mutex_lock(h->rx_mutex);
  if (h->running) {
    hw_stop(h);
    h->running = false;
  }
  h->baud = baud;
  h->presc = presc;
  h->brr = brr;
  /* Shortest frame (start, word, one stop bit): the most bytes per cycle. */
  h->lap_risk =
      (uint64_t)(3u * RX_SIZE / 2u) * (word_bits + 2u) * JH_G474_CORE_CLOCK_HZ;
  h->lap_risk_ms = (uint64_t)(3u * RX_SIZE / 2u) * (word_bits + 2u) * 1000u;
  h->data_mask = data_mask;
  h->errors = {};
  h->rx_halves = 0u;
  h->rx_read = 0u;
  HAL_ATOMIC_STORE(&h->tx_head, 0u, HAL_ATOMIC_RELEASE);
  h->tx_tail = 0u;
  h->tx_busy = 0u;
  console_hand_over(h->port, true);
  hw_start(h, cr1, cr2);
  h->running = true;
  unlock_both(h);
  return HAL_OK;
}

int hal_uart_available(hal_uart_t h) {
  if (!handle_valid(h) || !h->running) {
    return 0;
  }
  hal_mutex_lock(h->rx_mutex);
  const uint32_t waiting = rx_available(h);
  hal_mutex_unlock(h->rx_mutex);
  return (int)waiting;
}

hal_status_t hal_uart_read_bytes_ex(hal_uart_t h, uint8_t *out, size_t size,
                                    size_t *out_read) {
  if (out_read != nullptr) {
    *out_read = 0u;
  }
  if (!handle_valid(h) || (out == nullptr && size > 0u)) {
    return HAL_EINVAL;
  }
  if (!h->running) {
    return HAL_EUNINIT;
  }
  hal_mutex_lock(h->rx_mutex);
  const size_t count = rx_take(h, out, size);
  hal_mutex_unlock(h->rx_mutex);
  if (out_read != nullptr) {
    *out_read = count;
  }
  return count > 0u || size == 0u ? HAL_OK : HAL_EAGAIN;
}

hal_status_t hal_uart_read_ex(hal_uart_t h, uint8_t *out_value) {
  if (out_value == nullptr) {
    return HAL_EINVAL;
  }
  *out_value = 0u;
  return hal_uart_read_bytes_ex(h, out_value, 1u, nullptr);
}

int hal_uart_read(hal_uart_t h) {
  uint8_t value = 0u;
  return hal_uart_read_ex(h, &value) == HAL_OK ? (int)value : -1;
}

hal_status_t hal_uart_write_ex(hal_uart_t h, const uint8_t *data, size_t len,
                               size_t *out_written) {
  if (out_written != nullptr) {
    *out_written = 0u;
  }
  if (!handle_valid(h) || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (!h->running) {
    return HAL_EUNINIT;
  }
  hal_mutex_lock(h->tx_mutex);
  size_t queued = 0u;
  hal_status_t status = HAL_OK;
  uint32_t waited_from = hal_millis();
  const uint32_t timeout = drain_timeout_ms(h);
  while (queued < len) {
    const size_t count = tx_enqueue(h, data + queued, len - queued);
    if (count > 0u) {
      queued += count;
      tx_kick_masked(h);
      waited_from = hal_millis();
    } else if (hal_millis_deadline_expired(waited_from, timeout)) {
      status = HAL_ETIMEOUT;
      break;
    }
  }
  hal_mutex_unlock(h->tx_mutex);
  if (out_written != nullptr) {
    *out_written = queued;
  }
  return status;
}

size_t hal_uart_write(hal_uart_t h, const uint8_t *data, size_t len) {
  size_t written = 0u;
  (void)hal_uart_write_ex(h, data, len, &written);
  return written;
}

hal_status_t hal_uart_try_write_ex(hal_uart_t h, const uint8_t *data,
                                   size_t len) {
  if (!handle_valid(h) || (len > 0u && data == nullptr)) {
    return HAL_EINVAL;
  }
  if (!h->running) {
    return HAL_EUNINIT;
  }
  if (len > TX_SIZE) {
    return HAL_EOVERFLOW;
  }
  /* Another writer holding the queue (a blocking write waiting for room, a
   * flush) means no room now. */
  if (!hal_mutex_try_lock(h->tx_mutex)) {
    return HAL_EAGAIN;
  }
  hal_status_t status = HAL_EAGAIN;
  if (tx_free(h) >= len) {
    (void)tx_enqueue(h, data, len);
    tx_kick_masked(h);
    status = HAL_OK;
  }
  hal_mutex_unlock(h->tx_mutex);
  return status;
}

hal_status_t hal_uart_tx_free_ex(hal_uart_t h, size_t *out_free) {
  if (out_free == nullptr) {
    return HAL_EINVAL;
  }
  *out_free = 0u;
  if (!handle_valid(h)) {
    return HAL_EINVAL;
  }
  if (!h->running) {
    return HAL_EUNINIT;
  }
  *out_free = tx_free(h);
  return HAL_OK;
}

hal_status_t hal_uart_println_ex(hal_uart_t h, const char *s,
                                 size_t *out_written) {
  if (out_written != nullptr) {
    *out_written = 0u;
  }
  const char *text = s != nullptr ? s : "";
  size_t text_written = 0u;
  hal_status_t status =
      hal_uart_write_ex(h, (const uint8_t *)text, strlen(text), &text_written);
  size_t newline_written = 0u;
  if (status == HAL_OK) {
    status =
        hal_uart_write_ex(h, (const uint8_t *)"\r\n", 2u, &newline_written);
  }
  if (out_written != nullptr) {
    *out_written = text_written + newline_written;
  }
  return status;
}

size_t hal_uart_println(hal_uart_t h, const char *s) {
  size_t written = 0u;
  (void)hal_uart_println_ex(h, s, &written);
  return written;
}

hal_status_t hal_uart_flush(hal_uart_t h) {
  if (!handle_valid(h)) {
    return HAL_EINVAL;
  }
  if (!h->running) {
    return HAL_EUNINIT;
  }
  hal_mutex_lock(h->tx_mutex);
  const hal_status_t status = wait_tx_idle(h);
  hal_mutex_unlock(h->tx_mutex);
  return status;
}

hal_status_t
hal_uart_get_error_counters_ex(hal_uart_t h,
                               hal_uart_error_counters_t *counters) {
  if (!handle_valid(h) || counters == nullptr) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->rx_mutex);
  if (h->running) {
    (void)rx_available(h);
  }
  *counters = h->errors;
  hal_mutex_unlock(h->rx_mutex);
  return HAL_OK;
}

bool hal_uart_get_error_counters(hal_uart_t h,
                                 hal_uart_error_counters_t *counters) {
  return hal_status_to_bool(hal_uart_get_error_counters_ex(h, counters));
}

void hal_uart_destroy(hal_uart_t h) {
  if (!handle_valid(h)) {
    return;
  }
  hal_mutex_t rx_mutex = h->rx_mutex;
  hal_mutex_t tx_mutex = h->tx_mutex;
  hal_mutex_lock(tx_mutex);
  /* Bytes that cannot leave within the drain time are dropped. */
  if (h->running) {
    (void)wait_tx_idle(h);
  }
  hal_mutex_lock(rx_mutex);
  if (h->running) {
    hw_stop(h);
    console_hand_over(h->port, false);
  }
  h->running = false;
  h->in_use = 0;
  h->rx_mutex = nullptr;
  h->tx_mutex = nullptr;
  hal_mutex_unlock(rx_mutex);
  hal_mutex_unlock(tx_mutex);
  hal_mutex_destroy(rx_mutex);
  hal_mutex_destroy(tx_mutex);
}

#if defined(JH_STM32G474_HOST_REGS)
/* Host tests stand in for the DMA: they need the rings it uses. */
uint8_t *jh_stm32g474_uart_test_rx_ring(hal_uart_t h) {
  return h != nullptr ? h->rx_ring : nullptr;
}
const uint8_t *jh_stm32g474_uart_test_tx_ring(hal_uart_t h) {
  return h != nullptr ? h->tx_ring : nullptr;
}
#endif

#endif /* HAL_ENABLE_UART */

#endif // HAL_TARGET_IS_STM32G474
