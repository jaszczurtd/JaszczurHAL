#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#ifdef HAL_ENABLE_ONEWIRE

#include "../../hal_onewire.h"
#include "../../hal_sync.h"
#include "hal_mock.h"

#include <string.h>

#define HAL_ONEWIRE_MOCK_READ_BUF_SIZE 512
#define HAL_ONEWIRE_MOCK_MAX_SEARCH_ROMS 8

struct hal_onewire_impl_s {
  bool in_use;
  uint8_t pin;
  hal_mutex_t mutex;

  bool presence;
  uint8_t read_buf[HAL_ONEWIRE_MOCK_READ_BUF_SIZE];
  int read_len;
  int read_pos;

  uint8_t search_roms[HAL_ONEWIRE_MOCK_MAX_SEARCH_ROMS][8];
  uint8_t search_count;
  uint8_t search_cursor;
  bool target_family_enabled;
  uint8_t target_family;

  uint8_t last_selected_rom[8];
  bool has_selected_rom;
  uint8_t last_write;
  bool last_write_power;
  uint8_t last_write_bit;
  uint32_t reset_count;
  uint32_t skip_count;
  uint32_t depower_count;

  int lock_depth;
  int max_lock_depth;
};

static hal_onewire_impl_t s_pool[HAL_ONEWIRE_MAX_INSTANCES];
static hal_mutex_t s_onewire_bus_mutex = NULL;

static inline void release_pool_slot(hal_onewire_impl_t *h) {
  hal_critical_section_enter();
  h->in_use = false;
  hal_critical_section_exit();
}

static void onewire_ensure_bus_mutex(void) {
  if (!s_onewire_bus_mutex) {
    hal_critical_section_enter();
    if (!s_onewire_bus_mutex) {
      s_onewire_bus_mutex = hal_mutex_create();
    }
    hal_critical_section_exit();
  }
}

static inline void onewire_enter_op(hal_onewire_t h) {
  hal_mutex_lock(h->mutex);
  onewire_ensure_bus_mutex();
  if (s_onewire_bus_mutex) {
    hal_mutex_lock(s_onewire_bus_mutex);
  }
  h->lock_depth++;
  if (h->lock_depth > h->max_lock_depth) {
    h->max_lock_depth = h->lock_depth;
  }
}

static inline void onewire_exit_op(hal_onewire_t h) {
  if (h->lock_depth > 0) {
    h->lock_depth--;
  }
  if (s_onewire_bus_mutex) {
    hal_mutex_unlock(s_onewire_bus_mutex);
  }
  hal_mutex_unlock(h->mutex);
}

hal_onewire_t hal_onewire_init(uint8_t data_pin) {
  hal_critical_section_enter();
  int slot = -1;
  for (int i = 0; i < HAL_ONEWIRE_MAX_INSTANCES; ++i) {
    if (!s_pool[i].in_use) {
      slot = i;
      s_pool[i].in_use = true;
      break;
    }
  }
  hal_critical_section_exit();

  HAL_ASSERT(
      slot >= 0,
      "hal_onewire: pool exhausted - increase HAL_ONEWIRE_MAX_INSTANCES");
  if (slot < 0) {
    return NULL;
  }

  hal_onewire_impl_t *h = &s_pool[slot];
  memset(h, 0, sizeof(*h));
  h->in_use = true;
  h->pin = data_pin;
  h->presence = true;
  h->mutex = hal_mutex_create();
  if (!h->mutex) {
    release_pool_slot(h);
    return NULL;
  }
  return h;
}

void hal_onewire_deinit(hal_onewire_t h) {
  if (!h) {
    return;
  }

  hal_mutex_destroy(h->mutex);
  h->mutex = NULL;
  release_pool_slot(h);
}

bool hal_onewire_reset(hal_onewire_t h) {
  if (!h) {
    return false;
  }

  onewire_enter_op(h);
  h->reset_count++;
  const bool present = h->presence;
  onewire_exit_op(h);
  return present;
}

void hal_onewire_select(hal_onewire_t h, const uint8_t rom[8]) {
  if (!h || !rom) {
    return;
  }

  onewire_enter_op(h);
  memcpy(h->last_selected_rom, rom, 8);
  h->has_selected_rom = true;
  onewire_exit_op(h);
}

void hal_onewire_skip(hal_onewire_t h) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->skip_count++;
  onewire_exit_op(h);
}

void hal_onewire_write(hal_onewire_t h, uint8_t value, bool power) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->last_write = value;
  h->last_write_power = power;
  onewire_exit_op(h);
}

size_t hal_onewire_write_bytes(hal_onewire_t h, const uint8_t *data,
                               uint16_t len, bool power) {
  if (!h || !data || len == 0u) {
    return 0u;
  }

  onewire_enter_op(h);
  h->last_write = data[len - 1u];
  h->last_write_power = power;
  onewire_exit_op(h);
  return (size_t)len;
}

uint8_t hal_onewire_read(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }

  onewire_enter_op(h);
  uint8_t out = 0u;
  if (h->read_pos < h->read_len) {
    out = h->read_buf[h->read_pos++];
  }
  onewire_exit_op(h);
  return out;
}

