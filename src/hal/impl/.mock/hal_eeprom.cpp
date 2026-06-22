#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK
#include "../../hal_config.h"
#include "../../hal_eeprom.h"
#include "../../hal_sync.h"
#include "hal_mock.h"

#include <string.h>

/* ── Mock storage ─────────────────────────────────────────────────────────────
 */

static uint8_t s_mem[MOCK_EEPROM_BUF_SIZE] = {};
static hal_eeprom_type_t s_type = HAL_EEPROM_AT24C256;
static uint16_t s_size = 0;
static bool s_committed = false;
static uint32_t s_write_count = 0;
static hal_mutex_t s_eeprom_mutex = NULL;
static hal_eeprom_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;

static void eeprom_ensure_mutex(void) {
  if (s_eeprom_mutex == NULL) {
    s_eeprom_mutex = hal_mutex_create();
  }
}

static bool eeprom_type_uses_requested_size(hal_eeprom_type_t type) {
  return type == HAL_EEPROM_DEFAULT || type == HAL_EEPROM_RP2040 ||
         type == HAL_EEPROM_STM32_FLASH || type == HAL_EEPROM_FLASH;
}

static uint16_t active_size(void) {
  return (s_size < MOCK_EEPROM_BUF_SIZE) ? s_size
                                         : (uint16_t)MOCK_EEPROM_BUF_SIZE;
}

static uint16_t clipped_len(uint16_t addr, uint16_t len) {
  const uint16_t size = active_size();
  if (addr >= size) {
    return 0u;
  }
  const uint16_t remaining = (uint16_t)(size - addr);
  return (len <= remaining) ? len : remaining;
}

static void notify_progress(void) {
  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
}

/* ── Public API (mock implementations) ─────────────────────────────────── */

void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr) {
  (void)i2c_addr;
  eeprom_ensure_mutex();
  s_type = type;
  s_size = eeprom_type_uses_requested_size(type) ? size : (uint16_t)32768U;
  s_committed = false;
  memset(s_mem, 0, sizeof(s_mem));
}

void hal_eeprom_set_progress_callback(hal_eeprom_progress_callback_t callback,
                                      void *ctx) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  s_progress_callback = callback;
  s_progress_ctx = ctx;
  hal_mutex_unlock(s_eeprom_mutex);
}

/* ── Lock-free helpers (caller holds s_eeprom_mutex) ────────────────── */

static void write_byte_nolock(uint16_t addr, uint8_t val) {
  if (clipped_len(addr, 1u) == 1u) {
    s_mem[addr] = val;
    s_write_count++;
  }
}

static uint8_t read_byte_nolock(uint16_t addr) {
  return (clipped_len(addr, 1u) == 1u) ? s_mem[addr] : 0;
}

void hal_eeprom_write_byte(uint16_t addr, uint8_t val) {
  hal_mutex_lock(s_eeprom_mutex);
  write_byte_nolock(addr, val);
  hal_mutex_unlock(s_eeprom_mutex);
}

uint8_t hal_eeprom_read_byte(uint16_t addr) {
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t val = read_byte_nolock(addr);
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

void hal_eeprom_write_int(uint16_t addr, int32_t val) {
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t raw[4] = {
      (uint8_t)((val >> 0) & 0xFF),
      (uint8_t)((val >> 8) & 0xFF),
      (uint8_t)((val >> 16) & 0xFF),
      (uint8_t)((val >> 24) & 0xFF),
  };
  const uint16_t n = clipped_len(addr, sizeof(raw));
  for (uint16_t i = 0u; i < n; ++i) {
    write_byte_nolock((uint16_t)(addr + i), raw[i]);
  }
  hal_mutex_unlock(s_eeprom_mutex);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t raw[4] = {0u, 0u, 0u, 0u};
  const uint16_t n = clipped_len(addr, sizeof(raw));
  for (uint16_t i = 0u; i < n; ++i) {
    raw[i] = read_byte_nolock((uint16_t)(addr + i));
  }
  int32_t val = (int32_t)(((uint32_t)raw[0]) | ((uint32_t)raw[1] << 8) |
                          ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24));
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

void hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data, uint16_t len) {
  if (!data || len == 0u) {
    return;
  }
  hal_mutex_lock(s_eeprom_mutex);
  const uint16_t n = clipped_len(addr, len);
  for (uint16_t i = 0; i < n; i++) {
    write_byte_nolock((uint16_t)(addr + i), data[i]);
  }
  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if (!out || len == 0u) {
    return;
  }
  hal_mutex_lock(s_eeprom_mutex);
  const uint16_t n = clipped_len(addr, len);
  for (uint16_t i = 0; i < n; i++) {
    out[i] = read_byte_nolock((uint16_t)(addr + i));
  }
  for (uint16_t i = n; i < len; i++) {
    out[i] = 0u;
  }
  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_commit(void) {
  hal_mutex_lock(s_eeprom_mutex);
  s_committed = true;
  notify_progress();
  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_reset(void) {
  hal_mutex_lock(s_eeprom_mutex);
  memset(s_mem, 0, sizeof(s_mem));
  s_committed = true;
  notify_progress();
  hal_mutex_unlock(s_eeprom_mutex);
}

uint16_t hal_eeprom_size(void) { return s_size; }

/* ── Mock helpers ───────────────────────────────────────────────────────── */

uint8_t hal_mock_eeprom_get_byte(uint16_t addr) {
  return (clipped_len(addr, 1u) == 1u) ? s_mem[addr] : 0;
}

hal_eeprom_type_t hal_mock_eeprom_get_type(void) { return s_type; }

bool hal_mock_eeprom_was_committed(void) { return s_committed; }

void hal_mock_eeprom_clear_committed_flag(void) { s_committed = false; }

uint32_t hal_mock_eeprom_get_write_count(void) {
  hal_mutex_lock(s_eeprom_mutex);
  uint32_t v = s_write_count;
  hal_mutex_unlock(s_eeprom_mutex);
  return v;
}

void hal_mock_eeprom_clear_write_count(void) {
  hal_mutex_lock(s_eeprom_mutex);
  s_write_count = 0;
  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_mock_eeprom_reset(void) {
  memset(s_mem, 0, sizeof(s_mem));
  s_type = HAL_EEPROM_AT24C256;
  s_size = 0;
  s_committed = false;
  s_write_count = 0;
  s_progress_callback = NULL;
  s_progress_ctx = NULL;
}
#endif // HAL_TARGET_IS_MOCK
