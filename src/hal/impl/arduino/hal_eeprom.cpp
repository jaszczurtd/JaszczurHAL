#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"
#ifdef HAL_ENABLE_EEPROM

#include "../../hal_eeprom.h"
#include "../../hal_i2c.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../shared/hal_mutex_once.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

/* ── Internal state
 * ───────────────────────────────────────────────────────────── */

static hal_eeprom_type_t s_type = HAL_EEPROM_AT24C256;
static uint16_t s_size = 32768U;
static uint8_t s_i2c_addr = EEPROM_I2C_ADDRESS;
static hal_mutex_t s_eeprom_mutex = NULL;
static hal_eeprom_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;

static void eeprom_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_eeprom_mutex);
}

static hal_eeprom_type_t normalize_type(hal_eeprom_type_t type) {
  if (type == HAL_EEPROM_DEFAULT || type == HAL_EEPROM_FLASH) {
    return HAL_EEPROM_RP2040;
  }
  return type;
}

static uint16_t clipped_len(uint16_t addr, uint16_t len) {
  if (addr >= s_size) {
    return 0u;
  }
  const uint16_t remaining = (uint16_t)(s_size - addr);
  return (len <= remaining) ? len : remaining;
}

static void notify_progress(void) {
  if (s_progress_callback != NULL) {
    s_progress_callback(s_progress_ctx);
  }
}

/* ── AT24C256 helpers ───────────────────────────────────────────────────── */

static bool at24_wait_ready(void) {
  uint32_t waited_us = 0u;
  while (hal_i2c_is_busy(s_i2c_addr)) {
    if (waited_us >= HAL_AT24C256_WRITE_TIMEOUT_US) {
      return false;
    }
    hal_delay_us(HAL_AT24C256_ACK_POLL_US);
    waited_us += HAL_AT24C256_ACK_POLL_US;
    notify_progress();
  }
  return true;
}

static bool at24_write_page(uint16_t addr, const uint8_t *data, uint8_t len) {
  if (data == NULL || len == 0u || addr >= s_size) {
    return false;
  }

  hal_i2c_begin_transmission(s_i2c_addr);
  bool ok = hal_i2c_write((uint8_t)(addr >> 8)) == 1u;
  ok = ok && (hal_i2c_write((uint8_t)(addr & 0xFFu)) == 1u);
  for (uint8_t i = 0u; ok && i < len; ++i) {
    ok = hal_i2c_write(data[i]) == 1u;
  }
  const uint8_t status = hal_i2c_end_transmission();
  if (!ok || status != 0u) {
    return false;
  }

  return at24_wait_ready();
}

static bool at24_write_bytes(uint16_t addr, const uint8_t *data, uint16_t len) {
  uint16_t written = 0u;
  while (written < len) {
    const uint16_t current = (uint16_t)(addr + written);
    const uint8_t page_remaining =
        (uint8_t)(HAL_AT24C256_PAGE_SIZE - (current % HAL_AT24C256_PAGE_SIZE));
    uint16_t chunk = (uint16_t)(len - written);
    if (chunk > page_remaining) {
      chunk = page_remaining;
    }
    if (chunk > HAL_AT24C256_PAGE_SIZE) {
      chunk = HAL_AT24C256_PAGE_SIZE;
    }
    if (!at24_write_page(current, data + written, (uint8_t)chunk)) {
      return false;
    }
    written = (uint16_t)(written + chunk);
    notify_progress();
  }
  return true;
}

static bool at24_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  while (len > 0u) {
    uint8_t chunk = (len > 255u) ? 255u : (uint8_t)len;
    uint8_t addr_buf[2] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFFu)};
    if (!hal_i2c_write_read(s_i2c_addr, addr_buf, sizeof(addr_buf), out,
                            chunk)) {
      return false;
    }
    addr = (uint16_t)(addr + chunk);
    out += chunk;
    len = (uint16_t)(len - chunk);
  }
  return true;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr) {
  eeprom_ensure_mutex();
  s_type = normalize_type(type);
  if (s_type == HAL_EEPROM_RP2040) {
    s_size = size;
    EEPROM.begin(size);
  } else {
    s_size = 32768U;
    s_i2c_addr = (i2c_addr != 0) ? i2c_addr : EEPROM_I2C_ADDRESS;
  }
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
  if (s_type == HAL_EEPROM_RP2040) {
    if (addr < s_size) {
      EEPROM.write(addr, val);
    }
  } else {
    (void)at24_write_bytes(addr, &val, clipped_len(addr, 1u));
  }
}

