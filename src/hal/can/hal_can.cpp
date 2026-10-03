#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_CAN

#include "hal/can/hal_can.h"
#include "hal/can/jh_can_provider.h"
#include "hal/core/hal_target.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#if HAL_TARGET_IS_MOCK
#include "hal/impl/.mock/hal_can_mock_model.h"
#else
#ifdef HAL_ENABLE_MCP2515
#include "hal/can/mcp2515/hal_can_mcp2515.h"
#endif
#ifdef HAL_ENABLE_MCP251XFD
#include "hal/can/mcp251xfd/hal_can_mcp251xfd.h"
#endif
#ifdef HAL_ENABLE_STM32G474_FDCAN
#include "hal/impl/stm32g474/hal_can_stm32g474_fdcan.h"
#endif
#endif

#include <string.h>

/* Context storage for whichever provider a pool slot serves. */
union jh_can_ctx_storage_u {
  uint8_t none;
#if HAL_TARGET_IS_MOCK
  jh_can_mock_ctx_t mock;
#else
#ifdef HAL_ENABLE_MCP2515
  alignas(JHMCP2515) uint8_t mcp2515[sizeof(JHMCP2515)];
#endif
#ifdef HAL_ENABLE_MCP251XFD
  alignas(JHMCP251XFD) uint8_t mcp251xfd[sizeof(JHMCP251XFD)];
#endif
#ifdef HAL_ENABLE_STM32G474_FDCAN
  hal_can_stm32g474_fdcan_t stm32g474_fdcan;
#endif
#endif
};

#if HAL_TARGET_IS_MOCK
#define JH_CAN_POOL_SIZE MOCK_CAN_MAX_INST
#else
#define JH_CAN_POOL_SIZE HAL_CAN_MAX_INSTANCES
#endif

static hal_can_impl_t s_pool[JH_CAN_POOL_SIZE];
static jh_can_ctx_storage_u s_ctx[JH_CAN_POOL_SIZE];

/* Queue storage per pool slot; a ring of length 0 keeps one unused element so
 * the arrays stay valid C++. */
#define JH_CAN_STORE(len) ((len) > 0 ? (len) : 1)
static jh_can_queues_t s_queues[JH_CAN_POOL_SIZE];
static jh_can_rx_entry_t s_rx_store[JH_CAN_POOL_SIZE]
                                   [JH_CAN_STORE(HAL_CAN_RX_QUEUE_LEN)];
static jh_can_tx_entry_t s_tx_store[JH_CAN_POOL_SIZE]
                                   [JH_CAN_STORE(HAL_CAN_TX_QUEUE_LEN)];
static jh_can_event_t s_event_store[JH_CAN_POOL_SIZE]
                                   [JH_CAN_STORE(HAL_CAN_EVENT_QUEUE_LEN)];

/* On the host every enabled backend is served by the mock controller model. */
#if HAL_TARGET_IS_MOCK
#define JH_CAN_PROVIDER(real) (&jh_can_mock_provider)
#else
#define JH_CAN_PROVIDER(real) (&(real))
#endif

static const jh_can_provider_t *find_provider(hal_can_backend_t backend) {
  switch (backend) {
#ifdef HAL_ENABLE_MCP2515
  case HAL_CAN_BACKEND_MCP2515:
    return JH_CAN_PROVIDER(jh_can_mcp2515_provider);
#endif
#ifdef HAL_ENABLE_MCP251XFD
  case HAL_CAN_BACKEND_MCP251XFD:
    return JH_CAN_PROVIDER(jh_can_mcp251xfd_provider);
#endif
#ifdef HAL_ENABLE_STM32G474_FDCAN
  case HAL_CAN_BACKEND_STM32G474_FDCAN:
    return JH_CAN_PROVIDER(jh_can_stm32g474_fdcan_provider);
#endif
  default:
    return NULL;
  }
}

