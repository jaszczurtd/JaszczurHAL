#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include "hal/core/hal_config.h"
#include "rp_flash_storage.h"
#include "rp_flash_transaction.h"

#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <pico/platform.h>

#include <string.h>

namespace {

#ifdef JH_RP_FLASH_FAULT_INJECTION
jh_rp_flash_fail_phase_t s_fail_phase = JH_RP_FLASH_FAIL_NONE;
#endif

enum class FlashAction : uint8_t {
  Program,
  Erase,
  Replace,
  ReplacePublished,
  ProgramBytes,
};

struct FlashOperation {
  FlashAction action;
  uint32_t flash_offset;
  const uint8_t *data;
  size_t size;
  size_t publish_size;
  bool erased;
};

/* Page image for ProgramBytes; RAM-resident like the code that fills it. */
uint8_t s_page[FLASH_PAGE_SIZE];

/* Called with XIP off, so it lives in RAM like its callers. */
bool __no_inline_not_in_flash_func(fail_at)(jh_rp_flash_fail_phase_t phase) {
#ifdef JH_RP_FLASH_FAULT_INJECTION
  return s_fail_phase == phase;
#else
  (void)phase;
  return false;
#endif
}

bool range_valid(const jh_rp_flash_partition_t *partition, uint32_t offset,
                 size_t size) {
  return partition != nullptr && partition->size > 0u &&
         offset <= partition->size && size <= partition->size - offset;
}

bool __no_inline_not_in_flash_func(stored_matches)(uint32_t flash_offset,
                                                   const uint8_t *expected,
                                                   size_t size) {
  const volatile uint8_t *stored = reinterpret_cast<const volatile uint8_t *>(
      (uintptr_t)XIP_BASE + flash_offset);
  for (size_t index = 0u; index < size; index++) {
    if (stored[index] != expected[index]) {
      return false;
    }
  }
  return true;
}

/* Program [offset, offset + size) page by page. Bytes of a page outside the
 * range are sent as 0xFF, which leaves them as they are; so is a page whose
 * range bytes are all 0xFF, which is skipped. */
void __no_inline_not_in_flash_func(program_bytes)(uint32_t flash_offset,
                                                  const uint8_t *data,
                                                  size_t size, size_t written) {
  const uint32_t end = flash_offset + (uint32_t)size;
  const uint32_t torn = flash_offset + (uint32_t)written;
  for (uint32_t page = flash_offset & ~(uint32_t)(FLASH_PAGE_SIZE - 1u);
       page < end; page += FLASH_PAGE_SIZE) {
    bool blank = true;
    for (uint32_t index = 0u; index < FLASH_PAGE_SIZE; index++) {
      const uint32_t address = page + index;
      const uint8_t value = address >= flash_offset && address < torn
                                ? data[address - flash_offset]
                                : 0xFFu;
      s_page[index] = value;
      blank = blank && value == 0xFFu;
    }
    if (!blank) {
      flash_range_program(page, s_page, FLASH_PAGE_SIZE);
    }
  }
}

hal_status_t
__no_inline_not_in_flash_func(run_flash_operation)(void *raw_context) {
  auto *operation = static_cast<FlashOperation *>(raw_context);
  if (operation->action == FlashAction::ProgramBytes) {
    const bool torn = fail_at(JH_RP_FLASH_FAIL_APPEND_TORN);
    program_bytes(operation->flash_offset, operation->data, operation->size,
                  torn ? operation->size / 2u : operation->size);
    if (torn || fail_at(JH_RP_FLASH_FAIL_APPEND_AFTER_PROGRAM)) {
      return HAL_EIO;
    }
    return stored_matches(operation->flash_offset, operation->data,
                          operation->size)
               ? HAL_OK
               : HAL_EIO;
  }
  if (operation->action == FlashAction::Erase ||
      operation->action == FlashAction::Replace ||
      (operation->action == FlashAction::ReplacePublished &&
       !operation->erased)) {
    flash_range_erase(operation->flash_offset, operation->size);
  }
  if (operation->action == FlashAction::Program ||
      operation->action == FlashAction::Replace) {
    flash_range_program(operation->flash_offset, operation->data,
                        operation->size);
  } else if (operation->action == FlashAction::ReplacePublished) {
    if (fail_at(JH_RP_FLASH_FAIL_AFTER_INVALIDATE)) {
      return HAL_EIO;
    }
    const size_t body = operation->size - operation->publish_size;
    program_bytes(operation->flash_offset + operation->publish_size,
                  operation->data + operation->publish_size, body, body);
    if (fail_at(JH_RP_FLASH_FAIL_AFTER_BODY)) {
      return HAL_EIO;
    }
    if (!stored_matches(operation->flash_offset + operation->publish_size,
                        operation->data + operation->publish_size, body)) {
      return HAL_EIO;
    }
    if (fail_at(JH_RP_FLASH_FAIL_AFTER_VERIFY)) {
      return HAL_EIO;
    }
    flash_range_program(operation->flash_offset, operation->data,
                        operation->publish_size);
    if (fail_at(JH_RP_FLASH_FAIL_AFTER_PUBLISH)) {
      return HAL_EIO;
    }
  }
  return HAL_OK;
}

hal_status_t execute(FlashAction action, uint32_t flash_offset,
                     const void *data, size_t size) {
  FlashOperation operation = {
      action, flash_offset, static_cast<const uint8_t *>(data),
      size,   0u,           false};
  return jh_rp_flash_transaction_execute(run_flash_operation, &operation,
                                         HAL_RP_FLASH_TRANSACTION_TIMEOUT_MS);
}

} // namespace

