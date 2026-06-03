/**
 * @file g474_debug_uart.c
 * @brief USART2 debug console implementation (PA2/PA3, AF7) for STM32G474.
 *
 * Only built for the ARM hardware target (JH_STM32G474_HW).
 */

#ifdef JH_STM32G474_HW

#include "g474_debug_uart.h"
#include "stm32g474_regs.h"

#define GPIOA_INDEX 0u
#define PIN_TX      2u   /* PA2 */
#define PIN_RX      3u   /* PA3 */
#define AF7         7u   /* USART2 alternate function */

static int s_initialised = 0;

static void set_af(uint32_t port, uint32_t pin, uint32_t af)
{
    /* Alternate-function mode in MODER. */
    GPIO_MODER(port) = (GPIO_MODER(port) & ~(0x3u << (pin * 2u))) |
                       (GPIO_MODE_AF << (pin * 2u));
    /* AF selection in AFRL (pins 0-7) or AFRH (pins 8-15). */
    if (pin < 8u) {
        GPIO_AFRL(port) = (GPIO_AFRL(port) & ~(0xFu << (pin * 4u))) |
                          (af << (pin * 4u));
    } else {
        const uint32_t p = pin - 8u;
        GPIO_AFRH(port) = (GPIO_AFRH(port) & ~(0xFu << (p * 4u))) |
                          (af << (p * 4u));
    }
}

void g474_debug_uart_init(void)
{
    if (s_initialised) {
        return;
    }

    /* Clock the GPIOA port and USART2. */
    RCC_AHB2ENR  |= RCC_AHB2ENR_GPIOAEN;
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART2EN;

    set_af(GPIOA_INDEX, PIN_TX, AF7);
    set_af(GPIOA_INDEX, PIN_RX, AF7);

    /* USART2 clocked from PCLK1 = 16 MHz (HSI16, no prescaler after reset).
     * BRR = fck / baud with oversampling by 16. */
    USART2_CR1 = 0u;
    USART2_BRR = JH_G474_CORE_CLOCK_HZ / 115200u;   /* 16e6 / 115200 = 139 */
    USART2_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    s_initialised = 1;
}

void g474_debug_uart_putc(char c)
{
    while ((USART2_ISR & USART_ISR_TXE) == 0u) {
        /* wait for TX register empty */
    }
    USART2_TDR = (uint32_t)(uint8_t)c;
}

void g474_debug_uart_puts(const char *s)
{
    if (s == 0) {
        return;
    }
    while (*s) {
        g474_debug_uart_putc(*s++);
    }
}

void g474_debug_uart_put_u32(uint32_t v)
{
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

void g474_debug_uart_put_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    g474_debug_uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        g474_debug_uart_putc(hex[(v >> shift) & 0xFu]);
    }
}

#endif /* JH_STM32G474_HW */