static int pool_limit(void) {
#if HAL_TARGET_IS_MOCK
  const int configured = hal_get_config()->mock_can_max_inst;
#else
  const int configured = hal_get_config()->can_max_instances;
#endif
  return configured < JH_CAN_POOL_SIZE ? configured : JH_CAN_POOL_SIZE;
}

static bool usable(hal_can_t h) { return h != NULL && h->in_use; }

static void init_queues(jh_can_queues_t *q, int slot) {
  memset(q, 0, sizeof(*q));
  jh_spsc_ring_init(&q->rx, s_rx_store[slot], sizeof(jh_can_rx_entry_t),
                    (uint16_t)HAL_CAN_RX_QUEUE_LEN);
  jh_spsc_ring_init(&q->tx, s_tx_store[slot], sizeof(jh_can_tx_entry_t),
                    (uint16_t)HAL_CAN_TX_QUEUE_LEN);
  jh_spsc_ring_init(&q->events, s_event_store[slot], sizeof(jh_can_event_t),
                    (uint16_t)HAL_CAN_EVENT_QUEUE_LEN);
}

/* Tags start at 1 and skip 0 when they wrap. */
static uint32_t take_tag(hal_can_impl_t *h) {
  const uint32_t tag = h->next_tag;
  h->next_tag = tag + 1u != 0u ? tag + 1u : 1u;
  return tag;
}

/* Waits for the ring-based calls: false once @p timeout_ms has passed. */
static bool keep_waiting(uint32_t started_ms, uint32_t timeout_ms) {
  if (timeout_ms == 0u) {
    return false;
  }
  if (timeout_ms != HAL_CAN_WAIT_FOREVER &&
      hal_elapsed_u32(hal_millis(), started_ms, timeout_ms)) {
    return false;
  }
  hal_delay_ms(1u);
  return true;
}

static void release_slot(hal_can_impl_t *h) {
  if (h->mutex) {
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
  }
  h->provider = NULL;
  h->in_use = false;
}

/* One operating mode at most; no flag outside the caps. */
static bool mode_allowed(const jh_can_caps_t *caps, hal_can_mode_t mode) {
  if ((mode & ~caps->modes) != 0u) {
    return false;
  }
  const hal_can_mode_t ops =
      mode & (HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_EXTERNAL_LOOPBACK |
              HAL_CAN_MODE_LISTEN_ONLY | HAL_CAN_MODE_SLEEP);
  return (ops & (ops - 1u)) == 0u;
}

/* Sending needs a started channel that is not asleep. */
static bool can_transmit(const hal_can_impl_t *h) {
  return h->started && (h->mode & HAL_CAN_MODE_SLEEP) == 0u;
}

/* Classic sends finish before returning; the task side counts them. */
static void count_sync_send(hal_can_impl_t *h, hal_status_t st) {
  if (st == HAL_OK) {
    h->sync_tx_frames++;
  } else if (st != HAL_EBUSY) {
    h->sync_tx_failed++;
  }
}

static hal_status_t legacy_send(hal_can_impl_t *h, uint32_t id, uint8_t len,
                                const uint8_t *data) {
  if (len > 0u && data == NULL) {
    hal_derr_limited("can", "send called with NULL data pointer and len=%u",
                     (unsigned)len);
    return HAL_EINVAL;
  }
  if (h->provider->legacy_send != NULL) {
    return h->provider->legacy_send(h->ctx, id, len, data);
  }
  hal_can_frame_t frame = {};
  const uint8_t safe_len =
      len < HAL_CAN_MAX_DATA_LEN ? len : (uint8_t)HAL_CAN_MAX_DATA_LEN;
  if ((id & HAL_CAN_ID_EXTENDED_FLAG) != 0u) {
    frame.flags |= HAL_CAN_FRAME_EXTENDED;
    frame.id = id & HAL_CAN_EXT_ID_MASK;
  } else {
    frame.id = id & HAL_CAN_STD_ID_MASK;
  }
  if ((id & HAL_CAN_ID_RTR_FLAG) != 0u) {
    frame.flags |= HAL_CAN_FRAME_RTR;
  } else if (safe_len > 0u) {
    memcpy(frame.data, data, safe_len);
  }
  frame.dlc = safe_len;
  frame.len = safe_len;
  return h->provider->send_frame(h->ctx, &frame);
}

