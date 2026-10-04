// The STM32G474 UART backend on a register table. The test stands in for the
// DMA: it writes received bytes into the ring and counts the channel down,
// raises the half/full events (or leaves them pending, as a late interrupt
// would), and completes transmit chunks. Checked: the USART, DMAMUX and DMA
// programming of both ports, the receive position across laps and pending
// events, overrun recovery, frame formats, the transmit queue and its chunk
// chaining, line error counting, pin tables and the console hand-over. The
// NVIC enable/disable/pending banks are modelled as write-one registers, and
// a read hook lets the DMA move on while a read is copying (a preempted
// reader). A second thread holds a blocking write to check that the
// non-blocking calls do not wait for it.

#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "hal/serial/hal_uart.h"
#include "hal/system/hal_system.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#include <atomic>
#include <chrono>
#include <string.h>
#include <thread>

#define RX_SIZE ((uint32_t)HAL_UART_RX_BUFFER_SIZE)
#define TX_SIZE ((uint32_t)HAL_UART_TX_BUFFER_SIZE)

/* USART2: DMA2 channel 3 receives, channel 4 sends (0-based 2 and 3). */
#define RX_CH 2u
#define TX_CH 3u
#define PA2 2u
#define PA3 3u
#define PA9 9u
#define PA10 10u
#define NONE 255u

extern "C" {
void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
void DMA2_Channel1_IRQHandler(void);
void DMA2_Channel3_IRQHandler(void);
void DMA2_Channel4_IRQHandler(void);
}
uint8_t *jh_stm32g474_uart_test_rx_ring(hal_uart_t h);
const uint8_t *jh_stm32g474_uart_test_tx_ring(hal_uart_t h);

static std::atomic<uint32_t> s_millis;
/* hal_millis() stands still until the test moves s_millis. */
static std::atomic<bool> s_frozen_clock;
static int s_console_owner_calls;
static int s_console_app_owned;
/* Millisecond counts at which the TX chunk in flight completes, as the DMA
 * interrupt would during a blocking wait; 0 ends the list. */
static uint32_t s_tx_complete_at[4];

static void tx_complete(void);

extern "C" {
uint32_t hal_millis(void) {
  if (s_frozen_clock) {
    return s_millis;
  }
  ++s_millis;
  for (uint32_t &at : s_tx_complete_at) {
    if (at != 0u && at == s_millis) {
      at = 0u;
      tx_complete();
    }
  }
  return s_millis;
}
void g474_debug_uart_set_app_owned(int owned) {
  ++s_console_owner_calls;
  s_console_app_owned = owned;
}
}

static hal_uart_t s_uart;
static uint32_t s_rx_total; /* bytes the simulated DMA has received */

#define NO_IRQ 0xFFFFFFFFu
static uint32_t s_nvic_enabled[4];
static uint32_t s_nvic_pending[4];
/* This IRQ is raised while it is masked (its event arrives meanwhile). */
static uint32_t s_raise_while_masked = NO_IRQ;

static void receive(uint32_t count, bool events);

/* Reads of the receive channel's CNDTR to let pass before the DMA writes
 * s_jump_bytes more at once (negative: never). */
static int s_cndtr_reads_before_jump = -1;
static uint32_t s_jump_bytes;

static uint32_t dma_read(uintptr_t address, uint32_t cell) {
  if (address != DMA_CNDTR_ADDR(DMA2_BASE, RX_CH) ||
      s_cndtr_reads_before_jump < 0) {
    return cell;
  }
  if (s_cndtr_reads_before_jump-- > 0) {
    return cell;
  }
  receive(s_jump_bytes, true);
  return DMA_CNDTR(DMA2_BASE, RX_CH);
}

static void nvic_write(uintptr_t address, uint32_t value, uint32_t *cell) {
  *cell = value;
  for (uint32_t n = 0u; n < 4u; n++) {
    if (address == NVIC_ISER_ADDR(n)) {
      s_nvic_enabled[n] |= value;
    } else if (address == NVIC_ICER_ADDR(n)) {
      s_nvic_enabled[n] &= ~value;
      if (s_raise_while_masked / 32u == n &&
          (value & (1u << (s_raise_while_masked % 32u))) != 0u) {
        s_nvic_pending[n] |= value;
      }
    } else if (address == NVIC_ICPR_ADDR(n)) {
      s_nvic_pending[n] &= ~value;
    }
  }
}

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  jh_stm32g474_host_regs_set_hooks(dma_read, nvic_write);
  s_cndtr_reads_before_jump = -1;
  s_frozen_clock = false;
  memset(s_nvic_enabled, 0, sizeof(s_nvic_enabled));
  memset(s_nvic_pending, 0, sizeof(s_nvic_pending));
  s_raise_while_masked = NO_IRQ;
  s_millis = 0u;
  memset(s_tx_complete_at, 0, sizeof(s_tx_complete_at));
  s_console_owner_calls = 0;
  s_console_app_owned = -1;
  s_uart = NULL;
  s_rx_total = 0u;
}

void tearDown(void) {
  if (s_uart != NULL) {
    USART_ISR(USART2_BASE) = USART_ISR_TC;
    hal_uart_destroy(s_uart);
    s_uart = NULL;
  }
}

static bool irq_enabled(uint32_t irqn) {
  return (s_nvic_enabled[irqn / 32u] & (1u << (irqn % 32u))) != 0u;
}

static bool irq_disabled(uint32_t irqn) { return !irq_enabled(irqn); }

static bool irq_pending(uint32_t irqn) {
  return (s_nvic_pending[irqn / 32u] & (1u << (irqn % 32u))) != 0u;
}

