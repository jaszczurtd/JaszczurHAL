#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "hal/can/hal_can.h"
#include "hal/core/hal_config.h"
#include "hal/system/hal_sync.h"
#include "hal_mock.h"
#include <string.h>

struct hal_can_impl_s {
  hal_can_frame_t rx[MOCK_CAN_BUF_SIZE];
  int rx_head, rx_tail, rx_count;
  hal_can_frame_t tx[MOCK_CAN_BUF_SIZE];
  int tx_head, tx_tail, tx_count;
  int in_use;
  bool started;
  hal_can_backend_t backend;
  bool fd_capable;
  hal_can_mode_t mode;
  hal_can_state_t state;
  hal_can_error_counters_t counters;
  bool filter_enabled[HAL_CAN_MAX_FILTERS];
  hal_can_filter_t filters[HAL_CAN_MAX_FILTERS];
  hal_mutex_t mutex;
};

static hal_can_impl_t s_pool[MOCK_CAN_MAX_INST];

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

static bool mode_valid(const hal_can_impl_t *h, hal_can_mode_t mode) {
  hal_can_mode_t supported = HAL_CAN_MODE_LOOPBACK | HAL_CAN_MODE_LISTEN_ONLY |
                             HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP;
  if (h && h->backend == HAL_CAN_BACKEND_MCP251XFD && h->fd_capable) {
    supported |= HAL_CAN_MODE_FD;
  }
  if ((mode & ~supported) != 0u) {
    return false;
  }
  uint8_t ops = 0u;
  ops += (mode & HAL_CAN_MODE_LOOPBACK) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_SLEEP) != 0u ? 1u : 0u;
  return ops <= 1u;
}

static bool filters_accept(const hal_can_impl_t *h,
                           const hal_can_frame_t *frame) {
  bool any = false;
  for (uint8_t i = 0; i < HAL_CAN_MAX_FILTERS; i++) {
    if (!h->filter_enabled[i]) {
      continue;
    }
    any = true;
    if (hal_can_frame_matches_filter(frame, &h->filters[i])) {
      return true;
    }
  }
  return !any;
}

hal_can_t hal_can_create(const hal_can_config_t *cfg) {
  hal_can_config_t effective = cfg ? *cfg : hal_can_default_config();
  if (effective.backend != HAL_CAN_BACKEND_MCP2515 &&
      effective.backend != HAL_CAN_BACKEND_MCP251XFD) {
    return NULL;
  }

  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < hal_get_config()->mock_can_max_inst; i++) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[slot].in_use = 1;
      break;
    }
  }
  hal_critical_section_exit();

  if (slot < 0) {
    HAL_ASSERT(0, "hal_can: pool exhausted - increase MOCK_CAN_MAX_INST");
    return NULL;
  }
  hal_can_impl_t *h = &s_pool[slot];
  memset(h, 0, sizeof(*h));
  h->in_use = 1;
  h->started = true;
  h->backend = effective.backend;
  if (effective.backend == HAL_CAN_BACKEND_MCP251XFD) {
    h->fd_capable = effective.mcp251xfd.enable_fd;
    h->mode =
        (effective.mcp251xfd.one_shot_tx ? HAL_CAN_MODE_ONE_SHOT
                                         : HAL_CAN_MODE_NORMAL) |
        (effective.mcp251xfd.enable_fd ? HAL_CAN_MODE_FD : HAL_CAN_MODE_NORMAL);
  } else {
    h->fd_capable = false;
    h->mode = effective.mcp2515.one_shot_tx ? HAL_CAN_MODE_ONE_SHOT
                                            : HAL_CAN_MODE_NORMAL;
  }
  h->state = HAL_CAN_STATE_ERROR_ACTIVE;
  h->mutex = hal_mutex_create();
  return h;
}

void hal_can_destroy(hal_can_t h) {
  if (!h)
    return;
  hal_mutex_t m = h->mutex;
  if (m) {
    hal_mutex_lock(m);
  }
  h->in_use = 0;
  if (m) {
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
  }
  h->mutex = NULL;
}

bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data) {
  if (!h)
    return false;
  if (!h->started || (h->mode & HAL_CAN_MODE_SLEEP) != 0u ||
      h->state == HAL_CAN_STATE_BUS_OFF)
    return false;
  if (len > 0 && data == NULL)
    return false;
  const uint8_t safe_len =
      len < HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
  hal_can_frame_t f = {};
  f.id = id;
  f.len = safe_len;
  f.dlc = safe_len;
  if (safe_len > 0) {
    memcpy(f.data, data, safe_len);
  }
  hal_mutex_lock(h->mutex);
  bool ok = ring_push(h->tx, &h->tx_tail, &h->tx_count, &f) == 0;
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame) {
  if (!h || !frame) {
    return false;
  }
  if (!h->started || (h->mode & HAL_CAN_MODE_SLEEP) != 0u ||
      h->state == HAL_CAN_STATE_BUS_OFF || !hal_can_validate_frame(frame)) {
    return false;
  }
  if ((frame->flags & HAL_CAN_FRAME_FD) != 0u &&
      (h->backend != HAL_CAN_BACKEND_MCP251XFD || !h->fd_capable)) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  bool ok = ring_push(h->tx, &h->tx_tail, &h->tx_count, frame) == 0;
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data) {
  if (!h || !id || !len || !data)
    return false;
  hal_mutex_lock(h->mutex);
  hal_can_frame_t f;
  bool ok = ring_pop(h->rx, &h->rx_head, &h->rx_count, &f) == 0;
  hal_mutex_unlock(h->mutex);
  if (!ok)
    return false;
  if ((f.flags & HAL_CAN_FRAME_FD) != 0u || f.len > HAL_CAN_MAX_DATA_LEN) {
    return false;
  }
  *id = f.id;
  *len = f.len;
  memcpy(data, f.data,
         f.len < HAL_CAN_MAX_DATA_LEN ? f.len : HAL_CAN_MAX_DATA_LEN);
  return true;
}

