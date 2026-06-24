#include "hal_digipot.h"

#ifdef HAL_ENABLE_DIGIPOT

#include "hal_sync.h"
#include "impl/shared/drivers/digipot/hal_digipot_ops.h"

#include <stddef.h>

struct hal_digipot_impl_s {
  bool in_use;
  hal_digipot_config_t cfg;
  const hal_digipot_ops_t *ops;
  hal_mutex_t mutex;
};

static hal_digipot_impl_s s_pool[HAL_DIGIPOT_MAX_INSTANCES];

static hal_digipot_impl_s *pool_alloc(void) {
  hal_digipot_impl_s *slot = NULL;
  hal_critical_section_enter();
  for (int i = 0; i < HAL_DIGIPOT_MAX_INSTANCES; ++i) {
    if (!s_pool[i].in_use) {
      s_pool[i].in_use = true;
      slot = &s_pool[i];
      break;
    }
  }
  hal_critical_section_exit();
  return slot;
}

static const hal_digipot_ops_t *ops_for_chip(hal_digipot_chip_t chip) {
  switch (chip) {
#ifdef HAL_ENABLE_MCP401X
  case HAL_DIGIPOT_CHIP_MCP401X:
    return hal_digipot_mcp401x_ops();
#endif
#ifdef HAL_ENABLE_MAX5395
  case HAL_DIGIPOT_CHIP_MAX5395:
    return hal_digipot_max5395_ops();
#endif
  default:
    return NULL;
  }
}

hal_digipot_t hal_digipot_init(const hal_digipot_config_t *cfg) {
  if (cfg == NULL) {
    return NULL;
  }

  const hal_digipot_ops_t *ops = ops_for_chip(cfg->chip);
  if (ops == NULL || ops->validate == NULL || !ops->validate(cfg)) {
    return NULL;
  }

  hal_digipot_impl_s *h = pool_alloc();
  if (h == NULL) {
    return NULL;
  }
  h->cfg = *cfg;
  h->ops = ops;
  h->mutex = hal_mutex_create();

  bool ok = true;
  if (h->ops->init != NULL) {
    if (h->mutex != NULL) {
      hal_mutex_lock(h->mutex);
    }
    ok = h->ops->init(&h->cfg);
    if (h->mutex != NULL) {
      hal_mutex_unlock(h->mutex);
    }
  }

  if (!ok) {
    hal_digipot_deinit(h);
    return NULL;
  }
  return h;
}

void hal_digipot_deinit(hal_digipot_t h) {
  if (h == NULL) {
    return;
  }
  if (h->mutex != NULL) {
    hal_mutex_destroy(h->mutex);
    h->mutex = NULL;
  }
  h->ops = NULL;
  h->in_use = false;
}

bool hal_digipot_set_resistance(hal_digipot_t h, uint32_t ohms) {
  if (h == NULL || !h->in_use || h->ops == NULL ||
      h->ops->set_resistance == NULL) {
    return false;
  }
  if (h->mutex != NULL) {
    hal_mutex_lock(h->mutex);
  }
  const bool ok = h->ops->set_resistance(&h->cfg, ohms);
  if (h->mutex != NULL) {
    hal_mutex_unlock(h->mutex);
  }
  return ok;
}

uint16_t hal_digipot_step_count(hal_digipot_t h) {
  if (h == NULL || !h->in_use || h->ops == NULL || h->ops->step_count == NULL) {
    return 0u;
  }
  return h->ops->step_count();
}

uint32_t hal_digipot_e2e_resistance(hal_digipot_t h) {
  return (h != NULL && h->in_use) ? h->cfg.e2e_resistance : 0u;
}

hal_digipot_mode_t hal_digipot_mode(hal_digipot_t h) {
  return (h != NULL && h->in_use) ? h->cfg.mode
                                  : HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;
}

#endif /* HAL_ENABLE_DIGIPOT */
