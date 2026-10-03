#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_CAN

#include "hal/system/hal_system.h"
#include "hal_can_mock_model.h"
#include "hal_mock.h"

#include <string.h>

/* The model behind every enabled backend: two rings standing in for the bus,
 * software acceptance filters applied on injection, and a settable controller
 * state. FD frames are accepted only when the emulated backend has FD on. */

static unsigned s_fail_creates;
static bool s_queued;

static jh_can_mock_ctx_t *model(void *ctx) {
  return static_cast<jh_can_mock_ctx_t *>(ctx);
}

static int ring_push(hal_can_frame_t *buf, int *tail, int *count,
                     const hal_can_frame_t *f) {
  const int cap = hal_get_config()->mock_can_buf_size;
  if (*count >= cap)
    return -1;
  buf[*tail] = *f;
  *tail = (*tail + 1) % cap;
  (*count)++;
  return 0;
}

static int ring_pop(hal_can_frame_t *buf, int *head, int *count,
                    hal_can_frame_t *out) {
  const int cap = hal_get_config()->mock_can_buf_size;
  if (*count <= 0)
    return -1;
  *out = buf[*head];
  *head = (*head + 1) % cap;
  (*count)--;
  return 0;
}

/* ── Filters: ordered positions like the FDCAN element lists ─────────── */

static jh_can_mock_element_t *filter_list(jh_can_mock_ctx_t *m, bool ext,
                                          uint32_t *size) {
  *size = ext ? JH_CAN_MOCK_EXT_FILTERS : JH_CAN_MOCK_STD_FILTERS;
  return ext ? m->ext_filters : m->std_filters;
}

static void reset_filters(jh_can_mock_ctx_t *m) {
  for (uint32_t i = 0; i < JH_CAN_MOCK_STD_FILTERS; i++) {
    m->std_filters[i].owner = JH_CAN_MOCK_FREE;
  }
  for (uint32_t i = 0; i < JH_CAN_MOCK_EXT_FILTERS; i++) {
    m->ext_filters[i].owner = JH_CAN_MOCK_FREE;
  }
  m->accept_std = true;
  m->accept_ext = true;
  m->accept_rtr = true;
  m->policy_set = false;
}

/* The element filter @p index owns, or NULL. */
static jh_can_mock_element_t *owned_element(jh_can_mock_ctx_t *m, uint8_t index,
                                            bool *ext) {
  for (uint32_t list = 0; list < 2u; list++) {
    uint32_t size = 0u;
    jh_can_mock_element_t *elems = filter_list(m, list != 0u, &size);
    for (uint32_t i = 0; i < size; i++) {
      if (elems[i].owner == index) {
        *ext = list != 0u;
        return &elems[i];
      }
    }
  }
  return NULL;
}

/* Remove the filter @p index owns; false when it owns none. */
static bool drop_filter(jh_can_mock_ctx_t *m, uint8_t index) {
  bool ext = false;
  jh_can_mock_element_t *elem = owned_element(m, index, &ext);
  if (elem == NULL) {
    return false;
  }
  elem->owner = JH_CAN_MOCK_FREE;
  return true;
}

