/**
 * @file g474_debug_uart.c
 * @brief USART2 debug console implementation (PA2/PA3, AF7) for STM32G474.
 *
 * Only built for the ARM hardware target (JH_STM32G474_HW).
 */

#ifdef JH_STM32G474_HW

#include "g474_debug_uart.h"
#include "stm32g474_gpio_af.h"
#include "stm32g474_regs.h"

#define PIN_TX 2u /* PA2 */
#define PIN_RX 3u /* PA3 */
#define AF7 7u    /* USART2 alternate function */

static int s_initialised = 0;
static volatile int s_app_owned = 0;

void g474_debug_uart_init(void) {
  if (s_initialised || s_app_owned) {
    return;
  }

  RCC_APB1ENR1 |= RCC_APB1ENR1_USART2EN;
  jh_stm32g474_gpio_set_af(PIN_TX, AF7);
  jh_stm32g474_gpio_set_af(PIN_RX, AF7);

  /* BRR = PCLK1 / baud with oversampling by 16. */
  USART2_CR1 = 0u;
  USART2_BRR = (JH_G474_PCLK1_HZ + 57600u) / 115200u;
  USART2_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

  s_initialised = 1;
}

void g474_debug_uart_putc(char c) {
  if (s_app_owned) {
    return;
  }
  if (!s_initialised) {
    g474_debug_uart_init();
  }
  while ((USART2_ISR & USART_ISR_TXE) == 0u) {
    /* wait for TX register empty */
  }
  USART2_TDR = (uint32_t)(uint8_t)c;
}

void g474_debug_uart_flush(void) {
  if (!s_initialised || s_app_owned) {
    return;
  }
  while ((USART2_ISR & USART_ISR_TC) == 0u) {
    /* wait for the final stop bit to leave the transmitter */
  }
}

void g474_debug_uart_puts(const char *s) {
  if (s == 0) {
    return;
  }
  while (*s) {
    g474_debug_uart_putc(*s++);
  }
}

int g474_debug_uart_getc_nonblock(void) {
  if (s_app_owned) {
    return -1;
  }
  if ((USART2_ISR & USART_ISR_ORE_F) != 0u) {
    USART_ICR(USART2_BASE) = USART_ICR_ORECF_F;
  }

  if ((USART2_ISR & USART_ISR_RXNE_F) == 0u) {
    return -1;
  }

  return (int)(USART_RDR(USART2_BASE) & 0xFFu);
}

void g474_debug_uart_set_app_owned(int owned) {
  if (owned) {
    g474_debug_uart_flush();
    s_app_owned = 1;
  } else {
    s_app_owned = 0;
  }
  /* Either way the next console output starts from its own settings. */
  s_initialised = 0;
}

int g474_debug_uart_is_app_owned(void) { return s_app_owned; }

void g474_debug_uart_put_u32(uint32_t v) {
  char buf[10];
  int i = 0;
  if (v == 0u) {
    g474_debug_uart_putc('0');
    return;
  }
  while (v > 0u && i < (int)sizeof(buf)) {
    buf[i++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (i > 0) {
    g474_debug_uart_putc(buf[--i]);
  }
}

void g474_debug_uart_put_hex32(uint32_t v) {
  static const char hex[] = "0123456789ABCDEF";
  g474_debug_uart_puts("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    g474_debug_uart_putc(hex[(v >> shift) & 0xFu]);
  }
}

#endif /* JH_STM32G474_HW */
