#pragma once

/**
 * @file hal_debug_events.h
 * @brief Deferred diagnostic records for a time-critical producer.
 *
 * One producer copies fixed-size records into caller-owned storage. One
 * consumer formats and sends them later. Publishing never calls the consumer,
 * allocates memory, takes a mutex, or waits for space. A full queue drops the
 * new record and counts it; it must never postpone the operation being logged.
 * Use a separate queue for each concurrent producer, including nested IRQs.
 * Record occurrence timestamps and values at the producer, not at delivery.
 * Pointer members must remain valid until consumption; stack strings and
 * pointers to mutable state are not snapshots.
 */

#include "hal/core/hal_layout_check.h"
#include "hal/core/hal_status.h"
#include "hal/core/jh_spsc_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Consumer of one immutable record.
 * @param record Record in queue storage; valid only during this call.
 * @param context Consumer-owned context supplied to poll.
 * @return HAL_OK consumes the record. Any other status retains it for retry.
 * @note Formatting and output belong here. The callback must not recursively
 * poll or reset its queue. A retry must not repeat partially emitted output;
 * the consumer must retain its own output offset when its transport needs it.
 */
typedef hal_status_t (*hal_debug_event_consumer_t)(const void *record,
                                                   void *context);

/** @brief Queue state; initialize before either owner starts using it. */
typedef struct {
  jh_spsc_ring_t ring; /**< Fixed records, published with release/acquire. */
  uint32_t dropped;    /**< Producer-owned cumulative drops, modulo 2^32. */
} hal_debug_events_t;

/** @brief Attach storage and reset counters, with both owners stopped.
 * @param queue Queue state.
 * @param storage Storage for capacity * record_size bytes, suitably aligned
 * for the record type and valid for the entire queue lifetime.
 * @param record_size Size of one application-defined diagnostic record.
 * @param capacity Number of records, all usable.
 * @return HAL_OK, or HAL_EINVAL without changing queue state.
 */
static inline hal_status_t hal_debug_events_init(hal_debug_events_t *queue,
                                                 void *storage,
                                                 uint16_t record_size,
                                                 uint16_t capacity) {
  if (queue == NULL || storage == NULL || record_size == 0U || capacity == 0U) {
    return HAL_EINVAL;
  }
  jh_spsc_ring_init(&queue->ring, storage, record_size, capacity);
  queue->dropped = 0U;
  return HAL_OK;
}

/** @brief Producer: copy one record without formatting or waiting.
 * @param queue Initialized queue belonging to this producer.
 * @param record Complete record to copy; never retained by pointer.
 * @return HAL_OK, HAL_EAGAIN on overflow, or HAL_EINVAL for a null argument.
 * @note Runtime is bounded by one fixed-size copy and queue bookkeeping.
 * Overflow increments the diagnostic counter only. Callers must not retry
 * until success from a critical path or make control depend on this result.
 */
static inline hal_status_t hal_debug_events_publish(hal_debug_events_t *queue,
                                                    const void *record) {
  if (queue == NULL || record == NULL) {
    return HAL_EINVAL;
  }
  if (!jh_spsc_ring_push(&queue->ring, record)) {
    const uint32_t dropped =
        HAL_ATOMIC_LOAD(&queue->dropped, HAL_ATOMIC_RELAXED);
    HAL_ATOMIC_STORE(&queue->dropped, dropped + 1U, HAL_ATOMIC_RELAXED);
    return HAL_EAGAIN;
  }
  return HAL_OK;
}

/** @brief Read cumulative overflow count from either owner.
 * @param queue Initialized queue.
 * @return Drops modulo 2^32, or zero for a null queue.
 * @note Save the previous value at the consumer to report new drops. Reading
 * never clears the counter or contends with the producer through a RMW.
 */
static inline uint32_t
hal_debug_events_dropped(const hal_debug_events_t *queue) {
  return queue == NULL ? 0U
                       : HAL_ATOMIC_LOAD(&queue->dropped, HAL_ATOMIC_RELAXED);
}

/** @brief Consumer: deliver at most budget records present on entry.
 * @param queue Initialized queue belonging to this consumer.
 * @param budget Maximum callback calls; zero does no work.
 * @param consume Consumer callback, executed only by this function.
 * @param context Optional callback context.
 * @return HAL_OK after the bounded batch, HAL_EINVAL for null queue/callback,
 * or the first callback error, retaining that record.
 * @note This limits record count, not the duration of application callbacks.
 * A stalled callback can fill the queue, but cannot block its producer.
 */
static inline hal_status_t
hal_debug_events_poll(hal_debug_events_t *queue, uint32_t budget,
                      hal_debug_event_consumer_t consume, void *context) {
  if (queue == NULL || consume == NULL) {
    return HAL_EINVAL;
  }
  const uint32_t waiting = jh_spsc_ring_count(&queue->ring);
  const uint32_t count = budget < waiting ? budget : waiting;
  for (uint32_t i = 0U; i < count; ++i) {
    const void *record = jh_spsc_ring_peek(&queue->ring);
    if (record == NULL) {
      break;
    }
    const hal_status_t status = consume(record, context);
    if (status != HAL_OK) {
      return status;
    }
    jh_spsc_ring_drop(&queue->ring);
  }
  return HAL_OK;
}

#ifdef __cplusplus
}
#endif
