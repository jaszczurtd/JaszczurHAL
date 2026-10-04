// Lock-free reads of the STM32G474 UART backend against preemption. The
// backend is compiled into this file with its atomic loads routed through a
// wrapper that runs another task's work right after a chosen load, so the
// reader sees the counters move between its loads exactly where a task
// switch could put it.

#include "hal/core/hal_compiler.h"

#include <stdint.h>
#include <type_traits>

static const volatile void *s_preempt_after;
static void (*s_preempt)(void);

/* The load the backend would make, kept before the macro is replaced. */
template <typename T>
static std::remove_cv_t<T> hal_load(T *object, int order) {
  return HAL_ATOMIC_LOAD(object, order);
}

template <typename T>
static std::remove_cv_t<T> preemptible_load(T *object, int order) {
  const std::remove_cv_t<T> value = hal_load(object, order);
  if (s_preempt != nullptr &&
      (const volatile void *)object == s_preempt_after) {
    void (*preempt)(void) = s_preempt;
    s_preempt = nullptr;
    preempt();
  }
  return value;
}

#undef HAL_ATOMIC_LOAD
#define HAL_ATOMIC_LOAD(object, order) preemptible_load((object), (order))

#include "hal/impl/stm32g474/hal_uart.cpp"

#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#define TX_CH 3u /* DMA2 channel 4 sends for USART2 */

extern "C" {
uint32_t hal_millis(void) {
  static uint32_t now;
  return ++now;
}
void g474_debug_uart_set_app_owned(int owned) { (void)owned; }
}

static hal_uart_t s_uart;
static uint8_t s_data[HAL_UART_TX_BUFFER_SIZE];

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  s_preempt = nullptr;
  s_uart = hal_uart_create(HAL_UART_PORT_2, 3u, 2u);
  TEST_ASSERT_NOT_NULL(s_uart);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_uart_begin(s_uart, 3000000u, HAL_UART_CFG_8N1));
}

void tearDown(void) {
  USART_ISR(USART2_BASE) = USART_ISR_TC;
  hal_uart_destroy(s_uart);
}

static void tx_complete(void) {
  DMA_ISR(DMA2_BASE) = DMA_FLAG_TCIF(TX_CH);
  DMA2_Channel4_IRQHandler();
  DMA_ISR(DMA2_BASE) = 0u;
}

/* The DMA sends the full queue, another task fills it again, and the DMA
 * sends that too: both counters move on by a whole queue. */
static void drain_refill_drain(void) {
  tx_complete();
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, s_data, TX_SIZE));
  tx_complete();
}

static void expect_free_after_preemption(const volatile void *after) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_try_write_ex(s_uart, s_data, TX_SIZE));
  s_preempt_after = after;
  s_preempt = drain_refill_drain;
  size_t room = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_uart_tx_free_ex(s_uart, &room));
  TEST_ASSERT_NULL(s_preempt); /* the preemption did happen */
  TEST_ASSERT_EQUAL_UINT32(TX_SIZE, room);
}

void test_free_room_is_consistent_when_preempted_after_the_head(void) {
  expect_free_after_preemption(&s_uart->tx_head);
}

void test_free_room_is_consistent_when_preempted_after_the_tail(void) {
  expect_free_after_preemption(&s_uart->tx_tail);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_free_room_is_consistent_when_preempted_after_the_head);
  RUN_TEST(test_free_room_is_consistent_when_preempted_after_the_tail);
  return UNITY_END();
}