static void open_port2(uint32_t baud, uint16_t config) {
  s_uart = hal_uart_create(HAL_UART_PORT_2, PA3, PA2);
  TEST_ASSERT_NOT_NULL(s_uart);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_begin(s_uart, baud, config));
}

static uint8_t pattern(uint32_t n) { return (uint8_t)(n * 7u + 3u); }

static void rx_interrupt(uint32_t flags) {
  DMA_ISR(DMA2_BASE) = flags;
  DMA2_Channel3_IRQHandler();
  DMA_ISR(DMA2_BASE) = 0u;
}

/* The DMA receives @p count bytes of the pattern. With @p events false the
 * half/full interrupts stay pending (not delivered yet). */
static void receive(uint32_t count, bool events) {
  uint8_t *ring = jh_stm32g474_uart_test_rx_ring(s_uart);
  for (uint32_t i = 0u; i < count; i++) {
    const uint32_t position = s_rx_total & (RX_SIZE - 1u);
    ring[position] = pattern(s_rx_total);
    ++s_rx_total;
    const uint32_t next = s_rx_total & (RX_SIZE - 1u);
    DMA_CNDTR(DMA2_BASE, RX_CH) = RX_SIZE - next;
    if (events && next == RX_SIZE / 2u) {
      rx_interrupt(DMA_FLAG_HTIF(RX_CH) | DMA_FLAG_GIF(RX_CH));
    } else if (events && next == 0u) {
      rx_interrupt(DMA_FLAG_TCIF(RX_CH) | DMA_FLAG_GIF(RX_CH));
    }
  }
}

static void expect_read(uint32_t first, uint32_t count) {
  static uint8_t buffer[2u * HAL_UART_RX_BUFFER_SIZE];
  size_t got = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_read_bytes_ex(s_uart, buffer, count, &got));
  TEST_ASSERT_EQUAL_UINT32(count, got);
  for (uint32_t i = 0u; i < count; i++) {
    TEST_ASSERT_EQUAL_HEX8(pattern(first + i), buffer[i]);
  }
}

/* The chunk the TX channel is sending, located through its memory address. */
static const uint8_t *tx_chunk(void) {
  const uint8_t *ring = jh_stm32g474_uart_test_tx_ring(s_uart);
  const uint32_t offset =
      DMA_CMAR(DMA2_BASE, TX_CH) - (uint32_t)(uintptr_t)ring;
  TEST_ASSERT_TRUE(offset < TX_SIZE);
  return ring + offset;
}

static void tx_complete(void) {
  DMA_ISR(DMA2_BASE) = DMA_FLAG_TCIF(TX_CH) | DMA_FLAG_GIF(TX_CH);
  DMA2_Channel4_IRQHandler();
  DMA_ISR(DMA2_BASE) = 0u;
}

void test_begin_programs_usart2_dma_and_interrupts(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);

  TEST_ASSERT_EQUAL_UINT32((JH_G474_PCLK1_HZ + 1500000u) / 3000000u,
                           USART_BRR(USART2_BASE));
  TEST_ASSERT_EQUAL_HEX32(USART_CR1_RE_BIT | USART_CR1_TE_BIT | USART_CR1_UE,
                          USART_CR1(USART2_BASE));
  TEST_ASSERT_EQUAL_HEX32(0u, USART_CR2(USART2_BASE));
  TEST_ASSERT_EQUAL_HEX32(USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_EIE,
                          USART_CR3(USART2_BASE));
  TEST_ASSERT_TRUE((RCC_APB1ENR1 & RCC_APB1ENR1_USART2EN) != 0u);

  TEST_ASSERT_EQUAL_UINT32(DMA_REQUEST_USART2_RX, DMAMUX_CCR(8u + RX_CH));
  TEST_ASSERT_EQUAL_UINT32(DMA_REQUEST_USART2_TX, DMAMUX_CCR(8u + TX_CH));
  TEST_ASSERT_EQUAL_HEX32(DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_HTIE |
                              DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_PL_HIGH |
                              DMA_CCR_EN,
                          DMA_CCR(DMA2_BASE, RX_CH));
  TEST_ASSERT_EQUAL_UINT32(RX_SIZE, DMA_CNDTR(DMA2_BASE, RX_CH));
  TEST_ASSERT_EQUAL_UINT32(
      (uint32_t)(uintptr_t)jh_stm32g474_uart_test_rx_ring(s_uart),
      DMA_CMAR(DMA2_BASE, RX_CH));
  TEST_ASSERT_EQUAL_UINT32(
      (uint32_t)(uintptr_t)jh_stm32g474_host_reg32(USART2_BASE + 0x24u),
      DMA_CPAR(DMA2_BASE, RX_CH));
  TEST_ASSERT_EQUAL_UINT32(
      (uint32_t)(uintptr_t)jh_stm32g474_host_reg32(USART2_BASE + 0x28u),
      DMA_CPAR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_HEX32(0u, DMA_CCR(DMA2_BASE, TX_CH));

  TEST_ASSERT_TRUE(irq_enabled(USART2_IRQn));
  TEST_ASSERT_TRUE(irq_enabled(DMA2_Channel3_IRQn));
  TEST_ASSERT_TRUE(irq_enabled(DMA2_Channel4_IRQn));
  TEST_ASSERT_EQUAL_HEX8(0x80u, NVIC_IPR8(USART2_IRQn));
  TEST_ASSERT_EQUAL_HEX8(0x80u, NVIC_IPR8(DMA2_Channel3_IRQn));
  TEST_ASSERT_EQUAL_HEX8(0x80u, NVIC_IPR8(DMA2_Channel4_IRQn));
  /* PA2 and PA3 on AF7. */
  TEST_ASSERT_EQUAL_HEX32(0x7700u, GPIO_AFRL(0u) & 0xFF00u);
  TEST_ASSERT_EQUAL_INT(1, s_console_app_owned);

  hal_uart_destroy(s_uart);
  s_uart = NULL;
  TEST_ASSERT_EQUAL_INT(0, s_console_app_owned);
  TEST_ASSERT_EQUAL_HEX32(0u, USART_CR1(USART2_BASE));
  TEST_ASSERT_EQUAL_HEX32(0u, DMA_CCR(DMA2_BASE, RX_CH));
  TEST_ASSERT_EQUAL_UINT32(0u, DMAMUX_CCR(8u + RX_CH));
  TEST_ASSERT_TRUE(irq_disabled(USART2_IRQn));
  TEST_ASSERT_TRUE(irq_disabled(DMA2_Channel3_IRQn));
  TEST_ASSERT_TRUE(irq_disabled(DMA2_Channel4_IRQn));
}