static uint8_t read_byte_nolock(uint16_t addr) {
  if (addr >= s_size) {
    return 0u;
  }
  if (s_type == HAL_EEPROM_RP2040) {
    return EEPROM.read(addr);
  }
  uint8_t value = 0u;
  (void)at24_read_bytes(addr, &value, 1u);
  return value;
}

void hal_eeprom_write_byte(uint16_t addr, uint8_t val) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  write_byte_nolock(addr, val);
  hal_mutex_unlock(s_eeprom_mutex);
}

uint8_t hal_eeprom_read_byte(uint16_t addr) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t val = read_byte_nolock(addr);
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

void hal_eeprom_write_int(uint16_t addr, int32_t val) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t raw[4] = {
      (uint8_t)((val >> 0) & 0xFF),
      (uint8_t)((val >> 8) & 0xFF),
      (uint8_t)((val >> 16) & 0xFF),
      (uint8_t)((val >> 24) & 0xFF),
  };
  const uint16_t n = clipped_len(addr, sizeof(raw));
  if (s_type == HAL_EEPROM_RP2040) {
    for (uint16_t i = 0u; i < n; ++i) {
      EEPROM.write((uint16_t)(addr + i), raw[i]);
    }
  } else {
    (void)at24_write_bytes(addr, raw, n);
  }
  hal_mutex_unlock(s_eeprom_mutex);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t raw[4] = {0u, 0u, 0u, 0u};
  const uint16_t n = clipped_len(addr, sizeof(raw));
  if (s_type == HAL_EEPROM_RP2040) {
    for (uint16_t i = 0u; i < n; ++i) {
      raw[i] = EEPROM.read((uint16_t)(addr + i));
    }
  } else {
    (void)at24_read_bytes(addr, raw, n);
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
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const uint16_t n = clipped_len(addr, len);
  if (s_type == HAL_EEPROM_RP2040) {
    for (uint16_t i = 0u; i < n; i++) {
      EEPROM.write((uint16_t)(addr + i), data[i]);
    }
  } else {
    (void)at24_write_bytes(addr, data, n);
  }
  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if (!out || len == 0u) {
    return;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const uint16_t n = clipped_len(addr, len);
  if (s_type == HAL_EEPROM_RP2040) {
    for (uint16_t i = 0u; i < n; i++) {
      out[i] = EEPROM.read((uint16_t)(addr + i));
    }
  } else {
    (void)at24_read_bytes(addr, out, n);
  }
  for (uint16_t i = n; i < len; i++) {
    out[i] = 0u;
  }
  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_commit(void) {
  eeprom_ensure_mutex();
  if (s_type == HAL_EEPROM_RP2040) {
    hal_mutex_lock(s_eeprom_mutex);
    EEPROM.commit();
    hal_mutex_unlock(s_eeprom_mutex);
  }
  /* AT24C256: no-op - writes are already committed to the chip */
}

void hal_eeprom_reset(void) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  if (s_type == HAL_EEPROM_RP2040) {
    for (uint32_t a = 0; a < s_size; a++) {
      EEPROM.write((uint16_t)a, 0u);
      notify_progress();
    }
  } else {
    uint8_t zeros[HAL_AT24C256_PAGE_SIZE] = {0u};
    for (uint32_t a = 0; a < s_size; a += HAL_AT24C256_PAGE_SIZE) {
      uint16_t chunk = (uint16_t)(s_size - a);
      if (chunk > HAL_AT24C256_PAGE_SIZE) {
        chunk = HAL_AT24C256_PAGE_SIZE;
      }
      if (!at24_write_bytes((uint16_t)a, zeros, chunk)) {
        break;
      }
    }
  }
  hal_mutex_unlock(s_eeprom_mutex);
  hal_eeprom_commit();
}

uint16_t hal_eeprom_size(void) { return s_size; }

#endif /* HAL_ENABLE_EEPROM */
#endif // HAL_TARGET_IS_RP2040
