#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../hal_config.h"
#ifdef HAL_ENABLE_EEPROM

#include "../../hal_eeprom.h"
#include "../../hal_i2c.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"
#include "../../hal_system.h"
#include "../shared/hal_mutex_once.h"
#include "drivers/stm32g474/stm32g474_flash.h"

#include <stdint.h>
#include <string.h>

extern "C" {
extern const uint8_t __hal_stm32_eeprom_flash_start[];
extern const uint8_t __hal_stm32_eeprom_flash_end[];
}

static constexpr uint16_t AT24C256_SIZE = 32768u;

static uint8_t s_flash_mirror[HAL_STM32_FLASH_EEPROM_SIZE];
static hal_eeprom_type_t s_type = HAL_EEPROM_STM32_FLASH;
static uint16_t s_size = 0u;
static uint8_t s_i2c_addr = EEPROM_I2C_ADDRESS;
static uintptr_t s_flash_start = 0u;
static uint32_t s_flash_reserved_size = 0u;
static bool s_dirty = false;
static hal_mutex_t s_eeprom_mutex = NULL;
static hal_eeprom_progress_callback_t s_progress_callback = NULL;
static void *s_progress_ctx = NULL;

static void eeprom_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_eeprom_mutex);
}

static bool eeprom_is_flash_type(hal_eeprom_type_t type) {
  return type == HAL_EEPROM_DEFAULT || type == HAL_EEPROM_RP2040 ||
         type == HAL_EEPROM_STM32_FLASH || type == HAL_EEPROM_FLASH;
}

static hal_eeprom_type_t normalize_type(hal_eeprom_type_t type) {
  return eeprom_is_flash_type(type) ? HAL_EEPROM_STM32_FLASH : type;
}

