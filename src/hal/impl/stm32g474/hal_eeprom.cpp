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

#ifdef JH_STM32G474_HW
/* The linker script compares this with the region it reserved, so a size the
 * compiler sees but the linker does not (set only in hal_project_config.h)
 * fails the link instead of leaving storage at the linker's 4 KB. */
extern "C" void jh_stm32_eeprom_size_marker(void) {
  __asm volatile(".globl __hal_stm32_eeprom_c_size\n"
                 ".set __hal_stm32_eeprom_c_size, %c0\n" ::"i"(
                     HAL_STM32_FLASH_EEPROM_SIZE));
}
#endif

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

bool program_range(uintptr_t address, const uint8_t *data, uint32_t size,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  for (uint32_t offset = 0u; offset < size; offset += 8u) {
    bool erased = true;
    for (uint8_t index = 0u; index < 8u; ++index) {
      if (data[offset + index] != 0xffu) {
        erased = false;
        break;
      }
    }
    if (!erased) {
      if (!jh_stm32g474_flash_unlock()) {
        return false;
      }
      const bool programmed = jh_stm32g474_flash_program_doubleword(
          address + offset, &data[offset]);
      jh_stm32g474_flash_lock();
      if (!programmed) {
        return false;
      }
    }
    notify(progress, ctx);
  }
  return true;
}

bool program_verified(uintptr_t address, const uint8_t *data, uint32_t size,
                      hal_eeprom_progress_callback_t progress, void *ctx) {
  return program_range(address, data, size, progress, ctx) &&
         memcmp(reinterpret_cast<const void *>(address), data, size) == 0;
}

bool erase_pages(uintptr_t address, uint32_t len,
                 hal_eeprom_progress_callback_t progress, void *ctx) {
  for (uint32_t offset = 0u; offset < len;
       offset += HAL_STM32_FLASH_PAGE_SIZE) {
    if (!jh_stm32g474_flash_unlock()) {
      return false;
    }
    const bool erased = jh_stm32g474_flash_erase_page(address + offset);
    jh_stm32g474_flash_lock();
    if (!erased) {
      return false;
    }
    notify(progress, ctx);
  }
  return true;
}

hal_status_t store(void *context, const uint8_t *mirror, uint16_t storage_size,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)context;
  if (storage_size != s_reserved_size) {
    return HAL_EINVAL;
  }
  const bool ok =
      erase_pages(s_flash_start, s_reserved_size, progress, ctx) &&
      program_range(s_flash_start, mirror, s_reserved_size, progress, ctx);
  if (!ok) {
    hal_derr("hal_eeprom_commit: STM32 flash commit failed");
  }
  return ok ? HAL_OK : HAL_EIO;
}

hal_status_t replace_region(void *context, uint16_t addr, const uint8_t *data,
                            uint16_t len, uint16_t publish_size,
                            bool destination_erased,
                            hal_eeprom_progress_callback_t progress,
                            void *ctx) {
  (void)context;
  if (data == nullptr || publish_size == 0u || publish_size >= len ||
      (addr % HAL_STM32_FLASH_PAGE_SIZE) != 0u ||
      (len % HAL_STM32_FLASH_PAGE_SIZE) != 0u || (publish_size % 8u) != 0u ||
      (uint32_t)addr + len > s_reserved_size) {
    return HAL_EINVAL;
  }

  const uintptr_t address = s_flash_start + addr;
  if (!destination_erased && !erase_pages(address, len, progress, ctx)) {
    return HAL_EIO;
  }

  const uint32_t body_size = (uint32_t)len - publish_size;
  if (!program_verified(address + publish_size, data + publish_size, body_size,
                        progress, ctx)) {
    return HAL_EIO;
  }
  if (!program_range(address, data, publish_size, progress, ctx) ||
      memcmp(reinterpret_cast<const void *>(address), data, len) != 0) {
    return HAL_EIO;
  }
  return HAL_OK;
}

hal_status_t program(void *context, uint16_t addr, const uint8_t *data,
                     uint16_t len, hal_eeprom_progress_callback_t progress,
                     void *ctx) {
  (void)context;
  if (data == nullptr || (addr % 8u) != 0u || (len % 8u) != 0u ||
      (uint32_t)addr + len > s_reserved_size) {
    return HAL_EINVAL;
  }
  return program_verified(s_flash_start + addr, data, len, progress, ctx)
             ? HAL_OK
             : HAL_EIO;
}

hal_status_t erase(void *context, uint16_t addr, uint16_t len,
                   hal_eeprom_progress_callback_t progress, void *ctx) {
  (void)context;
  if ((addr % HAL_STM32_FLASH_PAGE_SIZE) != 0u ||
      (len % HAL_STM32_FLASH_PAGE_SIZE) != 0u ||
      (uint32_t)addr + len > s_reserved_size) {
    return HAL_EINVAL;
  }
  return erase_pages(s_flash_start + addr, len, progress, ctx) ? HAL_OK
                                                               : HAL_EIO;
}

hal_status_t read(void *context, uint16_t addr, uint8_t *out, uint16_t len) {
  (void)context;
  if (out == nullptr || (uint32_t)addr + len > s_reserved_size) {
    return HAL_EINVAL;
  }
  memcpy(out, reinterpret_cast<const void *>(s_flash_start + addr), len);
  return HAL_OK;
}

/* Each double word carries ECC and is programmed once per erase, so an append
 * starts and ends on a double word. */
const jh_eeprom_flash_backend_t kFlashBackend = {HAL_EEPROM_STM32_FLASH,
                                                 s_mirror,
                                                 sizeof(s_mirror),
                                                 true,
                                                 true,
                                                 nullptr,
                                                 load,
                                                 store,
                                                 replace_region,
                                                 program,
                                                 erase,
                                                 read,
                                                 HAL_STM32_FLASH_PAGE_SIZE,
                                                 8u,
                                                 8u};

} // namespace

const jh_eeprom_provider_ops_t *
jh_eeprom_provider_get_ops(hal_eeprom_type_t type) {
  return jh_eeprom_hardware_provider_get_ops(type, &kFlashBackend);
}

#endif /* HAL_ENABLE_EEPROM */
#endif /* HAL_TARGET_IS_STM32G474 */
