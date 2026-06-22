#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"
#ifdef HAL_ENABLE_CAN

#include "../../hal_can.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#ifdef HAL_ENABLE_MCP2515
#include "../shared/mcp2515/hal_can_mcp2515.h"
#endif

#include <new>

struct hal_can_impl_s {
  hal_can_backend_t backend;
  bool in_use;
  bool started;
  hal_can_mode_t mode;
  hal_mutex_t mutex;
  union {
    uint8_t dummy;
#ifdef HAL_ENABLE_MCP2515
    alignas(JHMCP2515) uint8_t mcp2515_mem[sizeof(JHMCP2515)];
#endif
  } storage;
};

static hal_can_impl_t s_pool[HAL_CAN_MAX_INSTANCES];

#ifdef HAL_ENABLE_MCP2515
static inline JHMCP2515 *as_mcp2515(hal_can_impl_t *h) {
  return reinterpret_cast<JHMCP2515 *>(h->storage.mcp2515_mem);
}
#endif

static void release_slot(hal_can_impl_t *h) {
  if (!h) {
    return;
  }
  if (h->mutex) {
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
  }
  h->in_use = false;
}

static bool mode_supported(hal_can_mode_t mode) {
  const hal_can_mode_t supported = HAL_CAN_MODE_LOOPBACK |
                                   HAL_CAN_MODE_LISTEN_ONLY |
                                   HAL_CAN_MODE_ONE_SHOT | HAL_CAN_MODE_SLEEP;
  if ((mode & ~supported) != 0u) {
    return false;
  }
  uint8_t ops = 0u;
  ops += (mode & HAL_CAN_MODE_LOOPBACK) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_LISTEN_ONLY) != 0u ? 1u : 0u;
  ops += (mode & HAL_CAN_MODE_SLEEP) != 0u ? 1u : 0u;
  return ops <= 1u;
}

hal_can_t hal_can_create(const hal_can_config_t *cfg) {
  hal_can_config_t effective = cfg ? *cfg : hal_can_default_config();

  const int max_instances =
      hal_get_config()->can_max_instances < HAL_CAN_MAX_INSTANCES
          ? hal_get_config()->can_max_instances
          : HAL_CAN_MAX_INSTANCES;
  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < max_instances; i++) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();

  if (slot < 0) {
    HAL_ASSERT(0, "hal_can: pool exhausted - increase HAL_CAN_MAX_INSTANCES");
    return NULL;
  }

  hal_can_impl_t *h = &s_pool[slot];
  h->backend = effective.backend;
  h->started = true;
  h->mode = effective.mcp2515.one_shot_tx ? HAL_CAN_MODE_ONE_SHOT
                                          : HAL_CAN_MODE_NORMAL;
  h->mutex = hal_mutex_create();

#ifdef HAL_ENABLE_MCP2515
  if (effective.backend == HAL_CAN_BACKEND_MCP2515) {
    JHMCP2515 *mcp = new (h->storage.mcp2515_mem)
        JHMCP2515(effective.mcp2515.cs_pin, effective.mcp2515.spi_bus);
    if (!hal_can_mcp2515_init(mcp, &effective.mcp2515)) {
      hal_can_mcp2515_deinit(mcp);
      release_slot(h);
      return NULL;
    }
    return h;
  }
#endif

  hal_derr_limited("can", "unsupported CAN backend %d", (int)effective.backend);
  release_slot(h);
  return NULL;
}

void hal_can_destroy(hal_can_t h) {
  if (!h)
    return;
  hal_mutex_t m = h->mutex;
  if (m) {
    hal_mutex_lock(m);
  }
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    hal_can_mcp2515_deinit(as_mcp2515(h));
  }
#endif
  h->in_use = false;
  h->mutex = NULL;
  if (m) {
    hal_mutex_unlock(m);
    hal_mutex_destroy(m);
  }
}

bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data) {
  if (!h || !h->in_use) {
    hal_derr_limited("can", "send called with NULL handle");
    return false;
  }
  if (!h->started || (h->mode & HAL_CAN_MODE_SLEEP) != 0u) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_send(as_mcp2515(h), id, len, data);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame) {
  if (!h || !h->in_use) {
    hal_derr_limited("can", "send_frame called with NULL handle");
    return false;
  }
  if (!frame) {
    hal_derr_limited("can", "send_frame called with NULL frame");
    return false;
  }
  if (!h->started || (h->mode & HAL_CAN_MODE_SLEEP) != 0u) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_send_frame(as_mcp2515(h), frame);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data) {
  if (!h || !h->in_use) {
    hal_derr_limited("can", "receive called with NULL handle");
    return false;
  }
  if (!id || !len || !data) {
    hal_derr_limited("can", "receive called with NULL output pointer(s)");
    return false;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_receive(as_mcp2515(h), id, len, data);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame) {
  if (!h || !h->in_use) {
    hal_derr_limited("can", "receive_frame called with NULL handle");
    return false;
  }
  if (!frame) {
    hal_derr_limited("can", "receive_frame called with NULL frame");
    return false;
  }

  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_receive_frame(as_mcp2515(h), frame);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_start(hal_can_t h) {
  if (!h || !h->in_use) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = true;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_start(as_mcp2515(h), h->mode);
  }
#endif
  if (ok) {
    h->started = true;
  }
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_stop(hal_can_t h) {
  if (!h || !h->in_use) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = true;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_stop(as_mcp2515(h));
  }
#endif
  if (ok) {
    h->started = false;
  }
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_set_mode(hal_can_t h, hal_can_mode_t mode) {
  if (!h || !h->in_use || !mode_supported(mode)) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = true;
#ifdef HAL_ENABLE_MCP2515
  if (h->started && h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_set_mode(as_mcp2515(h), mode);
  }
#endif
  if (ok) {
    h->mode = mode;
  }
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode) {
  if (!h || !h->in_use || !mode) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  *mode = h->mode;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_can_get_state(hal_can_t h, hal_can_state_t *state) {
  if (!h || !h->in_use || !state) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_get_state(as_mcp2515(h), h->started, state);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_get_error_counters(hal_can_t h,
                                hal_can_error_counters_t *counters) {
  if (!h || !h->in_use || !counters) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_get_error_counters(as_mcp2515(h), counters);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_available(hal_can_t h) {
  if (!h || !h->in_use)
    return false;
  hal_mutex_lock(h->mutex);
  bool available = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    available = hal_can_mcp2515_available(as_mcp2515(h));
  }
#endif
  hal_mutex_unlock(h->mutex);
  return available;
}

bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1) {
  if (!h || !h->in_use)
    return false;
  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_set_std_filters(as_mcp2515(h), id0, id1);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_can_set_filter(hal_can_t h, uint8_t index,
                        const hal_can_filter_t *filter) {
  if (!h || !h->in_use || !filter) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  bool ok = false;
#ifdef HAL_ENABLE_MCP2515
  if (h->backend == HAL_CAN_BACKEND_MCP2515) {
    ok = hal_can_mcp2515_set_filter(as_mcp2515(h), index, filter);
  }
#endif
  hal_mutex_unlock(h->mutex);
  return ok;
}

#endif /* HAL_ENABLE_CAN */
#endif /* HAL_TARGET_IS_STM32G474 */
