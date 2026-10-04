// The STM32G474 debug console (USART2) on a register table: it sets itself
// up on first output, goes silent while hal_uart PORT_2 owns USART2 without
// touching the application's settings, and comes back at 115200 once the
// application lets go.

#include "hal/impl/stm32g474/port/g474_debug_uart.h"
#include "hal/impl/stm32g474/port/stm32g474_regs.h"
#include "jh_stm32g474_host_regs.h"
#include "utils/unity.h"

#define CONSOLE_BRR ((JH_G474_PCLK1_HZ + 57600u) / 115200u)

void setUp(void) {
  jh_stm32g474_host_regs_reset();
  g474_debug_uart_set_app_owned(0);
  USART2_ISR = USART_ISR_TXE | USART_ISR_TC;
}

void tearDown(void) {}

void test_first_output_sets_the_console_up(void) {
  g474_debug_uart_putc('A');
  TEST_ASSERT_EQUAL_UINT32(CONSOLE_BRR, USART2_BRR);
  TEST_ASSERT_EQUAL_HEX32(USART_CR1_TE | USART_CR1_RE | USART_CR1_UE,
                          USART2_CR1);
  TEST_ASSERT_TRUE((RCC_APB1ENR1 & RCC_APB1ENR1_USART2EN) != 0u);
  TEST_ASSERT_EQUAL_HEX32('A', USART2_TDR);
  TEST_ASSERT_EQUAL_INT(0, g474_debug_uart_is_app_owned());
}

void test_application_ownership_silences_the_console(void) {
  g474_debug_uart_putc('A');
  g474_debug_uart_set_app_owned(1);
  TEST_ASSERT_NOT_EQUAL(0, g474_debug_uart_is_app_owned());

  /* The application's settings stay as it left them. */
  USART2_BRR = 57u;
  USART2_CR1 = 0x1234u;
  USART2_TDR = 0u;
  g474_debug_uart_init();
  g474_debug_uart_puts("log");
  g474_debug_uart_flush();
  TEST_ASSERT_EQUAL_UINT32(57u, USART2_BRR);
  TEST_ASSERT_EQUAL_HEX32(0x1234u, USART2_CR1);
  TEST_ASSERT_EQUAL_HEX32(0u, USART2_TDR);

  /* Its received bytes are not the console's. */
  USART2_ISR = USART_ISR_RXNE_F;
  USART_RDR(USART2_BASE) = 'x';
  TEST_ASSERT_EQUAL_INT(-1, g474_debug_uart_getc_nonblock());
}

void test_the_console_comes_back_at_its_own_rate(void) {
  g474_debug_uart_putc('A');
  g474_debug_uart_set_app_owned(1);
  USART2_BRR = 57u;
  USART2_CR1 = 0u; /* hal_uart_destroy stops the USART */
  g474_debug_uart_set_app_owned(0);
  g474_debug_uart_putc('B');
  TEST_ASSERT_EQUAL_UINT32(CONSOLE_BRR, USART2_BRR);
  TEST_ASSERT_EQUAL_HEX32(USART_CR1_TE | USART_CR1_RE | USART_CR1_UE,
                          USART2_CR1);
  TEST_ASSERT_EQUAL_HEX32('B', USART2_TDR);
  USART2_ISR = USART_ISR_TXE | USART_ISR_RXNE_F;
  USART_RDR(USART2_BASE) = 'y';
  TEST_ASSERT_EQUAL_INT('y', g474_debug_uart_getc_nonblock());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_first_output_sets_the_console_up);
  RUN_TEST(test_application_ownership_silences_the_console);
  RUN_TEST(test_the_console_comes_back_at_its_own_rate);
  return UNITY_END();
}