hal_status_t
jh_rp_flash_storage_partition(jh_rp_flash_partition_id_t id,
                              jh_rp_flash_partition_t *out_partition) {
  if (out_partition == nullptr) {
    return HAL_EINVAL;
  }

  constexpr uint32_t kFlashSize = (uint32_t)PICO_FLASH_SIZE_BYTES;
  constexpr uint32_t kEepromSize = (uint32_t)HAL_RP_FLASH_EEPROM_SIZE;
  constexpr uint32_t kLittlefsSize = (uint32_t)HAL_RP_FLASH_LITTLEFS_SIZE;
  constexpr uint32_t kOtaSlotSize = (uint32_t)HAL_RP_OTA_SLOT_SIZE;
  static_assert((kEepromSize % FLASH_SECTOR_SIZE) == 0u,
                "RP EEPROM reservation must be sector-aligned");
  static_assert((kLittlefsSize % FLASH_SECTOR_SIZE) == 0u,
                "RP LittleFS reservation must be sector-aligned");
  static_assert(kLittlefsSize == 0u ||
                    kLittlefsSize >= (2u * FLASH_SECTOR_SIZE),
                "RP LittleFS reservation must contain at least two sectors");
  static_assert(kEepromSize + kLittlefsSize <= kFlashSize,
                "RP storage reservations exceed physical flash");
  static_assert((kOtaSlotSize % FLASH_SECTOR_SIZE) == 0u,
                "RP OTA slot must be sector-aligned");

  if (id >= JH_RP_FLASH_PARTITION_OTA_PROGRAM &&
      id <= JH_RP_FLASH_PARTITION_OTA_STATE_B && kOtaSlotSize == 0u) {
    return HAL_ECONFIG;
  }

  jh_rp_flash_partition_t partition = {};
  switch (id) {
  case JH_RP_FLASH_PARTITION_EEPROM:
    partition.flash_offset = kFlashSize - kEepromSize;
    partition.size = kEepromSize;
    break;
  case JH_RP_FLASH_PARTITION_LITTLEFS:
    partition.flash_offset = kFlashSize - kEepromSize - kLittlefsSize;
    partition.size = kLittlefsSize;
    break;
  case JH_RP_FLASH_PARTITION_OTA_PROGRAM:
    partition.flash_offset = (uint32_t)HAL_RP_OTA_PROGRAM_OFFSET;
    partition.size = kOtaSlotSize;
    break;
  case JH_RP_FLASH_PARTITION_OTA_STAGING:
    partition.flash_offset = (uint32_t)HAL_RP_OTA_STAGING_OFFSET;
    partition.size = kOtaSlotSize;
    break;
  case JH_RP_FLASH_PARTITION_OTA_PHASE:
    partition.flash_offset = (uint32_t)HAL_RP_OTA_PHASE_OFFSET;
    partition.size = FLASH_SECTOR_SIZE;
    break;
  case JH_RP_FLASH_PARTITION_OTA_SCRATCH:
    partition.flash_offset = (uint32_t)HAL_RP_OTA_SCRATCH_OFFSET;
    partition.size = FLASH_SECTOR_SIZE;
    break;
  case JH_RP_FLASH_PARTITION_OTA_STATE_A:
    partition.flash_offset = (uint32_t)HAL_RP_OTA_STATE_A_OFFSET;
    partition.size = FLASH_SECTOR_SIZE;
    break;
  case JH_RP_FLASH_PARTITION_OTA_STATE_B:
    partition.flash_offset = (uint32_t)HAL_RP_OTA_STATE_B_OFFSET;
    partition.size = FLASH_SECTOR_SIZE;
    break;
  default:
    return HAL_EINVAL;
  }

  if (partition.size == 0u) {
    return HAL_ECONFIG;
  }
  *out_partition = partition;
  return HAL_OK;
}

