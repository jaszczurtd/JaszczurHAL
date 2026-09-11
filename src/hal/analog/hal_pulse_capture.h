#pragma once

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configuration of the single hardware frequency input.
 * Enable HAL_ENABLE_PULSE_CAPTURE. Init/deinit belong to one owner; read is
 * serialized across tasks and must not be called from an ISR. Drain read until
 * HAL_EAGAIN at least once every 5 ms. Input high and low times must each be
 * at least 1 us, at no more than 100 kHz. A hardware service gap of 10 ms
 * is an overflow error. Backends reserve their peripheral until deinit.
 */
typedef struct {
  uint8_t pin;         /**< HAL GPIO number; STM32G474 accepts PA0 (0). */
  bool falling;        /**< Select falling rather than rising transitions. */
  uint32_t timeout_us; /**< Maximum gap and sample age, 1000..100000 us. */
} hal_pulse_capture_config_t;

/** @brief One disjoint interval of 32 complete input periods.
 * ticks / clock_hz is its duration; frequency = periods * clock_hz / ticks.
 * The first edge only primes the measurement. Counter wrap is handled.
 */
typedef struct {
  uint32_t ticks;    /**< Elapsed hardware timer ticks, nonzero. */
  uint32_t clock_hz; /**< Actual hardware timebase in ticks per second. */
  uint32_t
      measured_us;   /**< Conservative completion time in hal_micros units. */
  uint32_t sequence; /**< Sample number, beginning at 1 after init. */
  uint16_t periods;  /**< Number of complete periods (32). */
} hal_pulse_capture_sample_t;

/** @brief Start capturing an input with hardware timestamps.
 * @param config Non-NULL configuration, copied by the driver.
 * @return HAL_OK, HAL_EINVAL, HAL_EBUSY (resources owned), HAL_ENOMEM,
 * HAL_ECONFIG (required backend settings missing), or HAL_EHW.
 */
hal_status_t hal_pulse_capture_init(const hal_pulse_capture_config_t *config);

/** @brief Release capture resources, also after failed init; idempotent.
 * Single owner only. A stop error can be retried.
 * @return HAL_OK, or a backend stop error.
 */
hal_status_t hal_pulse_capture_deinit(void);

/** @brief Consume the oldest complete interval without waiting for an edge.
 * @param out Non-NULL destination; unchanged on any error.
 * @return HAL_OK, HAL_EAGAIN (partial/no interval), HAL_ETIMEOUT (stale or
 * interrupted interval), HAL_EOVERFLOW (data lost), HAL_EHW, HAL_EINVAL,
 * HAL_EUNINIT, HAL_ECONFIG (timebase changed). Overflow/hardware faults require
 * deinit/init. A timeout discards
 * the partial interval; the next edge primes a new interval. ESP32-S3 requires
 * capture IRQ service before the next prescaled event; see backend limits.
 */
hal_status_t hal_pulse_capture_read(hal_pulse_capture_sample_t *out);

#ifdef __cplusplus
}
#endif
