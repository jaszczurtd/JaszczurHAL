#include "../../hal_target.h"
#if HAL_TARGET_IS_RP
#include "../../hal_config.h"
#ifdef HAL_ENABLE_EEPROM

#include "../../hal_eeprom.h"
#ifdef HAL_ENABLE_I2C
#include "../../hal_i2c.h"
#endif
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../shared/hal_mutex_once.h"

#include "drivers/flash/rp_flash_storage.h"
#include <string.h>

/* ── Internal state
 * ───────────────────────────────────────────────────────────── */

static hal_eeprom_type_t s_type = HAL_EEPROM_AT24C256;
static uint16_t s_size = 32768U;
static uint8_t s_i2c_addr = EEPROM_I2C_ADDRESS;
static hal_mutex_t s_eeprom_mutex = NULL;
static hal_eeprom_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;
static uint8_t s_flash_mirror[HAL_RP_FLASH_EEPROM_SIZE];
static jh_rp_flash_partition_t s_flash_partition = {};
static bool s_flash_ready = false;
static bool s_dirty = false;

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

static uint16_t bounded_flash_size(uint16_t requested) {
  uint32_t max_size = s_flash_partition.size;
  if (max_size > sizeof(s_flash_mirror)) {
    max_size = sizeof(s_flash_mirror);
  }
  if (max_size > UINT16_MAX) {
    max_size = UINT16_MAX;
  }
  if (requested == 0u) {
    return (uint16_t)max_size;
  }
  return requested <= max_size ? requested : 0u;
}

static hal_status_t flash_load_mirror(void) {
  s_flash_ready = false;
  const hal_status_t partition_status = jh_rp_flash_storage_partition(
      JH_RP_FLASH_PARTITION_EEPROM, &s_flash_partition);
  if (partition_status != HAL_OK ||
      s_flash_partition.size > sizeof(s_flash_mirror)) {
    return HAL_ECONFIG;
  }

  const hal_status_t read_status = jh_rp_flash_storage_read(
      &s_flash_partition, 0u, s_flash_mirror, s_flash_partition.size);
  if (read_status == HAL_OK) {
    s_flash_ready = true;
  }
  return read_status;
}

static hal_status_t flash_commit_mirror(void) {
  if (!s_flash_ready) {
    return HAL_EUNINIT;
  }
  if (!s_dirty) {
    return HAL_OK;
  }

  notify_progress();
  const hal_status_t status = jh_rp_flash_storage_replace(
      &s_flash_partition, s_flash_mirror, s_flash_partition.size);
  notify_progress();
  if (status == HAL_OK) {
    s_dirty = false;
  }
  return status;
}

/* ── AT24C256 helpers ───────────────────────────────────────────────────── */

#ifdef HAL_ENABLE_I2C

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

#else

static bool at24_write_bytes(uint16_t addr, const uint8_t *data, uint16_t len) {
  (void)addr;
  (void)data;
  (void)len;
  return false;
}

static bool at24_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  (void)addr;
  if (out != NULL && len > 0u) {
    memset(out, 0, len);
  }
  return false;
}

#endif

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * Status for an [addr, addr+len) access against the active device size.
 * Mirrors the clip performed by clipped_len(): HAL_EOVERFLOW means the access
 * is (partly) clipped; the clip side effect is preserved by the callers below.
 */
static hal_status_t eeprom_range_status(uint16_t addr, uint16_t len) {
  if (s_size == 0u) {
    return HAL_EUNINIT;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  if (addr >= s_size || (uint32_t)addr + (uint32_t)len > (uint32_t)s_size) {
    return HAL_EOVERFLOW;
  }
  return HAL_OK;
}

hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr) {
  if (type < HAL_EEPROM_DEFAULT || type > HAL_EEPROM_FLASH) {
    return HAL_EINVAL;
  }
#if !defined(HAL_ENABLE_I2C)
  if (type == HAL_EEPROM_AT24C256) {
    return HAL_ECONFIG;
  }
#endif
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  s_type = normalize_type(type);
  if (s_type == HAL_EEPROM_RP2040) {
    s_dirty = false;
    const hal_status_t flash_status = flash_load_mirror();
    s_size = flash_status == HAL_OK ? bounded_flash_size(size) : 0u;
    if (flash_status != HAL_OK || s_size == 0u) {
      hal_mutex_unlock(s_eeprom_mutex);
      return flash_status != HAL_OK ? flash_status : HAL_EINVAL;
    }
  } else {
    s_size = 32768U;
    s_i2c_addr = (i2c_addr != 0) ? i2c_addr : EEPROM_I2C_ADDRESS;
  }
  hal_mutex_unlock(s_eeprom_mutex);
  return HAL_OK;
}