void test_port1_uses_usart1_on_dma2_channels_1_and_2(void) {
  hal_uart_t uart = hal_uart_create(HAL_UART_PORT_1, PA10, PA9);
  TEST_ASSERT_NOT_NULL(uart);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(uart, 1000000u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_UINT32((JH_G474_PCLK2_HZ + 500000u) / 1000000u,
                           USART_BRR(USART1_BASE));
  TEST_ASSERT_TRUE((RCC_APB2ENR & RCC_APB2ENR_USART1EN) != 0u);
  TEST_ASSERT_EQUAL_UINT32(DMA_REQUEST_USART1_RX, DMAMUX_CCR(8u));
  TEST_ASSERT_EQUAL_UINT32(DMA_REQUEST_USART1_TX, DMAMUX_CCR(9u));
  TEST_ASSERT_TRUE((DMA_CCR(DMA2_BASE, 0u) & DMA_CCR_EN) != 0u);
  TEST_ASSERT_TRUE(irq_enabled(USART1_IRQn));
  TEST_ASSERT_TRUE(irq_enabled(DMA2_Channel1_IRQn));
  TEST_ASSERT_TRUE(irq_enabled(DMA2_Channel2_IRQn));
  /* PA9 and PA10 on AF7; the console keeps USART2. */
  TEST_ASSERT_EQUAL_HEX32(0x770u, GPIO_AFRH(0u) & 0xFF0u);
  TEST_ASSERT_EQUAL_INT(0, s_console_owner_calls);

  /* Its receive interrupt counts its own laps. */
  DMA_CNDTR(DMA2_BASE, 0u) = RX_SIZE / 2u;
  DMA_ISR(DMA2_BASE) = DMA_FLAG_HTIF(0u);
  DMA2_Channel1_IRQHandler();
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u), hal_uart_available(uart));

  USART_ISR(USART1_BASE) = USART_ISR_TC;
  hal_uart_destroy(uart);
  TEST_ASSERT_EQUAL_INT(0, s_console_owner_calls);
}

void test_bytes_are_read_in_order_across_ring_laps(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  uint8_t byte = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_uart_read_ex(s_uart, &byte));
  TEST_ASSERT_EQUAL_INT(-1, hal_uart_read(s_uart));

  uint32_t read = 0u;
  for (int round = 0; round < 7; round++) {
    const uint32_t count = 300u + (uint32_t)round * 13u;
    receive(count, true);
    TEST_ASSERT_EQUAL_INT((int)count, hal_uart_available(s_uart));
    expect_read(read, count);
    read += count;
  }
  TEST_ASSERT_TRUE(read > 3u * RX_SIZE);
  TEST_ASSERT_EQUAL_INT(0, hal_uart_available(s_uart));

  receive(1u, true);
  TEST_ASSERT_EQUAL_INT(pattern(read), hal_uart_read(s_uart));

  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(0u, counters.rx_buffer_overflow);
}

void test_a_pending_half_or_full_event_is_counted_from_the_position(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  /* Past the half: the half event is still pending. */
  receive(RX_SIZE / 2u + 10u, false);
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u + 10u), hal_uart_available(s_uart));
  rx_interrupt(DMA_FLAG_HTIF(RX_CH));
  expect_read(0u, RX_SIZE / 2u + 10u);

  /* Past the end of the ring: the full event is still pending. */
  receive(RX_SIZE / 2u + 30u, false);
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u + 30u), hal_uart_available(s_uart));
  rx_interrupt(DMA_FLAG_TCIF(RX_CH));
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u + 30u), hal_uart_available(s_uart));
  expect_read(RX_SIZE / 2u + 10u, RX_SIZE / 2u + 30u);
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(0u, counters.rx_buffer_overflow);
}

void test_bytes_the_dma_reached_during_the_copy_are_not_returned(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(100u, true);
  /* While the 100 bytes are copied the DMA writes 450 more: the slots of the
   * first 54 copied bytes may already hold new data. */
  s_jump_bytes = 450u;
  s_cndtr_reads_before_jump = 1;
  static uint8_t buffer[100];
  size_t got = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_read_bytes_ex(s_uart, buffer, 100u, &got));
  TEST_ASSERT_EQUAL_UINT32(46u, got);
  for (uint32_t i = 0u; i < 46u; i++) {
    TEST_ASSERT_EQUAL_HEX8(pattern(54u + i), buffer[i]);
  }
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(54u, counters.rx_buffer_overflow);
  /* The rest follows in order. */
  TEST_ASSERT_EQUAL_INT(450, hal_uart_available(s_uart));
  expect_read(100u, 450u);
}

void test_a_read_overtaken_by_a_whole_lap_starts_over(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(100u, true);
  /* A whole ring arrives during the copy: none of it can be trusted. */
  s_jump_bytes = RX_SIZE;
  s_cndtr_reads_before_jump = 1;
  expect_read(100u + RX_SIZE - RX_SIZE / 2u, 100u);
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(100u + RX_SIZE - RX_SIZE / 2u,
                           counters.rx_buffer_overflow);
}