bool hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame) {
  if (!h || !frame) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = ring_pop(h->rx, &h->rx_head, &h->rx_count, frame) == 0;
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_available(hal_can_t h) {
  if (!h)
    return false;
  hal_mutex_lock(h->mutex);
  bool v = h->rx_count > 0;
  hal_mutex_unlock(h->mutex);
  return v;
}

bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1) {
  if (!h) {
    return false;
  }
  hal_can_filter_t f0 = {id0 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK, 0u};
  hal_can_filter_t f1 = {id1 & HAL_CAN_STD_ID_MASK, HAL_CAN_STD_ID_MASK, 0u};
  return hal_can_set_filter(h, 0u, &f0) && hal_can_set_filter(h, 1u, &f1);
}

bool hal_can_set_filter(hal_can_t h, uint8_t index,
                        const hal_can_filter_t *filter) {
  if (!h || index >= HAL_CAN_MAX_FILTERS || !hal_can_validate_filter(filter)) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  h->filters[index] = *filter;
  h->filter_enabled[index] = true;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_start(hal_can_t h) {
  if (!h) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  h->started = true;
  if (h->state == HAL_CAN_STATE_STOPPED) {
    h->state = HAL_CAN_STATE_ERROR_ACTIVE;
  }
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_stop(hal_can_t h) {
  if (!h) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  h->started = false;
  h->state = HAL_CAN_STATE_STOPPED;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_set_mode(hal_can_t h, hal_can_mode_t mode) {
  if (!h || !mode_valid(h, mode)) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  h->mode = mode;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode) {
  if (!h || !mode) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  *mode = h->mode;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_get_state(hal_can_t h, hal_can_state_t *state) {
  if (!h || !state) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  *state = h->started ? h->state : HAL_CAN_STATE_STOPPED;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_get_error_counters(hal_can_t h,
                                hal_can_error_counters_t *counters) {
  if (!h || !counters) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  *counters = h->counters;
  hal_mutex_unlock(h->mutex);
  return true;
}

// ── Mock helpers
// ──────────────────────────────────────────────────────────────

void hal_mock_can_inject(hal_can_t h, uint32_t id, uint8_t len,
                         const uint8_t *data) {
  if (!h || (len > 0 && data == NULL))
    return;
  const uint8_t safe_len =
      len < HAL_CAN_MAX_DATA_LEN ? len : HAL_CAN_MAX_DATA_LEN;
  hal_can_frame_t f = {};
  f.id = id;
  f.len = safe_len;
  f.dlc = safe_len;
  if (safe_len > 0) {
    memcpy(f.data, data, safe_len);
  }
  if (!filters_accept(h, &f)) {
    return;
  }
  hal_mutex_lock(h->mutex);
  int ok = ring_push(h->rx, &h->rx_tail, &h->rx_count, &f);
  hal_mutex_unlock(h->mutex);
  (void)ok; // Assertion above checks the value; output not used otherwise
  HAL_ASSERT(ok == 0,
             "hal_mock_can_inject: RX ring full - increase MOCK_CAN_BUF_SIZE");
}

bool hal_mock_can_get_sent(hal_can_t h, uint32_t *id, uint8_t *len,
                           uint8_t *data) {
  if (!h || !id || !len || !data)
    return false;
  hal_mutex_lock(h->mutex);
  hal_can_frame_t f;
  bool ok = ring_pop(h->tx, &h->tx_head, &h->tx_count, &f) == 0;
  hal_mutex_unlock(h->mutex);
  if (!ok)
    return false;
  *id = f.id;
  *len = f.len;
  memcpy(data, f.data,
         f.len < HAL_CAN_MAX_DATA_LEN ? f.len : HAL_CAN_MAX_DATA_LEN);
  return true;
}

void hal_mock_can_inject_frame(hal_can_t h, const hal_can_frame_t *frame) {
  if (!h || !frame) {
    return;
  }
  if (!filters_accept(h, frame)) {
    return;
  }
  hal_mutex_lock(h->mutex);
  int ok = ring_push(h->rx, &h->rx_tail, &h->rx_count, frame);
  hal_mutex_unlock(h->mutex);
  HAL_ASSERT(
      ok == 0,
      "hal_mock_can_inject_frame: RX ring full - increase MOCK_CAN_BUF_SIZE");
}

bool hal_mock_can_get_sent_frame(hal_can_t h, hal_can_frame_t *frame) {
  if (!h || !frame) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = ring_pop(h->tx, &h->tx_head, &h->tx_count, frame) == 0;
  hal_mutex_unlock(h->mutex);
  return ok;
}

void hal_mock_can_reset(hal_can_t h) {
  if (!h)
    return;
  hal_mutex_lock(h->mutex);
  h->rx_head = h->rx_tail = h->rx_count = 0;
  h->tx_head = h->tx_tail = h->tx_count = 0;
  hal_mutex_unlock(h->mutex);
}

void hal_mock_can_set_state(hal_can_t h, hal_can_state_t state) {
  if (!h) {
    return;
  }
  hal_mutex_lock(h->mutex);
  h->state = state;
  h->started = state != HAL_CAN_STATE_STOPPED;
  hal_mutex_unlock(h->mutex);
}

void hal_mock_can_set_error_counters(hal_can_t h, uint8_t tx, uint8_t rx) {
  if (!h) {
    return;
  }
  hal_mutex_lock(h->mutex);
  h->counters.tx = tx;
  h->counters.rx = rx;
  hal_mutex_unlock(h->mutex);
}
#endif // HAL_TARGET_IS_MOCK
