#pragma once

/**
 * @file jh_can_provider.h
 * @brief Internal interface between the CAN facade and its providers.
 *
 * Not part of the public API. The facade in hal_can.cpp owns the handle pool,
 * the per-handle lock, argument and mode validation, the started/stopped
 * state and the classic-API wrappers. A provider only drives its controller
 * through the operations below; it may assume the facade already validated
 * the arguments against the capabilities it reported from init().
 *
 * A provider that implements attach() runs from interrupts: it fills the
 * receive and event rings of the facade-owned jh_can_queues_t and drains its
 * transmit ring. The facade then reads received frames from the ring and no
 * longer calls receive_frame() or available().
 */

#include "hal/can/hal_can.h"
#include "hal/core/hal_status.h"
#include "hal/core/jh_spsc_ring.h"
#include "hal/system/hal_sync.h"

#ifdef HAL_ENABLE_CAN

#include <stddef.h>

/** @brief What a channel accepts, reported by the provider at init. */
typedef struct {
  /** Mode flags hal_can_set_mode() may combine on this channel. */
  hal_can_mode_t modes;
  /** Slots addressable through hal_can_set_filter(). */
  uint8_t legacy_filters;
  /** Everything hal_can_get_caps() reports; modes is copied from above. */
  hal_can_caps_t public_caps;
} jh_can_caps_t;

/** @brief One received frame in the receive ring. */
typedef struct {
  hal_can_frame_t frame;
  hal_can_rx_info_t info;
} jh_can_rx_entry_t;

/** @brief One queued frame in the transmit ring. */
typedef struct {
  hal_can_frame_t frame;
  uint32_t tag;
} jh_can_tx_entry_t;

/** @brief Kinds of jh_can_event_t. */
enum { JH_CAN_EVENT_TX = 1u, JH_CAN_EVENT_STATE = 2u };

/** @brief One entry of the event ring. */
typedef struct {
  uint8_t kind;                      /**< JH_CAN_EVENT_*. */
  hal_can_tx_event_t tx;             /**< JH_CAN_EVENT_TX. */
  hal_can_state_t state;             /**< JH_CAN_EVENT_STATE. */
  hal_can_error_counters_t counters; /**< JH_CAN_EVENT_STATE. */
} jh_can_event_t;

/** @brief Traffic counters; each has one writer at a time and is read with
 *         jh_can_counter_read() from the other side. */
typedef struct {
  uint32_t rx_frames;
  uint32_t tx_frames;
  uint32_t rx_hw_lost;
  uint32_t rx_queue_overflow;
  uint32_t rx_dropped_fd_on_classic_read;
  uint32_t tx_failed;
  uint32_t event_overflow;
  uint32_t bus_off_count;
  uint32_t ram_access_failures;
} jh_can_counters_t;

/** @brief Count one; only the counter's single writer calls it. */
static inline void jh_can_count(uint32_t *counter) {
  HAL_ATOMIC_STORE(counter, HAL_ATOMIC_LOAD(counter, HAL_ATOMIC_RELAXED) + 1u,
                   HAL_ATOMIC_RELAXED);
}

/** @brief Read a counter another context writes. */
static inline uint32_t jh_can_counter_read(const uint32_t *counter) {
  return HAL_ATOMIC_LOAD(counter, HAL_ATOMIC_RELAXED);
}

/**
 * @brief Queues of one channel, owned by the facade.
 *
 * rx and events: producer is the provider (its interrupt), consumer the
 * facade. tx: producer is the facade, consumer the provider, which serializes
 * its own task-side and interrupt-side reads.
 */
typedef struct {
  jh_spsc_ring_t rx;     /**< jh_can_rx_entry_t */
  jh_spsc_ring_t events; /**< jh_can_event_t */
  jh_spsc_ring_t tx;     /**< jh_can_tx_entry_t */
  jh_can_counters_t counters;
  void (*volatile notify)(void *user); /**< Called by the producer side. */
  void *volatile notify_user;
} jh_can_queues_t;

/** @brief Queue an event, counting it when the ring is full. */
static inline void jh_can_queues_post(jh_can_queues_t *q,
                                      const jh_can_event_t *event) {
  if (!jh_spsc_ring_push(&q->events, event)) {
    jh_can_count(&q->counters.event_overflow);
  }
}

/** @brief Result status of a send outcome: HAL_OK for HAL_CAN_TX_DONE,
 *         HAL_EIO for HAL_CAN_TX_FAILED, HAL_EBUS for HAL_CAN_TX_BUS_OFF and
 *         HAL_ECANCELED for HAL_CAN_TX_STOPPED. */
static inline hal_status_t jh_can_tx_reason_status(uint8_t reason) {
  switch (reason) {
  case HAL_CAN_TX_DONE:
    return HAL_OK;
  case HAL_CAN_TX_BUS_OFF:
    return HAL_EBUS;
  case HAL_CAN_TX_STOPPED:
    return HAL_ECANCELED;
  default:
    return HAL_EIO;
  }
}

/** @brief Queue the outcome of one send; @p timestamp_us is when it went
 *         out, 0 for a frame that did not. */
static inline void jh_can_queues_post_tx(jh_can_queues_t *q, uint32_t tag,
                                         hal_status_t result, uint8_t reason,
                                         uint64_t timestamp_us) {
  jh_can_event_t event = {};
  event.kind = JH_CAN_EVENT_TX;
  event.tx.tag = tag;
  event.tx.result = result;
  event.tx.reason = reason;
  event.tx.timestamp_us = result == HAL_OK ? timestamp_us : 0u;
  if (result == HAL_OK) {
    jh_can_count(&q->counters.tx_frames);
  } else {
    jh_can_count(&q->counters.tx_failed);
  }
  jh_can_queues_post(q, &event);
}