/* Core cycles for @p bytes of 8N1 at 3 Mbaud. */
static uint32_t cycles_for(uint32_t bytes) {
  return (uint32_t)((uint64_t)bytes * 10u * JH_G474_CORE_CLOCK_HZ / 3000000u);
}

static uint32_t overrun_events(void) {
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  return counters.rx_overrun;
}

#define CYCLES_AT_BEGIN 0x80000000u
#define BOTH_EVENTS (DMA_FLAG_HTIF(RX_CH) | DMA_FLAG_TCIF(RX_CH))

/* One and a half rings (768 bytes) is the most that cannot hide a lap. */
void test_a_late_interrupt_that_may_have_missed_a_lap_is_counted(void) {
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(770u, false);
  DWT_CYCCNT = CYCLES_AT_BEGIN + cycles_for(770u);
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(1u, overrun_events());
}

void test_a_late_interrupt_within_one_and_a_half_rings_counts_nothing(void) {
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(766u, false);
  DWT_CYCCNT = CYCLES_AT_BEGIN + cycles_for(766u);
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(0u, overrun_events());
  /* The overrun itself is counted in bytes, as always. */
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u), hal_uart_available(s_uart));
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(766u - RX_SIZE / 2u, counters.rx_buffer_overflow);
}

/* The cycle counter wraps every 2^32 cycles; after a longer quiet stretch
 * the millisecond clock decides. */
void test_a_late_interrupt_after_the_cycle_counter_wrapped_is_counted(void) {
  s_frozen_clock = true;
  s_millis = 1000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(1024u, false);
  /* About 26 s later: the counter went round once and shows 10 cycles. */
  s_millis = 1000u + 26000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN + 10u;
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(1u, overrun_events());
}

void test_a_long_quiet_stretch_at_a_slow_rate_counts_nothing(void) {
  s_frozen_clock = true;
  s_millis = 1000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(300u, HAL_UART_CFG_8N1);
  /* 20 s at 300 baud carry at most 600 bytes: no lap can hide. */
  receive(600u, false);
  s_millis = 1000u + 20000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN + 10u;
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(0u, overrun_events());
}

void test_a_long_quiet_stretch_that_could_hide_a_lap_is_counted(void) {
  s_frozen_clock = true;
  s_millis = 1000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(300u, HAL_UART_CFG_8N1);
  /* 26 s at 300 baud could carry 780 bytes, more than 1.5 rings. */
  receive(780u, false);
  s_millis = 1000u + 26000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN + 10u;
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(1u, overrun_events());
}

void test_an_interrupt_on_time_after_a_long_quiet_stretch_restarts_the_bound(
    void) {
  s_frozen_clock = true;
  s_millis = 1000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  /* Thirty quiet seconds, then a half event served on time. */
  s_millis = 1000u + 30000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN + 7u;
  receive(300u, true);
  /* A short mask after it. */
  receive(700u, false);
  DWT_CYCCNT = CYCLES_AT_BEGIN + 7u + cycles_for(700u);
  s_millis = s_millis + 3u;
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(0u, overrun_events());
}

void test_back_to_back_late_interrupts_each_start_a_new_bound(void) {
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(700u, false);
  DWT_CYCCNT = CYCLES_AT_BEGIN + cycles_for(700u);
  rx_interrupt(BOTH_EVENTS);
  receive(700u, false);
  DWT_CYCCNT = CYCLES_AT_BEGIN + cycles_for(1400u);
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(0u, overrun_events());
}

void test_a_lone_event_after_a_long_idle_counts_nothing(void) {
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  DWT_CYCCNT = CYCLES_AT_BEGIN + 10u * JH_G474_CORE_CLOCK_HZ;
  receive(300u, true);
  TEST_ASSERT_EQUAL_UINT32(0u, overrun_events());
}

void test_a_read_with_nothing_pending_restarts_the_bound(void) {
  s_frozen_clock = true;
  s_millis = 1000u;
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  /* Thirty idle seconds (the cycle counter went round), then a read: the
   * lap count is right at that moment, on both clocks. */
  s_millis = 1000u + 30000u;
  const uint32_t read_at = CYCLES_AT_BEGIN + 5u * JH_G474_CORE_CLOCK_HZ;
  DWT_CYCCNT = read_at;
  TEST_ASSERT_EQUAL_INT(0, hal_uart_available(s_uart));
  receive(600u, false);
  DWT_CYCCNT = read_at + cycles_for(600u);
  s_millis = s_millis + 2u;
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(0u, overrun_events());
}

void test_a_read_with_events_pending_does_not_restart_the_bound(void) {
  DWT_CYCCNT = CYCLES_AT_BEGIN;
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(900u, false);
  DWT_CYCCNT = CYCLES_AT_BEGIN + cycles_for(900u);
  /* A read while both events still wait for the interrupt. */
  DMA_ISR(DMA2_BASE) = BOTH_EVENTS;
  (void)hal_uart_available(s_uart);
  DWT_CYCCNT = DWT_CYCCNT + 10u;
  rx_interrupt(BOTH_EVENTS);
  TEST_ASSERT_EQUAL_UINT32(1u, overrun_events());
}