/* Lowest free position of the filter's ID kind. */
static hal_status_t place_filter(jh_can_mock_ctx_t *m, uint8_t index,
                                 const hal_can_filter_ex_t *filter) {
  uint32_t size = 0u;
  jh_can_mock_element_t *elems =
      filter_list(m, (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u, &size);
  for (uint32_t i = 0; i < size; i++) {
    if (elems[i].owner == JH_CAN_MOCK_FREE) {
      elems[i].owner = index;
      elems[i].filter = *filter;
      return HAL_OK;
    }
  }
  return HAL_ENOMEM;
}

/* The first filter that matches decides; unmatched frames follow the
 * policy. @p index names the accepting filter or HAL_CAN_FILTER_NONE. */
static bool filters_accept(jh_can_mock_ctx_t *m, const hal_can_frame_t *frame,
                           uint8_t *index) {
  *index = HAL_CAN_FILTER_NONE;
  if ((frame->flags & HAL_CAN_FRAME_RTR) != 0u && !m->accept_rtr) {
    return false;
  }
  const bool ext = (frame->flags & HAL_CAN_FRAME_EXTENDED) != 0u;
  uint32_t size = 0u;
  const jh_can_mock_element_t *elems = filter_list(m, ext, &size);
  for (uint32_t i = 0; i < size; i++) {
    bool hit = false;
    if (elems[i].owner == JH_CAN_MOCK_FREE ||
        hal_can_frame_matches_filter_ex(frame, &elems[i].filter, &hit) !=
            HAL_OK ||
        !hit) {
      continue;
    }
    if (elems[i].filter.action == HAL_CAN_FILTER_REJECT) {
      return false;
    }
    *index = elems[i].owner;
    return true;
  }
  return ext ? m->accept_ext : m->accept_std;
}

/* Queued frames the controller drops: each ends with @p reason. */
static void cancel_queued(jh_can_mock_ctx_t *m, uint8_t reason) {
  jh_can_tx_entry_t entry;
  while (jh_spsc_ring_pop(&m->queues->tx, &entry)) {
    jh_can_queues_post_tx(m->queues, entry.tag, jh_can_tx_reason_status(reason),
                          reason, 0u);
  }
}

static void post_state(jh_can_mock_ctx_t *m) {
  jh_can_event_t event = {};
  event.kind = JH_CAN_EVENT_STATE;
  event.state = m->state;
  event.counters = m->counters;
  jh_can_queues_post(m->queues, &event);
}

static hal_status_t mock_init(void *ctx, const hal_can_config_t *cfg,
                              jh_can_caps_t *caps, hal_can_mode_t *mode) {
  if (s_fail_creates > 0u) {
    s_fail_creates--;
    return HAL_EIO;
  }
  bool one_shot = false;
  bool fd = false;
  switch (cfg->backend) {
  case HAL_CAN_BACKEND_MCP2515:
    one_shot = cfg->mcp2515.one_shot_tx;
    break;
  case HAL_CAN_BACKEND_MCP251XFD:
    one_shot = cfg->mcp251xfd.one_shot_tx;
    fd = cfg->mcp251xfd.enable_fd;
    caps->modes |= HAL_CAN_MODE_EXTERNAL_LOOPBACK;
    break;
  case HAL_CAN_BACKEND_STM32G474_FDCAN:
    one_shot = cfg->stm32g474_fdcan.one_shot_tx;
    fd = cfg->stm32g474_fdcan.enable_fd;
    caps->modes |= HAL_CAN_MODE_EXTERNAL_LOOPBACK;
    break;
  default:
    return HAL_EUNSUPPORTED;
  }
  model(ctx)->state = HAL_CAN_STATE_ERROR_ACTIVE;
  reset_filters(model(ctx));
  model(ctx)->queued = s_queued;
  if (s_queued) {
    caps->modes |= HAL_CAN_MODE_MANUAL_RECOVERY;
    caps->public_caps.features |= HAL_CAN_CAP_TX_EVENTS | HAL_CAN_CAP_RX_QUEUE |
                                  HAL_CAN_CAP_MANUAL_RECOVERY;
  }
  caps->modes |= HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_LISTEN_ONLY |
                 HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP |
                 (fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
  caps->legacy_filters = HAL_CAN_MAX_FILTERS;
  caps->public_caps.std_filters = JH_CAN_MOCK_STD_FILTERS;
  caps->public_caps.ext_filters = JH_CAN_MOCK_EXT_FILTERS;
  caps->public_caps.tx_slots = 1u;
  if (fd) {
    caps->public_caps.features |= HAL_CAN_CAP_FD;
  }
  caps->public_caps.max_nominal_bitrate_hz = 1000000u;
  caps->public_caps.max_data_bitrate_hz = fd ? 5000000u : 1000000u;
  *mode = (one_shot ? HAL_CAN_MODE_ONE_SHOT : HAL_CAN_MODE_NORMAL) |
          (fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
  model(ctx)->mode = *mode;
  return HAL_OK;
}

static void mock_deinit(void *ctx) { (void)ctx; }

/* Frames still waiting were queued for the old mode, so they end like on
 * FDCAN (an FD frame must not go out on a channel switched to classic). */
static hal_status_t mock_apply_mode(void *ctx, hal_can_mode_t mode) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (m->queues != NULL) {
    cancel_queued(m, HAL_CAN_TX_STOPPED);
  }
  m->mode = mode;
  if (m->state == HAL_CAN_STATE_STOPPED) {
    m->state = HAL_CAN_STATE_ERROR_ACTIVE;
  }
  return HAL_OK;
}

static hal_status_t mock_stop(void *ctx) {
  jh_can_mock_ctx_t *m = model(ctx);
  m->state = HAL_CAN_STATE_STOPPED;
  if (m->queues != NULL) {
    cancel_queued(m, HAL_CAN_TX_STOPPED);
  }
  return HAL_OK;
}

static hal_status_t mock_send_frame(void *ctx, const hal_can_frame_t *frame) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (m->state == HAL_CAN_STATE_BUS_OFF) {
    return HAL_EBUS;
  }
  return ring_push(m->tx, &m->tx_tail, &m->tx_count, frame) == 0 ? HAL_OK
                                                                 : HAL_EBUSY;
}

/* The classic send of the model keeps the identifier as passed. */
static hal_status_t mock_legacy_send(void *ctx, uint32_t id, uint8_t len,
                                     const uint8_t *data) {
  const uint8_t safe_len =
      len < HAL_CAN_MAX_DATA_LEN ? len : (uint8_t)HAL_CAN_MAX_DATA_LEN;
  hal_can_frame_t f = {};
  f.id = id;
  f.len = safe_len;
  f.dlc = safe_len;
  if (safe_len > 0) {
    memcpy(f.data, data, safe_len);
  }
  return mock_send_frame(ctx, &f);
}

static hal_status_t mock_receive_frame(void *ctx, hal_can_frame_t *frame) {
  jh_can_mock_ctx_t *m = model(ctx);
  return ring_pop(m->rx, &m->rx_head, &m->rx_count, frame) == 0 ? HAL_OK
                                                                : HAL_EAGAIN;
}

/* The classic receive of the model returns the identifier as injected. */
static hal_status_t mock_legacy_receive(void *ctx, uint32_t *id, uint8_t *len,
                                        uint8_t *data) {
  hal_can_frame_t f;
  const hal_status_t st = mock_receive_frame(ctx, &f);
  if (st != HAL_OK) {
    return st;
  }
  if ((f.flags & HAL_CAN_FRAME_FD) != 0u || f.len > HAL_CAN_MAX_DATA_LEN) {
    return HAL_EUNSUPPORTED;
  }
  *id = f.id;
  *len = f.len;
  memcpy(data, f.data, f.len);
  return HAL_OK;
}

static hal_status_t mock_available(void *ctx) {
  return model(ctx)->rx_count > 0 ? HAL_OK : HAL_EAGAIN;
}

/* A classic slot is an accepting mask filter; the first one rejects
 * unmatched frames unless a policy was chosen. */
static bool has_free_element(jh_can_mock_ctx_t *m, bool ext) {
  uint32_t size = 0u;
  const jh_can_mock_element_t *elems = filter_list(m, ext, &size);
  for (uint32_t i = 0; i < size; i++) {
    if (elems[i].owner == JH_CAN_MOCK_FREE) {
      return true;
    }
  }
  return false;
}

/* Like FDCAN: a slot keeps its element when the ID kind stays, and nothing
 * changes when the other list has no room. */
static hal_status_t mock_set_filter(void *ctx, uint8_t index,
                                    const hal_can_filter_t *filter) {
  jh_can_mock_ctx_t *m = model(ctx);
  const bool ext = (filter->flags & HAL_CAN_FILTER_EXTENDED) != 0u;
  bool old_ext = false;
  jh_can_mock_element_t *old = owned_element(m, index, &old_ext);
  const bool in_place = old != NULL && old_ext == ext;
  if (!in_place && !has_free_element(m, ext)) {
    return HAL_ENOMEM;
  }
  if (!m->policy_set) {
    m->accept_std = false;
    m->accept_ext = false;
    m->policy_set = true;
  }
  hal_can_filter_ex_t ex = {};
  ex.type = HAL_CAN_FILTER_MASK;
  ex.action = HAL_CAN_FILTER_ACCEPT;
  ex.flags = filter->flags;
  ex.id1 = filter->id;
  ex.id2 = filter->mask;
  if (in_place) {
    old->filter = ex;
    return HAL_OK;
  }
  (void)drop_filter(m, index);
  return place_filter(m, index, &ex);
}

static bool index_in_use(jh_can_mock_ctx_t *m, uint8_t index) {
  bool ext = false;
  return owned_element(m, index, &ext) != NULL;
}

/* Filters beyond the classic slots exist where the native FDCAN backend is
 * emulated (hal_mock_can_set_queued()); the SPI controllers have none. */
static hal_status_t
mock_add_filter(void *ctx, const hal_can_filter_ex_t *filter, uint8_t *index) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (!m->queued) {
    return HAL_EUNSUPPORTED;
  }
  uint8_t candidate = HAL_CAN_FILTER_FIRST_ADDED;
  while (index_in_use(m, candidate)) {
    candidate++;
  }
  const hal_status_t st = place_filter(m, candidate, filter);
  if (st == HAL_OK) {
    *index = candidate;
  }
  return st;
}

