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

enum class FlashAction : uint8_t {
  Program,
  Erase,
  Replace,
};

struct FlashOperation {
  FlashAction action;
  uint32_t flash_offset;
  const uint8_t *data;
  size_t size;
};

bool range_valid(const jh_rp_flash_partition_t *partition, uint32_t offset,
                 size_t size) {
  return partition != nullptr && partition->size > 0u &&
         offset <= partition->size && size <= partition->size - offset;
}

hal_status_t
__no_inline_not_in_flash_func(run_flash_operation)(void *raw_context) {
  auto *operation = static_cast<FlashOperation *>(raw_context);
  if (operation->action == FlashAction::Erase ||
      operation->action == FlashAction::Replace) {
    flash_range_erase(operation->flash_offset, operation->size);
  }
  if (operation->action == FlashAction::Program ||
      operation->action == FlashAction::Replace) {
    flash_range_program(operation->flash_offset, operation->data,
                        operation->size);
  }
  return HAL_OK;
}

hal_status_t execute(FlashAction action, uint32_t flash_offset,
                     const void *data, size_t size) {
  FlashOperation operation = {action, flash_offset,
                              static_cast<const uint8_t *>(data), size};
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

#endif