void test_overrun_keeps_the_newest_half_and_counts_what_was_lost(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  /* Up to the guard the reader still owns every byte. */
  receive(RX_SIZE - 16u, true);
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE - 16u), hal_uart_available(s_uart));
  /* One more and the older half is gone. */
  receive(1u, true);
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u), hal_uart_available(s_uart));
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(RX_SIZE - 15u - RX_SIZE / 2u,
                           counters.rx_buffer_overflow);
  expect_read(RX_SIZE - 15u - RX_SIZE / 2u, RX_SIZE / 2u);

  /* A whole lap and more unread. */
  receive(RX_SIZE + 100u, true);
  TEST_ASSERT_EQUAL_INT((int)(RX_SIZE / 2u), hal_uart_available(s_uart));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(RX_SIZE - 15u - RX_SIZE / 2u + RX_SIZE + 100u -
                               RX_SIZE / 2u,
                           counters.rx_buffer_overflow);
  expect_read(2u * RX_SIZE + 85u - RX_SIZE / 2u, RX_SIZE / 2u);

  /* Reading resumes normally afterwards. */
  receive(40u, true);
  expect_read(2u * RX_SIZE + 85u, 40u);
}

void test_slow_rates_use_the_prescaler_and_impossible_rates_are_refused(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  TEST_ASSERT_EQUAL_UINT32(0u, USART_PRESC(USART2_BASE));
  /* 1200 baud needs a divider above 65535 at full kernel clock. */
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 1200u, HAL_UART_CFG_8N1));
  const uint32_t div[] = {1u, 2u, 4u, 6u, 8u, 10u, 12u, 16u, 32u, 64u};
  uint32_t presc = 0u;
  while ((JH_G474_PCLK1_HZ / div[presc] + 600u) / 1200u > 0xFFFFu) {
    presc++;
  }
  TEST_ASSERT_TRUE(presc > 0u);
  TEST_ASSERT_EQUAL_UINT32(presc, USART_PRESC(USART2_BASE));
  TEST_ASSERT_EQUAL_UINT32((JH_G474_PCLK1_HZ / div[presc] + 600u) / 1200u,
                           USART_BRR(USART2_BASE));

  /* Faster than SYSCLK/16 or slower than the largest prescaler reaches. */
  const uint32_t brr = USART_BRR(USART2_BASE);
  TEST_ASSERT_EQUAL_INT(
      HAL_EUNSUPPORTED,
      hal_uart_begin(s_uart, JH_G474_PCLK1_HZ / 15u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_uart_begin(s_uart, 8u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_UINT32(brr, USART_BRR(USART2_BASE));
  /* SYSCLK/16 itself is reachable. */
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_uart_begin(s_uart, JH_G474_PCLK1_HZ / 16u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_UINT32(16u, USART_BRR(USART2_BASE));
  TEST_ASSERT_EQUAL_UINT32(0u, USART_PRESC(USART2_BASE));

  /* Stopping clears the prescaler for the console. */
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 1200u, HAL_UART_CFG_8N1));
  TEST_ASSERT_NOT_EQUAL(0u, USART_PRESC(USART2_BASE));
  hal_uart_destroy(s_uart);
  s_uart = NULL;
  TEST_ASSERT_EQUAL_UINT32(0u, USART_PRESC(USART2_BASE));
}

void test_frame_formats_set_word_length_parity_and_mask(void) {
  struct {
    uint16_t config;
    uint32_t cr1_frame;
    uint32_t cr2;
    uint8_t mask;
  } const cases[] = {
      {HAL_UART_CFG_8N1, 0u, 0u, 0xFFu},
      {HAL_UART_CFG_7N1, USART_CR1_M1, 0u, 0x7Fu},
      {HAL_UART_CFG_7E1, USART_CR1_PCE | USART_CR1_PEIE, 0u, 0x7Fu},
      {HAL_UART_CFG_8E1, USART_CR1_M0 | USART_CR1_PCE | USART_CR1_PEIE, 0u,
       0xFFu},
      {HAL_UART_CFG_8O2,
       USART_CR1_M0 | USART_CR1_PCE | USART_CR1_PS | USART_CR1_PEIE,
       USART_CR2_STOP_2, 0xFFu},
      {HAL_UART_CFG_6E1, USART_CR1_M1 | USART_CR1_PCE | USART_CR1_PEIE, 0u,
       0x3Fu},
  };
  s_uart = hal_uart_create(HAL_UART_PORT_2, PA3, PA2);
  TEST_ASSERT_NOT_NULL(s_uart);
  for (const auto &c : cases) {
    USART_ISR(USART2_BASE) = USART_ISR_TC;
    s_rx_total = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_begin(s_uart, 115200u, c.config));
    TEST_ASSERT_EQUAL_HEX32(c.cr1_frame | USART_CR1_RE_BIT | USART_CR1_TE_BIT |
                                USART_CR1_UE,
                            USART_CR1(USART2_BASE));
    TEST_ASSERT_EQUAL_HEX32(c.cr2, USART_CR2(USART2_BASE));
    /* The parity bit lands in the byte the DMA stores; it is not data. */
    jh_stm32g474_uart_test_rx_ring(s_uart)[0] = 0xC1u;
    DMA_CNDTR(DMA2_BASE, RX_CH) = RX_SIZE - 1u;
    TEST_ASSERT_EQUAL_INT(0xC1u & c.mask, hal_uart_read(s_uart));
  }

  /* Words the USART cannot carry leave the port as it was. */
  const uint32_t cr1 = USART_CR1(USART2_BASE);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_uart_begin(s_uart, 115200u, HAL_UART_CFG_5N1));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_uart_begin(s_uart, 115200u, HAL_UART_CFG_6N1));
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        hal_uart_begin(s_uart, 115200u, HAL_UART_CFG_5E1));
  TEST_ASSERT_EQUAL_HEX32(cr1, USART_CR1(USART2_BASE));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_uart_begin(s_uart, 0u, HAL_UART_CFG_8N1));
}