static hal_status_t mock_remove_filter(void *ctx, uint8_t index) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (!m->queued) {
    return HAL_EUNSUPPORTED;
  }
  return drop_filter(m, index) ? HAL_OK : HAL_ENOENT;
}

static hal_status_t mock_set_unmatched_policy(void *ctx, bool accept_std,
                                              bool accept_ext,
                                              bool accept_rtr) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (!m->queued) {
    return HAL_EUNSUPPORTED;
  }
  m->accept_std = accept_std;
  m->accept_ext = accept_ext;
  m->accept_rtr = accept_rtr;
  m->policy_set = true;
  return HAL_OK;
}

static hal_status_t mock_get_state(void *ctx, hal_can_state_t *state) {
  *state = model(ctx)->state;
  return HAL_OK;
}

static hal_status_t mock_get_error_counters(void *ctx,
                                            hal_can_error_counters_t *c) {
  *c = model(ctx)->counters;
  return HAL_OK;
}

static hal_status_t mock_attach(void *ctx, jh_can_queues_t *queues) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (!m->queued) {
    return HAL_EUNSUPPORTED; /* synchronous, like the SPI controllers */
  }
  m->queues = queues;
  return HAL_OK;
}

/* The controller takes queued frames while the model's bus has room. */
static void mock_kick_tx(void *ctx) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (m->state == HAL_CAN_STATE_BUS_OFF) {
    cancel_queued(m, HAL_CAN_TX_BUS_OFF);
    return;
  }
  const jh_can_tx_entry_t *entry;
  while ((entry = static_cast<const jh_can_tx_entry_t *>(
              jh_spsc_ring_peek(&m->queues->tx))) != NULL) {
    if (m->fail_sends > 0u) {
      m->fail_sends--;
      jh_can_queues_post_tx(m->queues, entry->tag,
                            jh_can_tx_reason_status(HAL_CAN_TX_FAILED),
                            HAL_CAN_TX_FAILED, 0u);
    } else if (ring_push(m->tx, &m->tx_tail, &m->tx_count, &entry->frame) ==
               0) {
      jh_can_queues_post_tx(m->queues, entry->tag, HAL_OK, HAL_CAN_TX_DONE,
                            hal_micros64());
    } else {
      break; /* bus busy: the frame stays queued */
    }
    jh_spsc_ring_drop(&m->queues->tx);
  }
  jh_can_queues_notify(m->queues);
}

