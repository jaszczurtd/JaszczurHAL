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
#include "port/stm32g474_regs.h"

#include <stdint.h>
#include <string.h>

extern "C" {
extern const uint8_t __hal_stm32_eeprom_flash_start[];
extern const uint8_t __hal_stm32_eeprom_flash_end[];
}

static constexpr uintptr_t STM32_FLASH_BASE_ADDR = 0x08000000u;
static constexpr uint32_t STM32_FLASH_BANK_SIZE = 256u * 1024u;
static constexpr uint32_t STM32_FLASH_TIMEOUT = 2000000u;
static constexpr uint16_t AT24C256_SIZE = 32768u;

static uint8_t s_flash_mirror[HAL_STM32_FLASH_EEPROM_SIZE];
static hal_eeprom_type_t s_type = HAL_EEPROM_STM32_FLASH;
static uint16_t s_size = 0u;
static uint8_t s_i2c_addr = EEPROM_I2C_ADDRESS;
static uintptr_t s_flash_start = 0u;
static uint32_t s_flash_reserved_size = 0u;
static bool s_dirty = false;
static hal_mutex_t s_eeprom_mutex = NULL;

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

/* ── AT24C256 helpers ───────────────────────────────────────────────────── */

static void at24_write_byte(uint16_t addr, uint8_t val) {
  hal_i2c_begin_transmission(s_i2c_addr);
  (void)hal_i2c_write((uint8_t)(addr >> 8));
  (void)hal_i2c_write((uint8_t)(addr & 0xFFu));
  (void)hal_i2c_write(val);
  (void)hal_i2c_end_transmission();

  while (hal_i2c_is_busy(s_i2c_addr)) {
    hal_delay_us(100u);
    hal_watchdog_feed();
  }
  hal_delay_ms(5u);
}

static uint8_t at24_read_byte(uint16_t addr) {
  hal_i2c_begin_transmission(s_i2c_addr);
  (void)hal_i2c_write((uint8_t)(addr >> 8));
  (void)hal_i2c_write((uint8_t)(addr & 0xFFu));
  (void)hal_i2c_end_transmission();

  (void)hal_i2c_request_from(s_i2c_addr, (uint8_t)1u);
  if (hal_i2c_available()) {
    return (uint8_t)hal_i2c_read();
  }
  return 0u;
}

/* ── STM32 flash helpers ────────────────────────────────────────────────── */

static bool flash_wait_ready(void) {
  uint32_t timeout = STM32_FLASH_TIMEOUT;
  while ((FLASH_SR & FLASH_SR_BSY) != 0u) {
    if (timeout-- == 0u) {
      return false;
    }
  }

  const uint32_t sr = FLASH_SR;
  if ((sr & FLASH_SR_ERRORS) != 0u) {
    FLASH_SR = FLASH_SR_ERRORS;
    return false;
  }
  if ((sr & FLASH_SR_EOP) != 0u) {
    FLASH_SR = FLASH_SR_EOP;
  }
  return true;
}

static bool flash_unlock(void) {
  if ((FLASH_CR & FLASH_CR_LOCK) == 0u) {
    return true;
  }

  FLASH_KEYR = FLASH_KEY1;
  FLASH_KEYR = FLASH_KEY2;
  return (FLASH_CR & FLASH_CR_LOCK) == 0u;
}

static void flash_lock(void) { FLASH_CR |= FLASH_CR_LOCK; }

static uint32_t flash_page_number(uintptr_t address, bool *bank2) {
  uint32_t offset = (uint32_t)(address - STM32_FLASH_BASE_ADDR);
  *bank2 = offset >= STM32_FLASH_BANK_SIZE;
  if (*bank2) {
    offset -= STM32_FLASH_BANK_SIZE;
  }
  return offset / HAL_STM32_FLASH_PAGE_SIZE;
}

static bool flash_erase_page(uintptr_t address) {
  if (!flash_wait_ready()) {
    return false;
  }

  FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP;

  bool bank2 = false;
  const uint32_t page = flash_page_number(address, &bank2);
  uint32_t cr = FLASH_CR;
  cr &= ~(FLASH_CR_PNB_MASK | FLASH_CR_BKER | FLASH_CR_PG);
  cr |= FLASH_CR_PER | ((page << FLASH_CR_PNB_POS) & FLASH_CR_PNB_MASK);
  if (bank2) {
    cr |= FLASH_CR_BKER;
  }

  FLASH_CR = cr;
  FLASH_CR |= FLASH_CR_STRT;
  const bool ok = flash_wait_ready();
  FLASH_CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_MASK | FLASH_CR_BKER);
  return ok;
}