void test_queued_bytes_go_out_in_chunks_chained_at_the_wrap(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[HAL_UART_TX_BUFFER_SIZE];
  for (uint32_t i = 0u; i < TX_SIZE; i++) {
    data[i] = pattern(i);
  }

  /* The first write starts a chunk at once. */
  size_t written = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_write_ex(s_uart, data, 100u, &written));
  TEST_ASSERT_EQUAL_UINT32(100u, written);
  TEST_ASSERT_EQUAL_UINT32(100u, DMA_CNDTR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_HEX32(DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE |
                              DMA_CCR_TEIE | DMA_CCR_EN,
                          DMA_CCR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_MEMORY(data, tx_chunk(), 100u);

  /* Later bytes wait for the chunk in flight. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 300u));
  TEST_ASSERT_EQUAL_UINT32(100u, DMA_CNDTR(DMA2_BASE, TX_CH));
  tx_complete();
  TEST_ASSERT_EQUAL_UINT32(300u, DMA_CNDTR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_MEMORY(data, tx_chunk(), 300u);

  /* This write fills the queue across the end of the ring. */
  const uint32_t across = TX_SIZE - 300u;
  size_t free_bytes = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_tx_free_ex(s_uart, &free_bytes));
  TEST_ASSERT_EQUAL_UINT32(across, free_bytes);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_try_write_ex(s_uart, data + 1u, across));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_tx_free_ex(s_uart, &free_bytes));
  TEST_ASSERT_EQUAL_UINT32(0u, free_bytes);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_uart_try_write_ex(s_uart, data, 1u));

  /* Its first chunk ends at the end of the ring, the second starts at 0. */
  const uint32_t to_end = TX_SIZE - 400u;
  tx_complete();
  TEST_ASSERT_EQUAL_UINT32(to_end, DMA_CNDTR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_MEMORY(data + 1u, tx_chunk(), to_end);
  tx_complete();
  TEST_ASSERT_EQUAL_UINT32(across - to_end, DMA_CNDTR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_PTR(jh_stm32g474_uart_test_tx_ring(s_uart), tx_chunk());
  TEST_ASSERT_EQUAL_MEMORY(data + 1u + to_end, tx_chunk(), across - to_end);

  /* The last chunk leaves the channel idle. */
  tx_complete();
  TEST_ASSERT_EQUAL_HEX32(0u, DMA_CCR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_tx_free_ex(s_uart, &free_bytes));
  TEST_ASSERT_EQUAL_UINT32(TX_SIZE, free_bytes);
}

void test_dma_transfer_errors_are_counted_and_do_not_stall_the_queue(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[30];
  for (uint32_t i = 0u; i < sizeof(data); i++) {
    data[i] = pattern(i);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 10u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data + 10u, 20u));
  /* A transmit bus error drops that chunk; the queue moves on. */
  DMA_ISR(DMA2_BASE) = DMA_FLAG_TEIF(TX_CH) | DMA_FLAG_GIF(TX_CH);
  DMA2_Channel4_IRQHandler();
  TEST_ASSERT_EQUAL_UINT32(20u, DMA_CNDTR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_MEMORY(data + 10u, tx_chunk(), 20u);

  /* A receive bus error stops the channel; it is reported as an overrun. */
  rx_interrupt(DMA_FLAG_TEIF(RX_CH) | DMA_FLAG_GIF(RX_CH));
  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(1u, counters.rx_overrun);
}

void test_a_chunk_completing_while_masked_still_chains_the_next(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[40];
  for (uint32_t i = 0u; i < sizeof(data); i++) {
    data[i] = pattern(i);
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 20u));
  /* The first chunk finishes while the next write holds the mask. */
  s_raise_while_masked = DMA2_Channel4_IRQn;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data + 20u, 20u));
  TEST_ASSERT_TRUE(irq_enabled(DMA2_Channel4_IRQn));
  TEST_ASSERT_TRUE(irq_pending(DMA2_Channel4_IRQn));
  /* The pending request runs the handler, which sends the rest. */
  tx_complete();
  TEST_ASSERT_EQUAL_UINT32(20u, DMA_CNDTR(DMA2_BASE, TX_CH));
  TEST_ASSERT_EQUAL_MEMORY(data + 20u, tx_chunk(), 20u);
}

void test_try_write_is_all_or_nothing(void) {
  open_port2(1000000u, HAL_UART_CFG_8N1);
  static uint8_t data[HAL_UART_TX_BUFFER_SIZE + 1u];
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        hal_uart_try_write_ex(s_uart, data, TX_SIZE + 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_try_write_ex(s_uart, data, TX_SIZE - 10u));
  /* 10 bytes free: 11 is refused whole, 10 is taken. */
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, hal_uart_try_write_ex(s_uart, data, 11u));
  size_t free_bytes = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_tx_free_ex(s_uart, &free_bytes));
  TEST_ASSERT_EQUAL_UINT32(10u, free_bytes);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 10u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_uart_try_write_ex(s_uart, NULL, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_uart_tx_free_ex(s_uart, NULL));
}

void test_a_stalled_line_ends_blocking_writes_and_flush_with_a_timeout(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[HAL_UART_TX_BUFFER_SIZE + 40u];
  size_t written = 0u;
  /* Nothing completes: the queue fills and the write gives up. */
  TEST_ASSERT_EQUAL_INT(
      HAL_ETIMEOUT, hal_uart_write_ex(s_uart, data, sizeof(data), &written));
  TEST_ASSERT_EQUAL_UINT32(TX_SIZE, written);
  /* 512 bytes twice over at 3 Mbaud is 3 ms, plus 100 ms of slack. */
  TEST_ASSERT_TRUE(s_millis >= 103u && s_millis < 110u);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, hal_uart_flush(s_uart));

  /* The DMA is done but the last frame is still on the wire. */
  tx_complete();
  tx_complete();
  USART_ISR(USART2_BASE) = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, hal_uart_flush(s_uart));
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_flush(s_uart));
}

