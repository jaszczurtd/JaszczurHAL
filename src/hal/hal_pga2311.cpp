#include "hal_pga2311.h"

#ifdef HAL_ENABLE_PGA2311

#include "hal_sync.h"
#include "impl/shared/drivers/pga2311/pga2311_driver.h"

#include <cmath>
#include <string.h>

struct hal_pga2311_impl_s {
  bool in_use;
  bool muted;
  hal_pga2311_config_t cfg;
  uint8_t target_left_code;
  uint8_t target_right_code;
  hal_mutex_t mutex;
};

static hal_pga2311_impl_t s_pool[HAL_PGA2311_MAX_INSTANCES];

static bool is_valid_handle(const hal_pga2311_impl_t *h) {
  return (h != NULL) && h->in_use;
}

static uint8_t normalize_spi_bus(uint8_t bus) { return (bus == 1u) ? 1u : 0u; }

static uint32_t normalize_spi_clock(uint32_t hz) {
  return hz ? hz : HAL_PGA2311_SPI_DEFAULT_HZ;
}

static uint8_t normalize_spi_mode(uint8_t mode) {
  return (mode <= HAL_SPI_MODE3) ? mode : HAL_SPI_MODE0;
}

static uint8_t normalize_spi_bit_order(uint8_t bit_order) {
  return (bit_order == HAL_SPI_LSBFIRST) ? HAL_SPI_LSBFIRST : HAL_SPI_MSBFIRST;
}

static void normalize_config(hal_pga2311_config_t *cfg) {
  if (cfg == NULL) {
    return;
  }
  cfg->spi_bus = normalize_spi_bus(cfg->spi_bus);
  cfg->spi_clock_hz = normalize_spi_clock(cfg->spi_clock_hz);
  cfg->spi_mode = normalize_spi_mode(cfg->spi_mode);
  cfg->spi_bit_order = normalize_spi_bit_order(cfg->spi_bit_order);
}

static hal_pga2311_impl_t *pool_alloc(void) {
  hal_pga2311_impl_t *slot = NULL;
  hal_critical_section_enter();
  for (int i = 0; i < HAL_PGA2311_MAX_INSTANCES; ++i) {
    if (!s_pool[i].in_use) {
      s_pool[i].in_use = true;
      slot = &s_pool[i];
      break;
    }
  }
  hal_critical_section_exit();
  return slot;
}

static void pool_release(hal_pga2311_impl_t *slot) {
  if (slot == NULL) {
    return;
  }
  if (slot->mutex != NULL) {
    hal_mutex_destroy(slot->mutex);
  }
  memset(slot, 0, sizeof(*slot));
}

hal_pga2311_config_t hal_pga2311_default_config(void) {
  hal_pga2311_config_t cfg = {};
  cfg.spi_bus = 0u;
  cfg.cs_pin = HAL_PGA2311_PIN_NONE;
  cfg.mute_pin = HAL_PGA2311_MUTE_PIN_NONE;
  cfg.mute_polarity = HAL_PGA2311_MUTE_ACTIVE_LOW;
  cfg.spi_clock_hz = HAL_PGA2311_SPI_DEFAULT_HZ;
  cfg.spi_bit_order = HAL_SPI_MSBFIRST;
  cfg.spi_mode = HAL_SPI_MODE0;
  cfg.start_muted = false;
  return cfg;
}

hal_pga2311_t hal_pga2311_init(const hal_pga2311_config_t *cfg) {
  if (cfg == NULL) {
    return NULL;
  }

  hal_pga2311_config_t normalized = *cfg;
  if (!hal_pga2311_driver_validate_config(&normalized)) {
    return NULL;
  }
  normalize_config(&normalized);

  hal_pga2311_impl_t *h = pool_alloc();
  if (h == NULL) {
    HAL_ASSERT(
        0, "hal_pga2311: pool exhausted - increase HAL_PGA2311_MAX_INSTANCES");
    return NULL;
  }

  memset(h, 0, sizeof(*h));
  h->in_use = true;
  h->cfg = normalized;
  h->target_left_code = HAL_PGA2311_CODE_0DB;
  h->target_right_code = HAL_PGA2311_CODE_0DB;
  h->muted = false;
  h->mutex = hal_mutex_create();
  if (h->mutex == NULL) {
    pool_release(h);
    return NULL;
  }

  hal_pga2311_driver_init_pins(&h->cfg);

  if (h->cfg.start_muted) {
    if (h->cfg.mute_pin != HAL_PGA2311_MUTE_PIN_NONE) {
      hal_pga2311_driver_set_hw_mute(&h->cfg, true);
      h->muted = true;
    } else {
      const bool ok = hal_pga2311_driver_write_codes(
          &h->cfg, HAL_PGA2311_CODE_MUTE, HAL_PGA2311_CODE_MUTE);
      if (!ok) {
        pool_release(h);
        return NULL;
      }
      h->muted = true;
    }
  }

  return h;
}

void hal_pga2311_deinit(hal_pga2311_t h) {
  if (!is_valid_handle(h)) {
    return;
  }
  pool_release(h);
}