size_t hal_onewire_read_bytes(hal_onewire_t h, uint8_t *out, uint16_t len) {
  if (!h || !out || len == 0u) {
    return 0u;
  }

  onewire_enter_op(h);
  for (uint16_t i = 0; i < len; ++i) {
    out[i] = (h->read_pos < h->read_len) ? h->read_buf[h->read_pos++] : 0u;
  }
  onewire_exit_op(h);
  return (size_t)len;
}

void hal_onewire_write_bit(hal_onewire_t h, uint8_t bit) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->last_write_bit = (uint8_t)(bit ? 1u : 0u);
  onewire_exit_op(h);
}

uint8_t hal_onewire_read_bit(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }

  onewire_enter_op(h);
  uint8_t out = 0u;
  if (h->read_pos < h->read_len) {
    out = (uint8_t)(h->read_buf[h->read_pos++] & 0x01u);
  }
  onewire_exit_op(h);
  return out;
}

void hal_onewire_depower(hal_onewire_t h) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->depower_count++;
  onewire_exit_op(h);
}

void hal_onewire_reset_search(hal_onewire_t h) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->search_cursor = 0u;
  onewire_exit_op(h);
}

void hal_onewire_target_search(hal_onewire_t h, uint8_t family_code) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->target_family_enabled = true;
  h->target_family = family_code;
  h->search_cursor = 0u;
  onewire_exit_op(h);
}

bool hal_onewire_search(hal_onewire_t h, uint8_t out_rom[8], bool search_mode) {
  if (!h || !out_rom) {
    return false;
  }

  onewire_enter_op(h);
  (void)search_mode;

  bool found = false;
  while (h->search_cursor < h->search_count) {
    const uint8_t *rom = h->search_roms[h->search_cursor++];
    if (h->target_family_enabled && rom[0] != h->target_family) {
      continue;
    }
    memcpy(out_rom, rom, 8);
    found = true;
    break;
  }

  onewire_exit_op(h);
  return found;
}

/* CRC helpers now live in hal_crc.cpp (hal_crc8_maxim / hal_crc16_maxim). */

/* ── Mock helpers ───────────────────────────────────────────────────────── */

void hal_mock_onewire_set_presence(hal_onewire_t h, bool present) {
  if (!h) {
    return;
  }
  onewire_enter_op(h);
  h->presence = present;
  onewire_exit_op(h);
}

void hal_mock_onewire_inject_read(hal_onewire_t h, const uint8_t *data,
                                  int len) {
  if (!h || !data || len <= 0) {
    return;
  }

  if (len > HAL_ONEWIRE_MOCK_READ_BUF_SIZE) {
    len = HAL_ONEWIRE_MOCK_READ_BUF_SIZE;
  }

  onewire_enter_op(h);
  memcpy(h->read_buf, data, (size_t)len);
  h->read_len = len;
  h->read_pos = 0;
  onewire_exit_op(h);
}

void hal_mock_onewire_reset_search_roms(hal_onewire_t h) {
  if (!h) {
    return;
  }

  onewire_enter_op(h);
  h->search_count = 0u;
  h->search_cursor = 0u;
  h->target_family_enabled = false;
  onewire_exit_op(h);
}

bool hal_mock_onewire_push_search_rom(hal_onewire_t h, const uint8_t rom[8]) {
  if (!h || !rom) {
    return false;
  }

  onewire_enter_op(h);
  bool ok = false;
  if (h->search_count < HAL_ONEWIRE_MOCK_MAX_SEARCH_ROMS) {
    memcpy(h->search_roms[h->search_count], rom, 8);
    h->search_count++;
    ok = true;
  }
  onewire_exit_op(h);
  return ok;
}

uint8_t hal_mock_onewire_get_last_write(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }
  onewire_enter_op(h);
  uint8_t v = h->last_write;
  onewire_exit_op(h);
  return v;
}

uint8_t hal_mock_onewire_get_last_write_bit(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }
  onewire_enter_op(h);
  uint8_t v = h->last_write_bit;
  onewire_exit_op(h);
  return v;
}

bool hal_mock_onewire_get_last_selected_rom(hal_onewire_t h,
                                            uint8_t out_rom[8]) {
  if (!h || !out_rom) {
    return false;
  }
  onewire_enter_op(h);
  const bool ok = h->has_selected_rom;
  if (ok) {
    memcpy(out_rom, h->last_selected_rom, 8);
  }
  onewire_exit_op(h);
  return ok;
}

uint32_t hal_mock_onewire_get_reset_count(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }
  onewire_enter_op(h);
  uint32_t v = h->reset_count;
  onewire_exit_op(h);
  return v;
}

uint32_t hal_mock_onewire_get_skip_count(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }
  onewire_enter_op(h);
  uint32_t v = h->skip_count;
  onewire_exit_op(h);
  return v;
}

uint32_t hal_mock_onewire_get_depower_count(hal_onewire_t h) {
  if (!h) {
    return 0u;
  }
  onewire_enter_op(h);
  uint32_t v = h->depower_count;
  onewire_exit_op(h);
  return v;
}

int hal_mock_onewire_get_max_lock_depth(hal_onewire_t h) {
  if (!h) {
    return 0;
  }
  onewire_enter_op(h);
  int v = h->max_lock_depth;
  onewire_exit_op(h);
  return v;
}

#endif /* HAL_ENABLE_ONEWIRE */
#endif // HAL_TARGET_IS_MOCK