void test_line_errors_are_counted_and_cleared(void) {
  open_port2(3000000u, HAL_UART_CFG_8E1);
  USART_ICR(USART2_BASE) = 0u;
  USART_ISR(USART2_BASE) = USART_ISR_ORE_F | USART_ISR_FE_F | USART_ISR_PE_F;
  USART2_IRQHandler();
  TEST_ASSERT_EQUAL_HEX32(USART_ICR_ALL_ERRORS_F, USART_ICR(USART2_BASE));
  USART_ISR(USART2_BASE) = USART_ISR_NE_F;
  USART2_IRQHandler();
  USART_ISR(USART2_BASE) = USART_ISR_ORE_F;
  USART2_IRQHandler();

  hal_uart_error_counters_t counters = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(2u, counters.rx_overrun);
  TEST_ASSERT_EQUAL_UINT32(2u, counters.rx_framing);
  TEST_ASSERT_EQUAL_UINT32(1u, counters.rx_parity);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.rx_buffer_overflow);

  /* A new begin starts from zero. */
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 1000000u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_get_error_counters_ex(s_uart, &counters));
  TEST_ASSERT_EQUAL_UINT32(0u, counters.rx_overrun);
  TEST_ASSERT_EQUAL_UINT32((JH_G474_PCLK1_HZ + 500000u) / 1000000u,
                           USART_BRR(USART2_BASE));
}

void test_rebegin_restarts_reception_from_the_ring_start(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  receive(RX_SIZE / 2u + 50u, true);
  expect_read(0u, 20u);
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 1000000u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_UINT32(RX_SIZE, DMA_CNDTR(DMA2_BASE, RX_CH));
  TEST_ASSERT_EQUAL_INT(0, hal_uart_available(s_uart));
  s_rx_total = 0u;
  receive(5u, true);
  expect_read(0u, 5u);
}

void test_rebegin_lets_queued_bytes_leave_at_the_old_rate(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[20];
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 20u));
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  s_tx_complete_at[0] = s_millis + 30u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 9600u, HAL_UART_CFG_8N1));
  /* The new rate is set only after the chunk went out. */
  TEST_ASSERT_EQUAL_UINT32(0u, s_tx_complete_at[0]);
  TEST_ASSERT_EQUAL_UINT32((JH_G474_PCLK1_HZ + 4800u) / 9600u,
                           USART_BRR(USART2_BASE));
}

void test_blocking_write_times_out_only_without_progress(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[2u * HAL_UART_TX_BUFFER_SIZE + 40u];
  /* The line drains a full chunk at 80 ms and again at 150 ms: each wait
   * is shorter than the 103 ms timeout, the whole write is longer. */
  s_tx_complete_at[0] = 80u;
  s_tx_complete_at[1] = 150u;
  size_t written = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_uart_write_ex(s_uart, data, sizeof(data), &written));
  TEST_ASSERT_EQUAL_UINT32(sizeof(data), written);
}

void test_rebegin_keeps_the_port_when_queued_bytes_cannot_leave(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[HAL_UART_TX_BUFFER_SIZE];
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, TX_SIZE));
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  const uint32_t brr = USART_BRR(USART2_BASE);
  /* Nothing completes: the accepted bytes stay queued and the rate stays. */
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        hal_uart_begin(s_uart, 9600u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_UINT32(brr, USART_BRR(USART2_BASE));
  size_t free_bytes = 99u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_tx_free_ex(s_uart, &free_bytes));
  TEST_ASSERT_EQUAL_UINT32(0u, free_bytes);
  TEST_ASSERT_TRUE((DMA_CCR(DMA2_BASE, TX_CH) & DMA_CCR_EN) != 0u);
  TEST_ASSERT_TRUE((DMA_CCR(DMA2_BASE, RX_CH) & DMA_CCR_EN) != 0u);
  /* Once they leave, the change goes through. */
  tx_complete();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 9600u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_UINT32((JH_G474_PCLK1_HZ + 4800u) / 9600u,
                           USART_BRR(USART2_BASE));
}

void test_non_blocking_calls_do_not_wait_for_a_blocked_writer(void) {
  open_port2(3000000u, HAL_UART_CFG_8N1);
  static uint8_t data[HAL_UART_TX_BUFFER_SIZE + 40u];
  /* Every register the threads touch exists before they start. */
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, data, 1u));
  tx_complete();
  /* With the clock standing still the writer waits for room until the test
   * moves the clock on. */
  s_frozen_clock = true;

  std::atomic<bool> writer_done(false);
  hal_status_t writer_status = HAL_OK;
  std::thread writer([&] {
    writer_status = hal_uart_write_ex(s_uart, data, sizeof(data), NULL);
    writer_done = true;
  });
  size_t free_bytes = 1u;
  const auto fill_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < fill_deadline &&
         hal_uart_tx_free_ex(s_uart, &free_bytes) == HAL_OK &&
         free_bytes != 0u) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  /* Releases the writer after the checks, or after 3 s when a check waits
   * for it. */
  std::atomic<bool> checked(false);
  std::thread release([&] {
    const auto limit =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!checked && std::chrono::steady_clock::now() < limit) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    s_millis += 100000u;
  });

  const auto started = std::chrono::steady_clock::now();
  const hal_status_t tried = hal_uart_try_write_ex(s_uart, data, 1u);
  uint8_t byte = 0u;
  size_t got = 0u;
  const hal_status_t read = hal_uart_read_bytes_ex(s_uart, &byte, 1u, &got);
  const bool writer_still_waiting = !writer_done;
  const auto elapsed = std::chrono::steady_clock::now() - started;
  checked = true;
  release.join();
  writer.join();
  s_frozen_clock = false;

  TEST_ASSERT_EQUAL_UINT32(0u, free_bytes);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, tried);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, read);
  TEST_ASSERT_TRUE(writer_still_waiting);
  TEST_ASSERT_TRUE(elapsed < std::chrono::milliseconds(1500));
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, writer_status);
}