static hal_status_t mock_get_status(void *ctx, hal_can_status_t *status) {
  const jh_can_mock_ctx_t *m = model(ctx);
  status->state = m->state;
  status->tec = m->counters.tx;
  status->rec = m->counters.rx;
  status->ram_access_failures = m->ram_access_failures;
  return HAL_OK;
}

static hal_status_t mock_recover(void *ctx) {
  jh_can_mock_ctx_t *m = model(ctx);
  if (m->state == HAL_CAN_STATE_BUS_OFF) {
    m->state = HAL_CAN_STATE_ERROR_ACTIVE;
    m->counters.tx = 0u;
    m->counters.rx = 0u;
    if (m->queues != NULL) {
      post_state(m);
    }
  }
  return HAL_OK;
}

const jh_can_provider_t jh_can_mock_provider = {HAL_CAN_BACKEND_MCP2515,
                                                mock_init,
                                                mock_deinit,
                                                mock_apply_mode,
                                                mock_stop,
                                                mock_send_frame,
                                                mock_receive_frame,
                                                mock_available,
                                                mock_set_filter,
                                                mock_get_state,
                                                mock_get_error_counters,
                                                mock_legacy_send,
                                                mock_legacy_receive,
                                                NULL,
                                                mock_attach,
                                                mock_kick_tx,
                                                mock_get_status,
                                                mock_recover,
                                                mock_add_filter,
                                                mock_remove_filter,
                                                mock_set_unmatched_policy};

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