hal_status_t
hal_eeprom_set_progress_callback(hal_eeprom_progress_callback_t callback,
                                 void *ctx) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  s_progress_callback = callback;
  s_progress_ctx = ctx;
  hal_mutex_unlock(s_eeprom_mutex);
  return HAL_OK;
}

/* ── Lock-free helpers (caller holds s_eeprom_mutex) ──────────────────
 * The write/read helpers return I2C success so the AT24C256 path can surface
 * bus failures as HAL_EIO; the RP2040 flash path cannot fail here (writes are
 * buffered until commit) and always reports success. */

static bool write_clipped_nolock(uint16_t addr, const uint8_t *data,
                                 uint16_t n) {
  if (n == 0u) {
    return true;
  }
  if (s_type == HAL_EEPROM_RP2040) {
    if (!s_flash_ready) {
      return false;
    }
    memcpy(s_flash_mirror + addr, data, n);
    s_dirty = true;
    return true;
  }
  return at24_write_bytes(addr, data, n);
}

static bool read_clipped_nolock(uint16_t addr, uint8_t *out, uint16_t n) {
  if (n == 0u) {
    return true;
  }
  if (s_type == HAL_EEPROM_RP2040) {
    if (!s_flash_ready) {
      return false;
    }
    memcpy(out, s_flash_mirror + addr, n);
    return true;
  }
  return at24_read_bytes(addr, out, n);
}

static uint8_t read_byte_nolock(uint16_t addr) {
  if (addr >= s_size) {
    return 0u;
  }
  uint8_t value = 0u;
  (void)read_clipped_nolock(addr, &value, 1u);
  return value;
}

/* Combine a range status with an I2C-io result: a bus failure on an otherwise
 * in-range access reports HAL_EIO. */
static hal_status_t combine_io(hal_status_t range, bool io_ok) {
  if (range == HAL_EUNINIT) {
    return range;
  }
  if (!io_ok) {
    return HAL_EIO;
  }
  return range;
}

hal_status_t hal_eeprom_write_byte(uint16_t addr, uint8_t val) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const hal_status_t range = eeprom_range_status(addr, 1u);
  const bool io_ok = write_clipped_nolock(addr, &val, clipped_len(addr, 1u));
  hal_mutex_unlock(s_eeprom_mutex);
  return combine_io(range, io_ok);
}

uint8_t hal_eeprom_read_byte(uint16_t addr) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t val = read_byte_nolock(addr);
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

hal_status_t hal_eeprom_read_byte_ex(uint16_t addr, uint8_t *out_val) {
  if (out_val == NULL) {
    return HAL_EINVAL;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const hal_status_t range = eeprom_range_status(addr, 1u);
  bool io_ok = true;
  if (range == HAL_OK) {
    io_ok = read_clipped_nolock(addr, out_val, 1u);
  }
  hal_mutex_unlock(s_eeprom_mutex);
  return combine_io(range, io_ok);
}

hal_status_t hal_eeprom_write_int(uint16_t addr, int32_t val) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const hal_status_t range =
      eeprom_range_status(addr, (uint16_t)sizeof(int32_t));
  uint8_t raw[4] = {
      (uint8_t)((val >> 0) & 0xFF),
      (uint8_t)((val >> 8) & 0xFF),
      (uint8_t)((val >> 16) & 0xFF),
      (uint8_t)((val >> 24) & 0xFF),
  };
  const bool io_ok =
      write_clipped_nolock(addr, raw, clipped_len(addr, sizeof(raw)));
  hal_mutex_unlock(s_eeprom_mutex);
  return combine_io(range, io_ok);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  uint8_t raw[4] = {0u, 0u, 0u, 0u};
  (void)read_clipped_nolock(addr, raw, clipped_len(addr, sizeof(raw)));
  int32_t val = (int32_t)(((uint32_t)raw[0]) | ((uint32_t)raw[1] << 8) |
                          ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24));
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