/* The oldest received frame: from the ring of an attached provider, else
 * from the controller. */
static hal_status_t take_frame(hal_can_impl_t *h, hal_can_frame_t *frame,
                               hal_can_rx_info_t *info) {
  if (h->attached) {
    jh_can_rx_entry_t entry;
    if (!jh_spsc_ring_pop(&h->queues->rx, &entry)) {
      return HAL_EAGAIN;
    }
    *frame = entry.frame;
    if (info != NULL) {
      *info = entry.info;
    }
    return HAL_OK;
  }
  const hal_status_t st = h->provider->receive_frame(h->ctx, frame);
  if (st == HAL_OK) {
    jh_can_count(&h->queues->counters.rx_frames);
    if (info != NULL) {
      info->timestamp_us = 0u;
      info->filter_index = HAL_CAN_FILTER_NONE;
    }
  }
  return st;
}

static hal_status_t frames_waiting(hal_can_impl_t *h) {
  if (h->attached) {
    return jh_spsc_ring_count(&h->queues->rx) > 0u ? HAL_OK : HAL_EAGAIN;
  }
  return h->provider->available(h->ctx);
}

static hal_status_t legacy_receive(hal_can_impl_t *h, uint32_t *id,
                                   uint8_t *len, uint8_t *data) {
  if (!h->attached && h->provider->legacy_receive != NULL) {
    const hal_status_t st = h->provider->legacy_receive(h->ctx, id, len, data);
    if (st == HAL_OK) {
      jh_can_count(&h->queues->counters.rx_frames);
    }
    return st;
  }
  hal_can_frame_t frame = {};
  const hal_status_t st = take_frame(h, &frame, NULL);
  if (st != HAL_OK) {
    return st;
  }
  /* The classic API has no room for CAN FD frames; they are consumed. */
  if ((frame.flags & HAL_CAN_FRAME_FD) != 0u ||
      frame.len > HAL_CAN_MAX_DATA_LEN) {
    jh_can_count(&h->queues->counters.rx_dropped_fd_on_classic_read);
    return HAL_EUNSUPPORTED;
  }
  uint32_t out_id = frame.id;
  if ((frame.flags & HAL_CAN_FRAME_EXTENDED) != 0u) {
    out_id |= HAL_CAN_ID_EXTENDED_FLAG;
  }
  if ((frame.flags & HAL_CAN_FRAME_RTR) != 0u) {
    out_id |= HAL_CAN_ID_RTR_FLAG;
  }
  *id = out_id;
  *len = frame.len;
  if (frame.len > 0u) {
    memcpy(data, frame.data, frame.len);
  }
  return HAL_OK;
}