bool hal_pga2311_set_raw(hal_pga2311_t h, uint8_t left_code,
                         uint8_t right_code) {
  if (!is_valid_handle(h)) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  h->target_left_code = left_code;
  h->target_right_code = right_code;

  bool ok = true;
  if (!(h->muted && h->cfg.mute_pin == HAL_PGA2311_MUTE_PIN_NONE)) {
    ok = hal_pga2311_driver_write_codes(&h->cfg, left_code, right_code);
  }

  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_pga2311_set_raw_both(hal_pga2311_t h, uint8_t code) {
  return hal_pga2311_set_raw(h, code, code);
}

bool hal_pga2311_gain_half_db_to_raw(int16_t half_db, uint8_t *out_code) {
  if (out_code == NULL) {
    return false;
  }
  if (half_db < HAL_PGA2311_GAIN_HALF_DB_MIN ||
      half_db > HAL_PGA2311_GAIN_HALF_DB_MAX) {
    return false;
  }

  const int16_t code = (int16_t)(half_db + 192);
  if (code < (int16_t)HAL_PGA2311_CODE_MIN ||
      code > (int16_t)HAL_PGA2311_CODE_MAX) {
    return false;
  }

  *out_code = (uint8_t)code;
  return true;
}

bool hal_pga2311_raw_to_gain_half_db(uint8_t code, int16_t *out_half_db) {
  if (out_half_db == NULL || code == HAL_PGA2311_CODE_MUTE) {
    return false;
  }
  *out_half_db = (int16_t)code - 192;
  return true;
}

bool hal_pga2311_set_gain_half_db(hal_pga2311_t h, int16_t left_half_db,
                                  int16_t right_half_db) {
  uint8_t left_code = 0u;
  uint8_t right_code = 0u;
  if (!hal_pga2311_gain_half_db_to_raw(left_half_db, &left_code) ||
      !hal_pga2311_gain_half_db_to_raw(right_half_db, &right_code)) {
    return false;
  }
  return hal_pga2311_set_raw(h, left_code, right_code);
}

static int16_t db_to_half_db(float db) {
  return (int16_t)std::lround((double)db * 2.0);
}

bool hal_pga2311_set_gain_db(hal_pga2311_t h, float left_db, float right_db) {
  if (left_db < HAL_PGA2311_GAIN_DB_MIN || left_db > HAL_PGA2311_GAIN_DB_MAX) {
    return false;
  }
  if (right_db < HAL_PGA2311_GAIN_DB_MIN ||
      right_db > HAL_PGA2311_GAIN_DB_MAX) {
    return false;
  }
  return hal_pga2311_set_gain_half_db(h, db_to_half_db(left_db),
                                      db_to_half_db(right_db));
}

bool hal_pga2311_set_gain_db_both(hal_pga2311_t h, float db) {
  return hal_pga2311_set_gain_db(h, db, db);
}

bool hal_pga2311_set_mute(hal_pga2311_t h, bool mute) {
  if (!is_valid_handle(h)) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  if (h->muted == mute) {
    hal_mutex_unlock(h->mutex);
    return true;
  }

  bool ok = true;
  if (h->cfg.mute_pin != HAL_PGA2311_MUTE_PIN_NONE) {
    hal_pga2311_driver_set_hw_mute(&h->cfg, mute);
  } else if (mute) {
    ok = hal_pga2311_driver_write_codes(&h->cfg, HAL_PGA2311_CODE_MUTE,
                                        HAL_PGA2311_CODE_MUTE);
  } else {
    ok = hal_pga2311_driver_write_codes(&h->cfg, h->target_left_code,
                                        h->target_right_code);
  }

  if (ok) {
    h->muted = mute;
  }

  hal_mutex_unlock(h->mutex);
  return ok;
}

bool hal_pga2311_is_muted(hal_pga2311_t h) {
  if (!is_valid_handle(h)) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  const bool muted = h->muted;
  hal_mutex_unlock(h->mutex);
  return muted;
}

bool hal_pga2311_get_target_raw(hal_pga2311_t h, uint8_t *left_code,
                                uint8_t *right_code) {
  if (!is_valid_handle(h) || left_code == NULL || right_code == NULL) {
    return false;
  }
  hal_mutex_lock(h->mutex);
  *left_code = h->target_left_code;
  *right_code = h->target_right_code;
  hal_mutex_unlock(h->mutex);
  return true;
}

bool hal_pga2311_get_target_gain_half_db(hal_pga2311_t h, int16_t *left_half_db,
                                         int16_t *right_half_db) {
  if (!is_valid_handle(h) || left_half_db == NULL || right_half_db == NULL) {
    return false;
  }

  hal_mutex_lock(h->mutex);
  const uint8_t left_code = h->target_left_code;
  const uint8_t right_code = h->target_right_code;
  hal_mutex_unlock(h->mutex);

  if (!hal_pga2311_raw_to_gain_half_db(left_code, left_half_db) ||
      !hal_pga2311_raw_to_gain_half_db(right_code, right_half_db)) {
    return false;
  }
  return true;
}

#endif /* HAL_ENABLE_PGA2311 */