static uint16_t bounded_size(uint16_t requested) {
  uint32_t max_size = s_flash_reserved_size;
  if (max_size > sizeof(s_flash_mirror)) {
    max_size = sizeof(s_flash_mirror);
  }
  if (max_size > 0xFFFFu) {
    max_size = 0xFFFFu;
  }

  if (requested == 0u || requested > max_size) {
    return (uint16_t)max_size;
  }
  return requested;
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

#endif /* HAL_ENABLE_I2C */

static bool flash_commit_mirror(void) {
  if (s_flash_reserved_size == 0u ||
      s_flash_reserved_size > sizeof(s_flash_mirror)) {
    return false;
  }
  if (!jh_stm32g474_flash_unlock()) {
    return false;
  }

  bool ok = true;
  for (uint32_t off = 0u; off < s_flash_reserved_size;
       off += HAL_STM32_FLASH_PAGE_SIZE) {
    if (!jh_stm32g474_flash_erase_page(s_flash_start + off)) {
      ok = false;
      break;
    }
    notify_progress();
  }

  for (uint32_t off = 0u; ok && off < s_flash_reserved_size; off += 8u) {
    bool all_erased = true;
    for (uint8_t i = 0u; i < 8u; ++i) {
      if (s_flash_mirror[off + i] != 0xFFu) {
        all_erased = false;
        break;
      }
    }
    if (all_erased) {
      continue;
    }
    if (!jh_stm32g474_flash_program_doubleword(s_flash_start + off,
                                               &s_flash_mirror[off])) {
      ok = false;
      break;
    }
    notify_progress();
  }

  jh_stm32g474_flash_lock();
  return ok;
}

static void flash_load_mirror(void) {
  s_flash_start = (uintptr_t)&__hal_stm32_eeprom_flash_start[0];
  const uintptr_t flash_end = (uintptr_t)&__hal_stm32_eeprom_flash_end[0];
  s_flash_reserved_size =
      (flash_end > s_flash_start) ? (uint32_t)(flash_end - s_flash_start) : 0u;
  if (s_flash_reserved_size > sizeof(s_flash_mirror)) {
    s_flash_reserved_size = sizeof(s_flash_mirror);
  }

  if (s_flash_reserved_size > 0u) {
    memcpy(s_flash_mirror, (const void *)s_flash_start, s_flash_reserved_size);
  }
  if (s_flash_reserved_size < sizeof(s_flash_mirror)) {
    memset(s_flash_mirror + s_flash_reserved_size, 0xFF,
           sizeof(s_flash_mirror) - s_flash_reserved_size);
  }
}

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

hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr) {
  if (type < HAL_EEPROM_DEFAULT || type > HAL_EEPROM_FLASH) {
    return HAL_EINVAL;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);

  s_type = normalize_type(type);
  s_dirty = false;

  if (s_type == HAL_EEPROM_STM32_FLASH) {
    flash_load_mirror();
    s_size = bounded_size(size);
  } else {
    s_size = AT24C256_SIZE;
    s_i2c_addr = (i2c_addr != 0u) ? i2c_addr : EEPROM_I2C_ADDRESS;
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

static void write_byte_nolock(uint16_t addr, uint8_t val) {
  if (addr < s_size && s_flash_reserved_size > 0u) {
    s_flash_mirror[addr] = val;
    s_dirty = true;
  }
}

static uint8_t read_byte_nolock(uint16_t addr) {
  if (addr >= s_size) {
    return 0u;
  }
  if (s_type == HAL_EEPROM_STM32_FLASH) {
    return (addr < s_size && s_flash_reserved_size > 0u) ? s_flash_mirror[addr]
                                                         : 0u;
  }

  uint8_t value = 0u;
  (void)at24_read_bytes(addr, &value, 1u);
  return value;
}

/* Clipped write/read returning I2C success so the AT24C256 path can surface
 * bus failures as HAL_EIO; the STM32 flash mirror path cannot fail here. */
static bool write_clipped_nolock(uint16_t addr, const uint8_t *data,
                                 uint16_t n) {
  if (n == 0u) {
    return true;
  }
  if (s_type == HAL_EEPROM_STM32_FLASH) {
    for (uint16_t i = 0u; i < n; i++) {
      write_byte_nolock((uint16_t)(addr + i), data[i]);
    }
    return true;
  }
  return at24_write_bytes(addr, data, n);
}

static bool read_clipped_nolock(uint16_t addr, uint8_t *out, uint16_t n) {
  if (n == 0u) {
    return true;
  }
  if (s_type == HAL_EEPROM_STM32_FLASH) {
    for (uint16_t i = 0u; i < n; i++) {
      out[i] = read_byte_nolock((uint16_t)(addr + i));
    }
    return true;
  }
  return at24_read_bytes(addr, out, n);
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
  const uint8_t val = read_byte_nolock(addr);
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
  const int32_t val =
      (int32_t)(((uint32_t)raw[0]) | ((uint32_t)raw[1] << 8) |
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
  if (data == NULL) {
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
  if (out == NULL) {
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
  hal_mutex_lock(s_eeprom_mutex);

  hal_status_t st = HAL_OK;
  if (s_type == HAL_EEPROM_STM32_FLASH && s_dirty) {
    if (flash_commit_mirror()) {
      s_dirty = false;
    } else {
      hal_derr("hal_eeprom_commit: STM32 flash commit failed");
      st = HAL_EIO;
    }
  }

  hal_mutex_unlock(s_eeprom_mutex);
  return st;
}

hal_status_t hal_eeprom_reset(void) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);

  hal_status_t st = (s_size == 0u) ? HAL_EUNINIT : HAL_OK;
  if (s_type == HAL_EEPROM_STM32_FLASH) {
    memset(s_flash_mirror, 0, s_flash_reserved_size);
    s_dirty = true;
  } else {
    uint8_t zeros[HAL_AT24C256_PAGE_SIZE] = {0u};
    for (uint32_t a = 0u; a < s_size; a += HAL_AT24C256_PAGE_SIZE) {
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
#endif /* HAL_TARGET_IS_STM32G474 */
