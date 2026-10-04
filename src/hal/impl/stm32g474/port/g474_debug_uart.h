#pragma once

/**
 * @file g474_debug_uart.h
 * @brief Tiny, dependency-free USART2 debug console for the G474 bring-up.
 *
 * USART2 (PA2 TX / PA3 RX, AF7) is wired to the ST-Link Virtual COM Port on
 * the Nucleo-G474RE, so output appears on the host terminal with no extra
 * hardware. Kept independent of the higher-level hal_uart/hal_serial so the
 * fault-dump path has no allocation or driver dependencies.
 *
 * 115200 8N1, blocking TX. Only meaningful on the ARM target.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise USART2 @ 115200 8N1 on PA2/PA3. Safe to call more than once. */
void g474_debug_uart_init(void);

/** Blocking write of one byte; sets the console up first if needed. */
void g474_debug_uart_putc(char c);

/** Wait until every queued byte has left the USART transmitter. */
void g474_debug_uart_flush(void);

/** Blocking write of a NUL-terminated string. */
void g474_debug_uart_puts(const char *s);

/** Return the next received byte, or -1 when no byte is currently available. */
int g474_debug_uart_getc_nonblock(void);

/**
 * Hand USART2 to the application (hal_uart PORT_2) or take it back.
 *
 * Handing over first waits for the console's last byte to leave. While the
 * application owns USART2 every console call returns at once without
 * touching it: printf, HAL logs, assertion text and the fault dump write
 * nothing (the fault record itself survives the reset and is printed on the
 * next boot), and getc reports no byte. Taking it back reprograms the console
 * at 115200 on its next output.
 */
void g474_debug_uart_set_app_owned(int owned);

/** Nonzero while the application owns USART2 (see set_app_owned). */
int g474_debug_uart_is_app_owned(void);

/** Write an unsigned 32-bit value in decimal. */
void g474_debug_uart_put_u32(uint32_t v);

/** Write a 32-bit value as 8 uppercase hex digits prefixed with "0x". */
void g474_debug_uart_put_hex32(uint32_t v);

#ifdef __cplusplus
}
#endif