hal_status_t hal_eeprom_read_int_ex(uint16_t addr, int32_t *out_val) {
  if (out_val == NULL) {
    return HAL_EINVAL;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const hal_status_t range =
      eeprom_range_status(addr, (uint16_t)sizeof(int32_t));
  bool io_ok = true;
  if (range == HAL_OK) {
    uint8_t raw[4] = {0u, 0u, 0u, 0u};
    io_ok = read_clipped_nolock(addr, raw, sizeof(raw));
    if (io_ok) {
      *out_val = (int32_t)(((uint32_t)raw[0]) | ((uint32_t)raw[1] << 8) |
                           ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24));
    }
  }
  hal_mutex_unlock(s_eeprom_mutex);
  return combine_io(range, io_ok);
}

hal_status_t hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data,
                                    uint16_t len) {
  if (!data) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const hal_status_t range = eeprom_range_status(addr, len);
  const bool io_ok = write_clipped_nolock(addr, data, clipped_len(addr, len));
  hal_mutex_unlock(s_eeprom_mutex);
  return combine_io(range, io_ok);
}

hal_status_t hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if (!out) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const hal_status_t range = eeprom_range_status(addr, len);
  const uint16_t n = clipped_len(addr, len);
  const bool io_ok = read_clipped_nolock(addr, out, n);
  for (uint16_t i = n; i < len; i++) {
    out[i] = 0u;
  }
  hal_mutex_unlock(s_eeprom_mutex);
  return combine_io(range, io_ok);
}

hal_status_t hal_eeprom_commit(void) {
  eeprom_ensure_mutex();
  hal_status_t status = HAL_OK;
  if (s_type == HAL_EEPROM_RP2040) {
    hal_mutex_lock(s_eeprom_mutex);
    status = flash_commit_mirror();
    hal_mutex_unlock(s_eeprom_mutex);
  }
  /* AT24C256: no-op - writes are already committed to the chip */
  return status;
}

hal_status_t hal_eeprom_reset(void) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  hal_status_t st = (s_size == 0u) ? HAL_EUNINIT : HAL_OK;
  if (s_type == HAL_EEPROM_RP2040) {
    if (s_flash_ready) {
      memset(s_flash_mirror, 0, s_size);
      s_dirty = true;
    } else {
      st = HAL_EUNINIT;
    }
  } else {
    uint8_t zeros[HAL_AT24C256_PAGE_SIZE] = {0u};
    for (uint32_t a = 0; a < s_size; a += HAL_AT24C256_PAGE_SIZE) {
      uint16_t chunk = (uint16_t)(s_size - a);
      if (chunk > HAL_AT24C256_PAGE_SIZE) {
        chunk = HAL_AT24C256_PAGE_SIZE;
      }
      if (!at24_write_bytes((uint16_t)a, zeros, chunk)) {
        st = HAL_EIO;
        break;
      }
    }
  }
  hal_mutex_unlock(s_eeprom_mutex);
  const hal_status_t cst = hal_eeprom_commit();
  if (st == HAL_OK) {
    st = cst;
  }
  return st;
}

uint16_t hal_eeprom_size(void) { return s_size; }

hal_status_t hal_eeprom_size_ex(uint16_t *out_size) {
  if (out_size == NULL) {
    return HAL_EINVAL;
  }
  const uint16_t size = s_size;
  *out_size = size;
  return size > 0u ? HAL_OK : HAL_EUNINIT;
}

#endif /* HAL_ENABLE_EEPROM */
#endif // HAL_TARGET_IS_RP