/* The model of a live handle, or NULL for a handle the mock does not serve. */
static jh_can_mock_ctx_t *served(hal_can_t h) {
  if (h == NULL || !h->in_use || h->provider != &jh_can_mock_provider) {
    return NULL;
  }
  return model(h->ctx);
}

static void inject(hal_can_t h, const hal_can_frame_t *f, const char *what) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL) {
    return;
  }
  hal_mutex_lock(h->mutex);
  int ok = 0;
  uint8_t index = HAL_CAN_FILTER_NONE;
  if (filters_accept(m, f, &index)) {
    if (m->queues != NULL) {
      /* Interrupt-style reception: a full queue counts the frame lost. */
      jh_can_rx_entry_t entry = {};
      entry.frame = *f;
      entry.info.filter_index = index;
      entry.info.timestamp_us = hal_micros64();
      if (jh_spsc_ring_push(&m->queues->rx, &entry)) {
        jh_can_count(&m->queues->counters.rx_frames);
      } else {
        jh_can_count(&m->queues->counters.rx_queue_overflow);
      }
      jh_can_queues_notify(m->queues);
    } else {
      ok = ring_push(m->rx, &m->rx_tail, &m->rx_count, f);
    }
  }
  hal_mutex_unlock(h->mutex);
  (void)ok; // Checked by the assertion below.
  (void)what;
  HAL_ASSERT(ok == 0, what);
}

void hal_mock_can_inject(hal_can_t h, uint32_t id, uint8_t len,
                         const uint8_t *data) {
  if (len > 0 && data == NULL)
    return;
  const uint8_t safe_len =
      len < HAL_CAN_MAX_DATA_LEN ? len : (uint8_t)HAL_CAN_MAX_DATA_LEN;
  hal_can_frame_t f = {};
  f.id = id;
  f.len = safe_len;
  f.dlc = safe_len;
  if (safe_len > 0) {
    memcpy(f.data, data, safe_len);
  }
  inject(h, &f,
         "hal_mock_can_inject: RX ring full - increase MOCK_CAN_BUF_SIZE");
}

