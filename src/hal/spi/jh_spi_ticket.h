#ifndef JH_SPI_TICKET_H
#define JH_SPI_TICKET_H

#include "hal/core/hal_compiler.h"
#include "hal/core/hal_layout_check.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief FIFO admission to a shared SPI mutex. Initialize both counters to
 * zero before use; do not reset them while a caller holds or awaits a turn. */
typedef struct {
  uint32_t next;
  uint32_t serving;
} jh_spi_ticket_t;

/** @brief Reserve a turn; @p queue is non-NULL. Counters wrap modulo 2^32. */
static inline uint32_t jh_spi_ticket_reserve(jh_spi_ticket_t *queue) {
  return HAL_ATOMIC_FETCH_ADD(&queue->next, 1u, HAL_ATOMIC_RELAXED);
}

/** @brief Acquire prior writes when @p ticket becomes current. The non-NULL
 * @p queue must be the one that issued this ticket. */
static inline bool jh_spi_ticket_ready(const jh_spi_ticket_t *queue,
                                       uint32_t ticket) {
  return HAL_ATOMIC_LOAD(&queue->serving, HAL_ATOMIC_ACQUIRE) == ticket;
}

/** @brief Publish the current owner's writes and admit the next caller.
 * @p queue is non-NULL; only its current owner may call this function. */
static inline void jh_spi_ticket_release(jh_spi_ticket_t *queue) {
  (void)HAL_ATOMIC_FETCH_ADD(&queue->serving, 1u, HAL_ATOMIC_RELEASE);
}

#endif