hal_status_t hal_can_create(const hal_can_config_t *cfg, hal_can_t *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  *out = NULL;
  const hal_can_config_t effective = cfg ? *cfg : hal_can_default_config();
  const jh_can_provider_t *provider = find_provider(effective.backend);
  if (provider == NULL) {
    hal_derr_limited("can", "unsupported CAN backend %d",
                     (int)effective.backend);
    return HAL_EUNSUPPORTED;
  }

  const int limit = pool_limit();
  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < limit; i++) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();
  if (slot < 0) {
    hal_derr_limited("can", "pool exhausted - increase HAL_CAN_MAX_INSTANCES");
    return HAL_ENOMEM;
  }

  hal_can_impl_t *h = &s_pool[slot];
  memset(&s_ctx[slot], 0, sizeof(s_ctx[slot]));
  init_queues(&s_queues[slot], slot);
  h->provider = provider;
  h->ctx = &s_ctx[slot];
  h->backend = effective.backend;
  h->started = true;
  h->mode = HAL_CAN_MODE_NORMAL;
  memset(&h->caps, 0, sizeof(h->caps));
  h->queues = &s_queues[slot];
  h->attached = false;
  h->next_tag = 1u;
  h->rx_cb = NULL;
  h->tx_cb = NULL;
  h->state_cb = NULL;
  h->cb_user = NULL;
  h->reported_state = HAL_CAN_STATE_ERROR_ACTIVE;
  h->sync_tx_frames = 0u;
  h->sync_tx_failed = 0u;
  h->mutex = hal_mutex_create();
  hal_status_t st = provider->init(h->ctx, &effective, &h->caps, &h->mode);
  if (st != HAL_OK) {
    release_slot(h);
    return st;
  }
  /* A queued channel needs all three queues; a zero length keeps it
   * synchronous. */
  if (provider->attach != NULL && HAL_CAN_RX_QUEUE_LEN > 0 &&
      HAL_CAN_TX_QUEUE_LEN > 0 && HAL_CAN_EVENT_QUEUE_LEN > 0) {
    st = provider->attach(h->ctx, h->queues);
    if (st == HAL_OK) {
      h->attached = true;
    } else if (st != HAL_EUNSUPPORTED) {
      provider->deinit(h->ctx);
      release_slot(h);
      return st;
    }
  }
  h->caps.public_caps.modes = h->caps.modes;
  if (!h->attached) {
    h->caps.public_caps.features &=
        (uint8_t) ~(HAL_CAN_CAP_TX_EVENTS | HAL_CAN_CAP_RX_QUEUE);
  }
  *out = h;
  return HAL_OK;
}

void hal_can_destroy(hal_can_t h) {
  if (!usable(h)) {
    return;
  }
  hal_mutex_t m = h->mutex;
  hal_mutex_lock(m);
  h->provider->deinit(h->ctx);
  h->provider = NULL;
  h->in_use = false;
  h->mutex = NULL;
  hal_mutex_unlock(m);
  hal_mutex_destroy(m);
}

