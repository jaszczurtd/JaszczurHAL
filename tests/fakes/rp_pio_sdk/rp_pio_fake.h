#pragma once

/* Test control of the simulated PIO software UART and the DMA moving its
 * receive words. Frames reach the receive state machine listening on a pin
 * only when a test sends them. */

#include <stdint.h>

void jh_fake_pio_reset(void);

/* One 8N1 frame on @p pin; false when no receive state machine listens. */
bool jh_fake_uart_receive(uint8_t pin, uint8_t byte);
/* Frames lost because the receive FIFO was full. */
uint32_t jh_fake_uart_dropped(void);

/* COUNT field of the channel draining the state machine on @p pin (0 when
 * none runs). */
uint32_t jh_fake_dma_rx_remaining(uint8_t pin);
/* Let that channel move @p transfers frames of @p filler, as a long burst
 * nobody read would: the count drops and the ring keeps the newest words.
 * Returns the transfers performed (fewer when the run ends first). */
uint32_t jh_fake_dma_rx_elapse(uint8_t pin, uint32_t transfers, uint8_t filler);
