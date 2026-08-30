#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_STM32G474

#include "../../port/stm32g474_regs.h"
#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "stm32g474_flash.h"

#include <string.h>

static constexpr uintptr_t STM32_FLASH_BASE_ADDR = 0x08000000u;
static constexpr uint32_t STM32_FLASH_BANK_SIZE = 256u * 1024u;
static constexpr uint32_t STM32_FLASH_TIMEOUT = 2000000u;

static hal_mutex_t s_flash_mutex = NULL;
static bool s_flash_transaction_active = false;

bool jh_stm32g474_flash_wait_ready(void) {
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

bool jh_stm32g474_flash_access_begin(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_flash_mutex);
  if (mutex == NULL) {
    return false;
  }
  hal_mutex_lock(mutex);
  return true;
}

void jh_stm32g474_flash_access_end(void) { hal_mutex_unlock(s_flash_mutex); }

bool jh_stm32g474_flash_unlock(void) {
  if (!jh_stm32g474_flash_access_begin()) {
    return false;
  }

  if ((FLASH_CR & FLASH_CR_LOCK) == 0u) {
    s_flash_transaction_active = true;
    return true;
  }

  FLASH_KEYR = FLASH_KEY1;
  FLASH_KEYR = FLASH_KEY2;
  if ((FLASH_CR & FLASH_CR_LOCK) != 0u) {
    jh_stm32g474_flash_access_end();
    return false;
  }
  s_flash_transaction_active = true;
  return true;
}

void jh_stm32g474_flash_lock(void) {
  FLASH_CR |= FLASH_CR_LOCK;
  if (s_flash_transaction_active) {
    s_flash_transaction_active = false;
    jh_stm32g474_flash_access_end();
  }
}

static uint32_t flash_page_number(uintptr_t address, bool *bank2) {
  uint32_t offset = (uint32_t)(address - STM32_FLASH_BASE_ADDR);
  *bank2 = offset >= STM32_FLASH_BANK_SIZE;
  if (*bank2) {
    offset -= STM32_FLASH_BANK_SIZE;
  }
  return offset / HAL_STM32_FLASH_PAGE_SIZE;
}

bool jh_stm32g474_flash_erase_page(uintptr_t address) {
  if (!jh_stm32g474_flash_wait_ready()) {
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
  const bool ok = jh_stm32g474_flash_wait_ready();
  FLASH_CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_MASK | FLASH_CR_BKER);
  return ok;
}

bool jh_stm32g474_flash_program_doubleword(uintptr_t address,
                                           const uint8_t *data) {
  if (!jh_stm32g474_flash_wait_ready()) {
    return false;
  }

  FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
  FLASH_CR |= FLASH_CR_PG;

  volatile uint32_t *dst =
      (volatile uint32_t *)address; // NOLINT(performance-no-int-to-ptr)
  uint32_t low = 0u;
  uint32_t high = 0u;
  memcpy(&low, data, sizeof(low));
  memcpy(&high, data + sizeof(low), sizeof(high));
  dst[0] = low;
  dst[1] = high;

  const bool ok = jh_stm32g474_flash_wait_ready();
  FLASH_CR &= ~FLASH_CR_PG;
  return ok;
}

#endif /* HAL_TARGET_IS_STM32G474 */