void test_pins_outside_the_af7_tables_are_refused(void) {
  TEST_ASSERT_NULL(hal_uart_create(HAL_UART_PORT_2, PA10, PA9));
  TEST_ASSERT_NULL(hal_uart_create(HAL_UART_PORT_1, PA3, PA2));
  TEST_ASSERT_NULL(hal_uart_create(HAL_UART_PORT_2, NONE, NONE));
  TEST_ASSERT_NULL(hal_uart_create((hal_uart_port_t)3, PA3, PA2));

  /* One direction may stay unconnected: a TX-only port does not start the
   * receive channel. */
  s_uart = hal_uart_create(HAL_UART_PORT_2, NONE, PA2);
  TEST_ASSERT_NOT_NULL(s_uart);
  TEST_ASSERT_NULL(hal_uart_create(HAL_UART_PORT_2, PA3, PA2));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 115200u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_HEX32(0u, DMA_CCR(DMA2_BASE, RX_CH) & DMA_CCR_EN);
  TEST_ASSERT_EQUAL_HEX32(USART_CR1_TE_BIT | USART_CR1_UE,
                          USART_CR1(USART2_BASE));

  /* Pins move only while stopped. */
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_uart_set_rx_ex(s_uart, PA10));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_uart_set_rx_ex(s_uart, PA3));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_set_tx_ex(s_uart, PA2));
}

void test_new_reads_check_their_arguments(void) {
  s_uart = hal_uart_create(HAL_UART_PORT_2, PA3, PA2);
  uint8_t buffer[4];
  size_t got = 7u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT,
                        hal_uart_read_bytes_ex(s_uart, buffer, 4u, &got));
  TEST_ASSERT_EQUAL_UINT32(0u, got);
  size_t free_bytes = 7u;
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_uart_tx_free_ex(s_uart, &free_bytes));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, hal_uart_try_write_ex(s_uart, buffer, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 115200u, HAL_UART_CFG_8N1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_uart_read_bytes_ex(s_uart, NULL, 4u, &got));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_read_bytes_ex(s_uart, NULL, 0u, &got));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_uart_read_bytes_ex(s_uart, buffer, 4u, NULL));
  receive(2u, true);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_read_bytes_ex(s_uart, buffer, 4u, &got));
  TEST_ASSERT_EQUAL_UINT32(2u, got);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_programs_usart2_dma_and_interrupts);
  RUN_TEST(test_port1_uses_usart1_on_dma2_channels_1_and_2);
  RUN_TEST(test_bytes_are_read_in_order_across_ring_laps);
  RUN_TEST(test_a_pending_half_or_full_event_is_counted_from_the_position);
  RUN_TEST(test_bytes_the_dma_reached_during_the_copy_are_not_returned);
  RUN_TEST(test_a_read_overtaken_by_a_whole_lap_starts_over);
  RUN_TEST(test_a_late_interrupt_that_may_have_missed_a_lap_is_counted);
  RUN_TEST(test_a_late_interrupt_within_one_and_a_half_rings_counts_nothing);
  RUN_TEST(test_a_late_interrupt_after_the_cycle_counter_wrapped_is_counted);
  RUN_TEST(test_a_long_quiet_stretch_at_a_slow_rate_counts_nothing);
  RUN_TEST(test_a_long_quiet_stretch_that_could_hide_a_lap_is_counted);
  RUN_TEST(
      test_an_interrupt_on_time_after_a_long_quiet_stretch_restarts_the_bound);
  RUN_TEST(test_back_to_back_late_interrupts_each_start_a_new_bound);
  RUN_TEST(test_a_lone_event_after_a_long_idle_counts_nothing);
  RUN_TEST(test_a_read_with_nothing_pending_restarts_the_bound);
  RUN_TEST(test_a_read_with_events_pending_does_not_restart_the_bound);
  RUN_TEST(test_overrun_keeps_the_newest_half_and_counts_what_was_lost);
  RUN_TEST(test_slow_rates_use_the_prescaler_and_impossible_rates_are_refused);
  RUN_TEST(test_frame_formats_set_word_length_parity_and_mask);
  RUN_TEST(test_queued_bytes_go_out_in_chunks_chained_at_the_wrap);
  RUN_TEST(test_dma_transfer_errors_are_counted_and_do_not_stall_the_queue);
  RUN_TEST(test_a_chunk_completing_while_masked_still_chains_the_next);
  RUN_TEST(test_try_write_is_all_or_nothing);
  RUN_TEST(test_a_stalled_line_ends_blocking_writes_and_flush_with_a_timeout);
  RUN_TEST(test_line_errors_are_counted_and_cleared);
  RUN_TEST(test_rebegin_restarts_reception_from_the_ring_start);
  RUN_TEST(test_rebegin_lets_queued_bytes_leave_at_the_old_rate);
  RUN_TEST(test_blocking_write_times_out_only_without_progress);
  RUN_TEST(test_rebegin_keeps_the_port_when_queued_bytes_cannot_leave);
  RUN_TEST(test_non_blocking_calls_do_not_wait_for_a_blocked_writer);
  RUN_TEST(test_pins_outside_the_af7_tables_are_refused);
  RUN_TEST(test_new_reads_check_their_arguments);
  return UNITY_END();
}
