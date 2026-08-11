#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/impl/stm32g474/drivers/stm32g474/stm32g474_flash.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/jh_eeprom_provider.h"

#include <string.h>

extern "C" {
extern const uint8_t __hal_stm32_eeprom_flash_start[];
extern const uint8_t __hal_stm32_eeprom_flash_end[];
}

namespace {

uint8_t s_mirror[HAL_STM32_FLASH_EEPROM_SIZE] = {};
uintptr_t s_flash_start = 0u;
uint32_t s_reserved_size = 0u;

hal_status_t load(void *context, uint8_t *mirror, uint16_t mirror_capacity,
                  uint16_t *out_storage_size) {
  (void)context;
  if (out_storage_size == nullptr) {
    return HAL_EINVAL;
  }
  s_flash_start =
      reinterpret_cast<uintptr_t>(&__hal_stm32_eeprom_flash_start[0]);
  const uintptr_t flash_end =
      reinterpret_cast<uintptr_t>(&__hal_stm32_eeprom_flash_end[0]);
  s_reserved_size = flash_end > s_flash_start
                        ? static_cast<uint32_t>(flash_end - s_flash_start)
                        : 0u;
  if (s_reserved_size > mirror_capacity) {
    s_reserved_size = mirror_capacity;
  }
  if (s_reserved_size == 0u || s_reserved_size > UINT16_MAX) {
    return HAL_ECONFIG;
  }
  memcpy(mirror, reinterpret_cast<const void *>(s_flash_start),
         s_reserved_size);
  if (s_reserved_size < mirror_capacity) {
    memset(mirror + s_reserved_size, 0xff, mirror_capacity - s_reserved_size);
  }
  *out_storage_size = static_cast<uint16_t>(s_reserved_size);
  return HAL_OK;
}

void notify(hal_eeprom_progress_callback_t progress, void *ctx) {
  if (progress != nullptr) {
    progress(ctx);
  }
}

hal_status_t store(void *context, const uint8_t *mirror, uint16_t storage_size,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)context;
  if (storage_size != s_reserved_size || !jh_stm32g474_flash_unlock()) {
    return HAL_EIO;
  }
  bool ok = true;
  for (uint32_t offset = 0u; offset < s_reserved_size;
       offset += HAL_STM32_FLASH_PAGE_SIZE) {
    if (!jh_stm32g474_flash_erase_page(s_flash_start + offset)) {
      ok = false;
      break;
    }
    notify(progress, ctx);
  }
  for (uint32_t offset = 0u; ok && offset < s_reserved_size; offset += 8u) {
    bool erased = true;
    for (uint8_t i = 0u; i < 8u; ++i) {
      if (mirror[offset + i] != 0xffu) {
        erased = false;
        break;
      }
    }
    if (!erased && !jh_stm32g474_flash_program_doubleword(
                       s_flash_start + offset, &mirror[offset])) {
      ok = false;
    }
    notify(progress, ctx);
  }
  jh_stm32g474_flash_lock();
  if (!ok) {
    hal_derr("hal_eeprom_commit: STM32 flash commit failed");
  }
  return ok ? HAL_OK : HAL_EIO;
}

const jh_eeprom_flash_backend_t kFlashBackend = {HAL_EEPROM_STM32_FLASH,
                                                 s_mirror,
                                                 sizeof(s_mirror),
                                                 true,
                                                 true,
                                                 nullptr,
                                                 load,
                                                 store};

} // namespace

const jh_eeprom_provider_ops_t *
jh_eeprom_provider_get_ops(hal_eeprom_type_t type) {
  return jh_eeprom_hardware_provider_get_ops(type, &kFlashBackend);
}

#endif /* HAL_ENABLE_EEPROM */
#endif /* HAL_TARGET_IS_STM32G474 */