static bool flash_program_doubleword(uintptr_t address, const uint8_t *data) {
  if (!flash_wait_ready()) {
    return false;
  }

  FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
  FLASH_CR |= FLASH_CR_PG;

  volatile uint32_t *dst = (volatile uint32_t *)address;
  uint32_t low = 0u;
  uint32_t high = 0u;
  memcpy(&low, data, sizeof(low));
  memcpy(&high, data + sizeof(low), sizeof(high));
  dst[0] = low;
  dst[1] = high;

  const bool ok = flash_wait_ready();
  FLASH_CR &= ~FLASH_CR_PG;
  return ok;
}

static bool flash_commit_mirror(void) {
  if (s_flash_reserved_size == 0u ||
      s_flash_reserved_size > sizeof(s_flash_mirror)) {
    return false;
  }
  if (!flash_unlock()) {
    return false;
  }

  bool ok = true;
  for (uint32_t off = 0u; off < s_flash_reserved_size;
       off += HAL_STM32_FLASH_PAGE_SIZE) {
    if (!flash_erase_page(s_flash_start + off)) {
      ok = false;
      break;
    }
    hal_watchdog_feed();
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
    if (!flash_program_doubleword(s_flash_start + off, &s_flash_mirror[off])) {
      ok = false;
      break;
    }
    hal_watchdog_feed();
  }

  flash_lock();
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

void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr) {
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
}

static void write_byte_nolock(uint16_t addr, uint8_t val) {
  if (s_type == HAL_EEPROM_STM32_FLASH) {
    if (addr < s_size && s_flash_reserved_size > 0u) {
      s_flash_mirror[addr] = val;
      s_dirty = true;
    }
    return;
  }

  at24_write_byte(addr, val);
}

static uint8_t read_byte_nolock(uint16_t addr) {
  if (s_type == HAL_EEPROM_STM32_FLASH) {
    return (addr < s_size && s_flash_reserved_size > 0u) ? s_flash_mirror[addr]
                                                         : 0u;
  }

  return at24_read_byte(addr);
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
  const uint8_t val = read_byte_nolock(addr);
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

void hal_eeprom_write_int(uint16_t addr, int32_t val) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  write_byte_nolock((uint16_t)(addr + 0u), (uint8_t)((val >> 0) & 0xFF));
  write_byte_nolock((uint16_t)(addr + 1u), (uint8_t)((val >> 8) & 0xFF));
  write_byte_nolock((uint16_t)(addr + 2u), (uint8_t)((val >> 16) & 0xFF));
  write_byte_nolock((uint16_t)(addr + 3u), (uint8_t)((val >> 24) & 0xFF));
  hal_mutex_unlock(s_eeprom_mutex);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);
  const int32_t val =
      (int32_t)(((uint32_t)read_byte_nolock((uint16_t)(addr + 0u))) |
                ((uint32_t)read_byte_nolock((uint16_t)(addr + 1u)) << 8) |
                ((uint32_t)read_byte_nolock((uint16_t)(addr + 2u)) << 16) |
                ((uint32_t)read_byte_nolock((uint16_t)(addr + 3u)) << 24));
  hal_mutex_unlock(s_eeprom_mutex);
  return val;
}

void hal_eeprom_write_bytes(uint16_t addr, const uint8_t *data, uint16_t len) {
  if (data == NULL || len == 0u) {
    return;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);

  const uint16_t n =
      (s_type == HAL_EEPROM_STM32_FLASH) ? clipped_len(addr, len) : len;
  for (uint16_t i = 0u; i < n; i++) {
    write_byte_nolock((uint16_t)(addr + i), data[i]);
  }

  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_read_bytes(uint16_t addr, uint8_t *out, uint16_t len) {
  if (out == NULL || len == 0u) {
    return;
  }
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);

  const uint16_t n =
      (s_type == HAL_EEPROM_STM32_FLASH) ? clipped_len(addr, len) : len;
  for (uint16_t i = 0u; i < n; i++) {
    out[i] = read_byte_nolock((uint16_t)(addr + i));
  }
  for (uint16_t i = n; i < len; i++) {
    out[i] = 0u;
  }

  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_commit(void) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);

  if (s_type == HAL_EEPROM_STM32_FLASH && s_dirty) {
    if (flash_commit_mirror()) {
      s_dirty = false;
    } else {
      hal_derr("hal_eeprom_commit: STM32 flash commit failed");
    }
  }

  hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_reset(void) {
  eeprom_ensure_mutex();
  hal_mutex_lock(s_eeprom_mutex);

  if (s_type == HAL_EEPROM_STM32_FLASH) {
    memset(s_flash_mirror, 0, s_flash_reserved_size);
    s_dirty = true;
  } else {
    for (uint32_t a = 0u; a < s_size; a++) {
      at24_write_byte((uint16_t)a, 0u);
      hal_watchdog_feed();
    }
  }

  hal_mutex_unlock(s_eeprom_mutex);
  hal_eeprom_commit();
}

uint16_t hal_eeprom_size(void) { return s_size; }

#endif /* HAL_ENABLE_EEPROM */
#endif /* HAL_TARGET_IS_STM32G474 */