hal_status_t hal_can_send(hal_can_t h, uint32_t id, uint8_t len,
                          const uint8_t *data) {
  if (!usable(h)) {
    hal_derr_limited("can", "send called with NULL handle");
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st =
      can_transmit(h) ? legacy_send(h, id, len, data) : HAL_EBUSY;
  count_sync_send(h, st);
  hal_mutex_unlock(h->mutex);
  return st;
}

/* An FD frame needs FD in the channel's current mode, not only in its
 * capabilities: a channel switched to classic CAN refuses it. */
static bool frame_fits_mode(const hal_can_impl_t *h,
                            const hal_can_frame_t *frame) {
  return (frame->flags & HAL_CAN_FRAME_FD) == 0u ||
         (h->mode & HAL_CAN_MODE_FD) != 0u;
}

/* Waits for the frame, also on a queued channel. */
hal_status_t hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame) {
  if (!usable(h)) {
    hal_derr_limited("can", "send_frame called with NULL handle");
    return HAL_EINVAL;
  }
  if (hal_can_validate_frame(frame) != HAL_OK) {
    hal_derr_limited("can", "send_frame called with an invalid frame");
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_EBUSY;
  if (!frame_fits_mode(h, frame)) {
    st = HAL_EUNSUPPORTED;
  } else if (can_transmit(h)) {
    st = h->provider->send_frame(h->ctx, frame);
    count_sync_send(h, st);
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len,
                             uint8_t *data) {
  if (!usable(h)) {
    hal_derr_limited("can", "receive called with NULL handle");
    return HAL_EINVAL;
  }
  if (!id || !len || !data) {
    hal_derr_limited("can", "receive called with NULL output pointer(s)");
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = legacy_receive(h, id, len, data);
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame) {
  return hal_can_receive_frame_ex(h, frame, NULL, 0u);
}

hal_status_t hal_can_start(hal_can_t h) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = h->provider->apply_mode(h->ctx, h->mode);
  if (st == HAL_OK) {
    h->started = true;
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_stop(hal_can_t h) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = h->provider->stop(h->ctx);
  if (st == HAL_OK) {
    h->started = false;
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_set_mode(hal_can_t h, hal_can_mode_t mode) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_EUNSUPPORTED;
  if (mode_allowed(&h->caps, mode)) {
    st = h->started ? h->provider->apply_mode(h->ctx, mode) : HAL_OK;
    if (st == HAL_OK) {
      h->mode = mode;
    }
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode) {
  if (!usable(h) || !mode) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  *mode = h->mode;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

hal_status_t hal_can_get_state(hal_can_t h, hal_can_state_t *state) {
  if (!usable(h) || !state) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_OK;
  if (h->started) {
    st = h->provider->get_state(h->ctx, state);
  } else {
    *state = HAL_CAN_STATE_STOPPED;
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_get_error_counters(hal_can_t h,
                                        hal_can_error_counters_t *counters) {
  if (!usable(h) || !counters) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = h->provider->get_error_counters(h->ctx, counters);
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_available(hal_can_t h) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = frames_waiting(h);
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_EUNSUPPORTED;
  if (h->provider->set_std_filters != NULL) {
    st = h->provider->set_std_filters(h->ctx, id0, id1);
  } else if (h->caps.legacy_filters >= 2u) {
    const hal_can_filter_t f0 = {id0 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK,
                                 0u};
    const hal_can_filter_t f1 = {id1 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK,
                                 0u};
    st = h->provider->set_filter(h->ctx, 0u, &f0);
    if (st == HAL_OK) {
      st = h->provider->set_filter(h->ctx, 1u, &f1);
    }
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_set_filter(hal_can_t h, uint8_t index,
                                const hal_can_filter_t *filter) {
  if (!usable(h) || hal_can_validate_filter(filter) != HAL_OK) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_EINVAL;
  if (index < h->caps.legacy_filters) {
    st = h->provider->set_filter(h->ctx, index, filter);
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_add_filter(hal_can_t h, const hal_can_filter_ex_t *filter,
                                uint8_t *index) {
  if (!usable(h) || hal_can_validate_filter_ex(filter) != HAL_OK ||
      index == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = h->provider->add_filter != NULL
                              ? h->provider->add_filter(h->ctx, filter, index)
                              : HAL_EUNSUPPORTED;
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_remove_filter(hal_can_t h, uint8_t index) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = h->provider->remove_filter != NULL
                              ? h->provider->remove_filter(h->ctx, index)
                              : HAL_EUNSUPPORTED;
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_set_unmatched_policy(hal_can_t h, bool accept_std,
                                          bool accept_ext, bool accept_rtr) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const hal_status_t st = h->provider->set_unmatched_policy != NULL
                              ? h->provider->set_unmatched_policy(
                                    h->ctx, accept_std, accept_ext, accept_rtr)
                              : HAL_EUNSUPPORTED;
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_get_caps(hal_can_t h, hal_can_caps_t *out) {
  if (!usable(h) || out == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  *out = h->caps.public_caps;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

hal_status_t hal_can_send_frame_ex(hal_can_t h, const hal_can_frame_t *frame,
                                   uint32_t timeout_ms, uint32_t *tag) {
  if (!usable(h) || hal_can_validate_frame(frame) != HAL_OK) {
    return HAL_EINVAL;
  }
  const uint32_t started_ms = hal_millis();
  for (;;) {
    hal_mutex_lock(h->mutex);
    hal_status_t st = HAL_EAGAIN;
    if (!frame_fits_mode(h, frame)) {
      st = HAL_EUNSUPPORTED;
    } else if (!can_transmit(h)) {
      st = HAL_EBUSY;
    } else if (h->attached) {
      jh_can_tx_entry_t entry;
      entry.frame = *frame;
      entry.tag = h->next_tag;
      if (jh_spsc_ring_push(&h->queues->tx, &entry)) {
        (void)take_tag(h);
        h->provider->kick_tx(h->ctx);
        if (tag != NULL) {
          *tag = entry.tag;
        }
        st = HAL_OK;
      }
    } else {
      /* No transmit queue: send now and still report the outcome. */
      const uint32_t sent_tag = take_tag(h);
      st = h->provider->send_frame(h->ctx, frame);
      jh_can_queues_post_tx(h->queues, sent_tag, st,
                            st == HAL_OK ? HAL_CAN_TX_DONE : HAL_CAN_TX_FAILED,
                            hal_micros64());
      if (tag != NULL) {
        *tag = sent_tag;
      }
    }
    hal_mutex_unlock(h->mutex);
    if (st != HAL_EAGAIN) {
      return st;
    }
    if (!keep_waiting(started_ms, timeout_ms)) {
      return timeout_ms == 0u ? HAL_EBUSY : HAL_ETIMEOUT;
    }
  }
}

hal_status_t hal_can_receive_frame_ex(hal_can_t h, hal_can_frame_t *frame,
                                      hal_can_rx_info_t *info,
                                      uint32_t timeout_ms) {
  if (!usable(h)) {
    hal_derr_limited("can", "receive_frame called with NULL handle");
    return HAL_EINVAL;
  }
  if (frame == NULL) {
    hal_derr_limited("can", "receive_frame called with NULL frame");
    return HAL_EINVAL;
  }
  const uint32_t started_ms = hal_millis();
  for (;;) {
    hal_mutex_lock(h->mutex);
    const hal_status_t st = take_frame(h, frame, info);
    hal_mutex_unlock(h->mutex);
    if (st != HAL_EAGAIN) {
      return st;
    }
    if (!keep_waiting(started_ms, timeout_ms)) {
      return timeout_ms == 0u ? HAL_EAGAIN : HAL_ETIMEOUT;
    }
  }
}

hal_status_t hal_can_set_callbacks(hal_can_t h, hal_can_rx_cb_t rx,
                                   hal_can_tx_cb_t tx, hal_can_state_cb_t state,
                                   void *user) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  h->rx_cb = rx;
  h->tx_cb = tx;
  h->state_cb = state;
  h->cb_user = user;
  hal_mutex_unlock(h->mutex);
  return HAL_OK;
}

/* Frames one hal_can_service(h, 0) reads from a controller without a
 * receive ring: more than its hardware buffers hold. */
#define JH_CAN_SERVICE_POLLED_FRAMES 32u

/* A provider without its own events: report a state the polling sees. */
static bool poll_state_change(hal_can_impl_t *h, jh_can_event_t *event) {
  hal_can_state_t state = HAL_CAN_STATE_STOPPED;
  if (h->started && h->provider->get_state(h->ctx, &state) != HAL_OK) {
    return false;
  }
  if (state == h->reported_state) {
    return false;
  }
  memset(event, 0, sizeof(*event));
  event->kind = JH_CAN_EVENT_STATE;
  event->state = state;
  (void)h->provider->get_error_counters(h->ctx, &event->counters);
  h->reported_state = state;
  return true;
}

int hal_can_service(hal_can_t h, int max_events) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  /* 0 or less: what waits now, not what keeps arriving meanwhile. */
  hal_mutex_lock(h->mutex);
  int budget = max_events;
  if (budget <= 0) {
    const uint32_t frames = h->attached ? jh_spsc_ring_count(&h->queues->rx)
                                        : JH_CAN_SERVICE_POLLED_FRAMES;
    budget = (int)(jh_spsc_ring_count(&h->queues->events) + frames + 1u);
  }
  hal_mutex_unlock(h->mutex);

  int delivered = 0;
  while (delivered < budget) {
    enum { NOTHING, EVENT, FRAME } what = NOTHING;
    jh_can_event_t event;
    hal_can_frame_t frame;
    hal_can_rx_info_t info;
    if (!usable(h)) {
      break; /* destroyed by a callback */
    }
    hal_mutex_lock(h->mutex);
    if (jh_spsc_ring_pop(&h->queues->events, &event)) {
      what = EVENT;
    } else if (!h->attached && h->state_cb != NULL &&
               poll_state_change(h, &event)) {
      what = EVENT;
    } else if (h->rx_cb != NULL && take_frame(h, &frame, &info) == HAL_OK) {
      what = FRAME;
    }
    const hal_can_rx_cb_t rx_cb = h->rx_cb;
    const hal_can_tx_cb_t tx_cb = h->tx_cb;
    const hal_can_state_cb_t state_cb = h->state_cb;
    void *const user = h->cb_user;
    hal_mutex_unlock(h->mutex);
    if (what == NOTHING) {
      break;
    }
    if (what == FRAME) {
      rx_cb(h, &frame, &info, user);
    } else if (event.kind == JH_CAN_EVENT_TX && tx_cb != NULL) {
      tx_cb(h, &event.tx, user);
    } else if (event.kind == JH_CAN_EVENT_STATE && state_cb != NULL) {
      state_cb(h, event.state, &event.counters, user);
    }
    delivered++;
  }
  return delivered;
}

hal_status_t hal_can_set_isr_notify(hal_can_t h, void (*notify)(void *),
                                    void *user) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_EUNSUPPORTED;
  if (h->attached) {
    h->queues->notify = NULL;
    h->queues->notify_user = user;
    h->queues->notify = notify;
    st = HAL_OK;
  }
  hal_mutex_unlock(h->mutex);
  return st;
}

hal_status_t hal_can_get_status(hal_can_t h, hal_can_status_t *out) {
  if (!usable(h) || out == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_can_status_t status = {};
  hal_status_t st = HAL_OK;
  if (h->provider->get_status != NULL) {
    st = h->provider->get_status(h->ctx, &status);
  } else {
    hal_can_error_counters_t counters = {};
    st = h->provider->get_state(h->ctx, &status.state);
    if (st == HAL_OK) {
      st = h->provider->get_error_counters(h->ctx, &counters);
    }
    status.tec = counters.tx;
    status.rec = counters.rx;
  }
  if (!h->started) {
    status.state = HAL_CAN_STATE_STOPPED;
  }
  const jh_can_counters_t *c = &h->queues->counters;
  status.rx_frames = jh_can_counter_read(&c->rx_frames);
  status.tx_frames = jh_can_counter_read(&c->tx_frames) + h->sync_tx_frames;
  /* plus what the provider counted before attach */
  status.rx_hw_lost += jh_can_counter_read(&c->rx_hw_lost);
  status.rx_queue_overflow = jh_can_counter_read(&c->rx_queue_overflow);
  status.rx_dropped_fd_on_classic_read =
      jh_can_counter_read(&c->rx_dropped_fd_on_classic_read);
  status.tx_failed = jh_can_counter_read(&c->tx_failed) + h->sync_tx_failed;
  status.event_overflow = jh_can_counter_read(&c->event_overflow);
  status.bus_off_count = jh_can_counter_read(&c->bus_off_count);
  /* plus what the provider counted without interrupts */
  status.ram_access_failures += jh_can_counter_read(&c->ram_access_failures);
  hal_mutex_unlock(h->mutex);
  if (st == HAL_OK) {
    *out = status;
  }
  return st;
}

hal_status_t hal_can_recover(hal_can_t h, uint32_t timeout_ms) {
  if (!usable(h)) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  hal_status_t st = HAL_EUNSUPPORTED;
  if ((h->mode & HAL_CAN_MODE_MANUAL_RECOVERY) != 0u &&
      h->provider->recover != NULL) {
    st = h->provider->recover(h->ctx);
  }
  hal_mutex_unlock(h->mutex);
  if (st != HAL_OK) {
    return st;
  }
  const uint32_t started_ms = hal_millis();
  for (;;) {
    hal_can_state_t state = HAL_CAN_STATE_BUS_OFF;
    hal_mutex_lock(h->mutex);
    st = h->provider->get_state(h->ctx, &state);
    hal_mutex_unlock(h->mutex);
    if (st != HAL_OK || state != HAL_CAN_STATE_BUS_OFF) {
      return st;
    }
    if (!keep_waiting(started_ms, timeout_ms)) {
      return HAL_ETIMEOUT;
    }
  }
}

#endif /* HAL_ENABLE_CAN */