/** @brief Tell the consumer that something was queued. */
static inline void jh_can_queues_notify(jh_can_queues_t *q) {
  void (*notify)(void *) = q->notify;
  if (notify != NULL) {
    notify(q->notify_user);
  }
}

/** @brief Operations of one CAN provider. */
typedef struct {
  /** Backend this provider serves. */
  hal_can_backend_t backend;
  /**
   * @brief Bring the controller up from @p cfg.
   * @param ctx Zeroed context storage of the provider's context type.
   * @param[out] caps Capabilities of the created channel.
   * @param[out] mode Mode the controller runs in after init.
   * @return HAL_OK, or an error after which deinit() is not called.
   */
  hal_status_t (*init)(void *ctx, const hal_can_config_t *cfg,
                       jh_can_caps_t *caps, hal_can_mode_t *mode);
  /** @brief Release the controller; ctx is not used afterwards. */
  void (*deinit)(void *ctx);
  /** @brief Enter @p mode and take part in bus traffic (start, set_mode). */
  hal_status_t (*apply_mode)(void *ctx, hal_can_mode_t mode);
  /** @brief Leave the bus without releasing the controller. */
  hal_status_t (*stop)(void *ctx);
  /** @brief Transmit a validated frame the channel's mode allows. */
  hal_status_t (*send_frame)(void *ctx, const hal_can_frame_t *frame);
  /** @brief Take the oldest received frame; HAL_EAGAIN when none waits. */
  hal_status_t (*receive_frame)(void *ctx, hal_can_frame_t *frame);
  /** @brief HAL_OK when at least one received frame waits, HAL_EAGAIN when
   *         none does, or the error of a controller that could not be read. */
  hal_status_t (*available)(void *ctx);
  /** @brief Program classic-API filter slot @p index (already validated). */
  hal_status_t (*set_filter)(void *ctx, uint8_t index,
                             const hal_can_filter_t *filter);
  /** @brief Controller state while started. */
  hal_status_t (*get_state)(void *ctx, hal_can_state_t *state);
  /** @brief Transmit and receive error counters. */
  hal_status_t (*get_error_counters)(void *ctx,
                                     hal_can_error_counters_t *counters);
  /**
   * @brief Optional backend-specific classic send; NULL builds a classic
   *        frame from the HAL_CAN_ID_*_FLAG bits and calls send_frame().
   *        @p data is NULL only with @p len 0 (the facade refuses the rest).
   */
  hal_status_t (*legacy_send)(void *ctx, uint32_t id, uint8_t len,
                              const uint8_t *data);
  /**
   * @brief Optional backend-specific classic receive; NULL reads frames and
   *        returns classic ones with the HAL_CAN_ID_*_FLAG bits set.
   */
  hal_status_t (*legacy_receive)(void *ctx, uint32_t *id, uint8_t *len,
                                 uint8_t *data);
  /**
   * @brief Optional hal_can_set_std_filters(); NULL programs slots 0 and 1
   *        as exact 11-bit matches through set_filter().
   */
  hal_status_t (*set_std_filters)(void *ctx, uint32_t id0, uint32_t id1);
  /**
   * @brief Optional: run from interrupts on @p queues from now on.
   * @return HAL_OK, or HAL_EUNSUPPORTED to keep synchronous operation.
   */
  hal_status_t (*attach)(void *ctx, jh_can_queues_t *queues);
  /** @brief With attach(): move frames from the transmit ring into the
   *         controller while it has room. */
  void (*kick_tx)(void *ctx);
  /** @brief Optional: state, error counters and last protocol errors; the
   *         facade adds the traffic counters. */
  hal_status_t (*get_status)(void *ctx, hal_can_status_t *status);
  /** @brief Optional: start the bus-off recovery in manual recovery mode. */
  hal_status_t (*recover)(void *ctx);
  /** @brief Optional: add a validated filter and return its index (at least
   *         HAL_CAN_FILTER_FIRST_ADDED). */
  hal_status_t (*add_filter)(void *ctx, const hal_can_filter_ex_t *filter,
                             uint8_t *index);
  /** @brief With add_filter: remove an added filter or a classic slot;
   *         HAL_ENOENT when @p index holds none. */
  hal_status_t (*remove_filter)(void *ctx, uint8_t index);
  /** @brief With add_filter: hal_can_set_unmatched_policy(). */
  hal_status_t (*set_unmatched_policy)(void *ctx, bool accept_std,
                                       bool accept_ext, bool accept_rtr);
} jh_can_provider_t;

/**
 * @brief Handle of one channel. Owned by the facade; the mock reads it to
 *        reach its controller model from the hal_mock_can_* helpers.
 */
struct hal_can_impl_s {
  const jh_can_provider_t *provider;
  void *ctx; /**< Provider context inside the facade pool. */
  hal_can_backend_t backend;
  bool in_use;
  bool started;
  hal_can_mode_t mode; /**< Mode last applied or stored while stopped. */
  jh_can_caps_t caps;
  hal_mutex_t mutex;
  jh_can_queues_t *queues; /**< Always set; rings may have no capacity. */
  bool attached;           /**< Provider runs from interrupts on queues. */
  uint32_t next_tag;
  hal_can_rx_cb_t rx_cb;
  hal_can_tx_cb_t tx_cb;
  hal_can_state_cb_t state_cb;
  void *cb_user;
  hal_can_state_t reported_state; /**< Last state given to state_cb when
                                       the provider is not attached. */
  uint32_t sync_tx_frames; /**< Classic sends, counted by the task side. */
  uint32_t sync_tx_failed;
};

#endif /* HAL_ENABLE_CAN */