hal_status_t jh_rp_flash_storage_read(const jh_rp_flash_partition_t *partition,
                                      uint32_t offset, void *out, size_t size) {
  if (out == nullptr || !range_valid(partition, offset, size)) {
    return HAL_EINVAL;
  }
  if (size == 0u) {
    return HAL_OK;
  }

  memcpy(out,
         reinterpret_cast<const void *>((uintptr_t)XIP_BASE +
                                        partition->flash_offset + offset),
         size);
  return HAL_OK;
}

hal_status_t
jh_rp_flash_storage_program(const jh_rp_flash_partition_t *partition,
                            uint32_t offset, const void *data, size_t size) {
  if (data == nullptr || !range_valid(partition, offset, size) ||
      ((partition->flash_offset + offset) % FLASH_PAGE_SIZE) != 0u ||
      (size % FLASH_PAGE_SIZE) != 0u) {
    return HAL_EINVAL;
  }
  if (size == 0u) {
    return HAL_OK;
  }

  const uint32_t flash_offset = partition->flash_offset + offset;
  const hal_status_t status =
      execute(FlashAction::Program, flash_offset, data, size);
  if (status != HAL_OK) {
    return status;
  }
  return memcmp(
             reinterpret_cast<const void *>((uintptr_t)XIP_BASE + flash_offset),
             data, size) == 0
             ? HAL_OK
             : HAL_EIO;
}

hal_status_t jh_rp_flash_storage_erase(const jh_rp_flash_partition_t *partition,
                                       uint32_t offset, size_t size) {
  if (!range_valid(partition, offset, size) ||
      ((partition->flash_offset + offset) % FLASH_SECTOR_SIZE) != 0u ||
      (size % FLASH_SECTOR_SIZE) != 0u) {
    return HAL_EINVAL;
  }
  if (size == 0u) {
    return HAL_OK;
  }
  return execute(FlashAction::Erase, partition->flash_offset + offset, nullptr,
                 size);
}

hal_status_t
jh_rp_flash_storage_replace(const jh_rp_flash_partition_t *partition,
                            const void *data, size_t size) {
  if (data == nullptr || partition == nullptr || size != partition->size ||
      (partition->flash_offset % FLASH_SECTOR_SIZE) != 0u ||
      (size % FLASH_SECTOR_SIZE) != 0u || (size % FLASH_PAGE_SIZE) != 0u) {
    return HAL_EINVAL;
  }

  const hal_status_t status =
      execute(FlashAction::Replace, partition->flash_offset, data, size);
  if (status != HAL_OK) {
    return status;
  }
  return memcmp(reinterpret_cast<const void *>((uintptr_t)XIP_BASE +
                                               partition->flash_offset),
                data, size) == 0
             ? HAL_OK
             : HAL_EIO;
}

hal_status_t
jh_rp_flash_storage_program_bytes(const jh_rp_flash_partition_t *partition,
                                  uint32_t offset, const void *data,
                                  size_t size) {
  if (data == nullptr || !range_valid(partition, offset, size)) {
    return HAL_EINVAL;
  }
  if (size == 0u) {
    return HAL_OK;
  }
  return execute(FlashAction::ProgramBytes, partition->flash_offset + offset,
                 data, size);
}

hal_status_t jh_rp_flash_storage_replace_published(
    const jh_rp_flash_partition_t *partition, uint32_t offset, const void *data,
    size_t size, size_t publish_size, bool erased) {
  if (data == nullptr || !range_valid(partition, offset, size) ||
      publish_size == 0u || publish_size >= size ||
      ((partition->flash_offset + offset) % FLASH_SECTOR_SIZE) != 0u ||
      (size % FLASH_SECTOR_SIZE) != 0u ||
      (publish_size % FLASH_PAGE_SIZE) != 0u ||
      ((size - publish_size) % FLASH_PAGE_SIZE) != 0u) {
    return HAL_EINVAL;
  }

  FlashOperation operation = {FlashAction::ReplacePublished,
                              partition->flash_offset + offset,
                              static_cast<const uint8_t *>(data),
                              size,
                              publish_size,
                              erased};
  const hal_status_t status = jh_rp_flash_transaction_execute(
      run_flash_operation, &operation, HAL_RP_FLASH_TRANSACTION_TIMEOUT_MS);
  if (status != HAL_OK) {
    return status;
  }
  return memcmp(reinterpret_cast<const void *>((uintptr_t)XIP_BASE +
                                               operation.flash_offset),
                data, size) == 0
             ? HAL_OK
             : HAL_EIO;
}

#ifdef JH_RP_FLASH_FAULT_INJECTION
void jh_rp_flash_storage_set_fail_phase(jh_rp_flash_fail_phase_t phase) {
  s_fail_phase = phase;
}
#endif

#endif