void hal_mock_can_inject_frame(hal_can_t h, const hal_can_frame_t *frame) {
  if (frame == NULL) {
    return;
  }
  inject(
      h, frame,
      "hal_mock_can_inject_frame: RX ring full - increase MOCK_CAN_BUF_SIZE");
}

hal_status_t hal_mock_can_get_sent_frame(hal_can_t h, hal_can_frame_t *frame) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL || frame == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_lock(h->mutex);
  const bool ok = ring_pop(m->tx, &m->tx_head, &m->tx_count, frame) == 0;
  hal_mutex_unlock(h->mutex);
  return ok ? HAL_OK : HAL_EAGAIN;
}

hal_status_t hal_mock_can_get_sent(hal_can_t h, uint32_t *id, uint8_t *len,
                                   uint8_t *data) {
  if (!id || !len || !data) {
    return HAL_EINVAL;
  }
  hal_can_frame_t f;
  const hal_status_t st = hal_mock_can_get_sent_frame(h, &f);
  if (st != HAL_OK) {
    return st;
  }
  *id = f.id;
  *len = f.len;
  memcpy(data, f.data,
         f.len < HAL_CAN_MAX_DATA_LEN ? f.len : HAL_CAN_MAX_DATA_LEN);
  return HAL_OK;
}

void hal_mock_can_reset(hal_can_t h) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL)
    return;
  hal_mutex_lock(h->mutex);
  m->rx_head = m->rx_tail = m->rx_count = 0;
  m->tx_head = m->tx_tail = m->tx_count = 0;
  if (m->queues != NULL) {
    jh_spsc_ring_clear(&m->queues->rx);
  }
  hal_mutex_unlock(h->mutex);
}

void hal_mock_can_set_state(hal_can_t h, hal_can_state_t state) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL) {
    return;
  }
  hal_mutex_lock(h->mutex);
  const bool changed = m->state != state;
  m->state = state;
  h->started = state != HAL_CAN_STATE_STOPPED;
  if (m->queues != NULL && changed) {
    if (state == HAL_CAN_STATE_BUS_OFF) {
      jh_can_count(&m->queues->counters.bus_off_count);
      cancel_queued(m, HAL_CAN_TX_BUS_OFF);
    }
    post_state(m);
    jh_can_queues_notify(m->queues);
  }
  hal_mutex_unlock(h->mutex);
}

void hal_mock_can_fail_sends(hal_can_t h, unsigned count) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL) {
    return;
  }
  hal_mutex_lock(h->mutex);
  m->fail_sends = count;
  hal_mutex_unlock(h->mutex);
}

/* Counted where FDCAN counts it: by the interrupt on a queued channel, by
 * the task otherwise. */
void hal_mock_can_report_ram_access_failure(hal_can_t h) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL) {
    return;
  }
  hal_mutex_lock(h->mutex);
  if (m->queues != NULL) {
    jh_can_count(&m->queues->counters.ram_access_failures);
  } else {
    m->ram_access_failures++;
  }
  hal_mutex_unlock(h->mutex);
}

void hal_mock_can_fail_creates(unsigned count) { s_fail_creates = count; }

void hal_mock_can_set_queued(bool queued) { s_queued = queued; }

void hal_mock_can_set_error_counters(hal_can_t h, uint8_t tx, uint8_t rx) {
  jh_can_mock_ctx_t *m = served(h);
  if (m == NULL) {
    return;
  }
  hal_mutex_lock(h->mutex);
  m->counters.tx = tx;
  m->counters.rx = rx;
  hal_mutex_unlock(h->mutex);
}

#endif // HAL_ENABLE_CAN
#endif // HAL_TARGET_IS_MOCK
