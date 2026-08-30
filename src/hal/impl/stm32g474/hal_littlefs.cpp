#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_STM32G474

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_LITTLEFS

#include "drivers/stm32g474/stm32g474_flash.h"
#include "hal/storage/jh_littlefs_provider.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

extern "C" {
extern const uint8_t __hal_stm32_littlefs_flash_start[];
extern const uint8_t __hal_stm32_littlefs_flash_end[];
}

namespace {

constexpr uint32_t kReadSize = 8u;
constexpr uint32_t kProgSize = 8u;
constexpr uint32_t kCacheSize = 64u;
constexpr uint32_t kLookaheadSize = 16u;
constexpr int32_t kBlockCycles = 500;

uintptr_t s_flash_start = 0u;
uint32_t s_flash_size = 0u;
uint8_t s_read_buffer[kCacheSize];
uint8_t s_prog_buffer[kCacheSize];
uint8_t s_lookahead_buffer[kLookaheadSize];

bool address_for_offset(size_t offset, uintptr_t *out_address) {
  if (out_address == nullptr || offset > s_flash_size ||
      offset > UINTPTR_MAX - s_flash_start) {
    return false;
  }
  *out_address = s_flash_start + offset;
  return true;
}

hal_status_t prepare(void *context, jh_littlefs_geometry_t *out_geometry) {
  (void)context;
  if (out_geometry == nullptr) {
    return HAL_EINVAL;
  }

  s_flash_start =
      reinterpret_cast<uintptr_t>(&__hal_stm32_littlefs_flash_start[0]);
  const uintptr_t flash_end =
      reinterpret_cast<uintptr_t>(&__hal_stm32_littlefs_flash_end[0]);
  s_flash_size = flash_end > s_flash_start
                     ? static_cast<uint32_t>(flash_end - s_flash_start)
                     : 0u;

  *out_geometry = {kReadSize,
                   kProgSize,
                   HAL_STM32_FLASH_PAGE_SIZE,
                   s_flash_size / HAL_STM32_FLASH_PAGE_SIZE,
                   kBlockCycles,
                   kCacheSize,
                   kLookaheadSize,
                   s_read_buffer,
                   s_prog_buffer,
                   s_lookahead_buffer};
  return out_geometry->block_count > 0u ? HAL_OK : HAL_ECONFIG;
}

hal_status_t read(void *context, size_t offset, void *buffer, size_t size) {
  (void)context;
  if (buffer == nullptr) {
    return HAL_EINVAL;
  }
  uintptr_t address = 0u;
  if (!address_for_offset(offset, &address) || size > s_flash_size - offset) {
    return HAL_EOVERFLOW;
  }
  if (!jh_stm32g474_flash_access_begin()) {
    return HAL_EIO;
  }
  memcpy(buffer, reinterpret_cast<const void *>(address), size);
  jh_stm32g474_flash_access_end();
  return HAL_OK;
}

hal_status_t program(void *context, size_t offset, const void *buffer,
                     size_t size, hal_littlefs_progress_callback_t progress,
                     void *progress_ctx) {
  (void)context;
  uintptr_t address = 0u;
  if (buffer == nullptr || (offset % kProgSize) != 0u ||
      (size % kProgSize) != 0u || !address_for_offset(offset, &address) ||
      size > s_flash_size - offset) {
    return HAL_EINVAL;
  }
  if (!jh_stm32g474_flash_unlock()) {
    return HAL_EIO;
  }

  const uint8_t *source = static_cast<const uint8_t *>(buffer);
  bool ok = true;
  size_t completed = 0u;
  for (; completed < size; completed += kProgSize) {
    if (!jh_stm32g474_flash_program_doubleword(address + completed,
                                               source + completed)) {
      ok = false;
      break;
    }
  }
  jh_stm32g474_flash_lock();

  if (progress != nullptr) {
    for (size_t notified = 0u; notified < completed; notified += kProgSize) {
      progress(progress_ctx);
    }
  }
  return ok ? HAL_OK : HAL_EIO;
}

hal_status_t erase(void *context, size_t offset, size_t size,
                   hal_littlefs_progress_callback_t progress,
                   void *progress_ctx) {
  (void)context;
  uintptr_t address = 0u;
  if (size != HAL_STM32_FLASH_PAGE_SIZE ||
      (offset % HAL_STM32_FLASH_PAGE_SIZE) != 0u ||
      !address_for_offset(offset, &address) || size > s_flash_size - offset) {
    return HAL_EINVAL;
  }
  if (!jh_stm32g474_flash_unlock()) {
    return HAL_EIO;
  }
  const bool ok = jh_stm32g474_flash_erase_page(address);
  jh_stm32g474_flash_lock();
  if (progress != nullptr) {
    progress(progress_ctx);
  }
  return ok ? HAL_OK : HAL_EIO;
}

hal_status_t sync(void *context) {
  (void)context;
  if (!jh_stm32g474_flash_access_begin()) {
    return HAL_EIO;
  }
  const bool ready = jh_stm32g474_flash_wait_ready();
  jh_stm32g474_flash_access_end();
  return ready ? HAL_OK : HAL_EIO;
}

const jh_littlefs_block_backend_t kBackend = {nullptr, prepare, read,
                                              program, erase,   sync};

} // namespace

const jh_littlefs_provider_t *jh_littlefs_provider_get(void) {
  return jh_littlefs_lfs_provider_configure(&kBackend);
}

#endif /* HAL_ENABLE_LITTLEFS */
#endif /* HAL_TARGET_IS_STM32G474 */
