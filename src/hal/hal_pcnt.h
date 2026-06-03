#pragma once

/**
 * @file hal_pcnt.h
 * @brief Hardware abstraction for edge/pulse counting.
 *
 * Opt-in module: enable with `HAL_ENABLE_PCNT`.
 *
 * Counts edges on an input pin into a free-running 32-bit counter. The backend
 * strategy differs per target but the contract is identical:
 *   - STM32G474 : hardware timer in external-clock mode (channel 0 = TIM2 on
 *                 PA0) — zero CPU per edge, high input frequency.
 *   - RP2040    : software counter driven by a GPIO edge interrupt — simple and
 *                 portable, but limited by ISR rate (avoid very high frequencies).
 *   - mock      : in-memory counter with a test injection helper.
 *
 * Channel numbering is uniform (0,1,...). hal_pcnt_init() returns false if the
 * channel is not available on the current target; check it in portable code.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Which transition(s) increment the counter. */
typedef enum {
    HAL_PCNT_EDGE_RISING  = 0,
    HAL_PCNT_EDGE_FALLING = 1,
    HAL_PCNT_EDGE_BOTH    = 2,
} hal_pcnt_edge_t;

/**
 * @brief Whether this target can count pulses at all.
 * @return true on every current backend (hardware or ISR-based).
 */
bool hal_pcnt_is_supported(void);

/**
 * @brief Number of counter channels available on this target.
 */
uint8_t hal_pcnt_channel_count(void);

/**
 * @brief Initialise a counter channel on @p pin counting @p edge transitions.
 * @param channel Channel index (0-based).
 * @param pin     Input pin (backend pin convention; on STM32 = port*16+pin).
 * @param edge    Edge(s) to count.
 * @return true on success; false if the channel/pin is unsupported.
 */
bool hal_pcnt_init(uint8_t channel, uint8_t pin, hal_pcnt_edge_t edge);

/**
 * @brief Current accumulated edge count.
 * @param channel Channel index.
 * @return Count, or 0 for an invalid/uninitialised channel.
 */
uint32_t hal_pcnt_read(uint8_t channel);

/**
 * @brief Reset the counter to zero.
 * @param channel Channel index.
 */
void hal_pcnt_reset(uint8_t channel);

/**
 * @brief Read the count and atomically reset it to zero.
 * @param channel Channel index.
 * @return The count captured before the reset.
 */
uint32_t hal_pcnt_read_and_reset(uint8_t channel);

#ifdef __cplusplus
}
#endif
