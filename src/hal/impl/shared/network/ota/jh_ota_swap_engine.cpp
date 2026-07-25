#include "jh_ota_swap_engine.h"

namespace {

constexpr uint8_t kErased = 0xFFu;
constexpr uint8_t kScratchReady = 0xFEu;
constexpr uint8_t kProgramReady = 0xFCu;
constexpr uint8_t kStagingReady = 0xF8u;

hal_status_t copy_and_mark(const jh_ota_swap_backend_t *backend, void *context,
                           uint32_t sector, jh_ota_swap_slot_t destination,
                           jh_ota_swap_slot_t source, uint8_t phase) {
  hal_status_t status =
      backend->copy_sector(context, sector, destination, source);
  if (status != HAL_OK) {
    return status;
  }
  return backend->mark_phase(context, sector, phase);
}

} // namespace

hal_status_t jh_ota_swap_execute(const jh_ota_swap_backend_t *backend,
                                 void *context, uint32_t sector_count) {
  if (backend == nullptr || backend->read_phase == nullptr ||
      backend->copy_sector == nullptr || backend->mark_phase == nullptr ||
      sector_count == 0u) {
    return HAL_EINVAL;
  }
  for (uint32_t sector = 0u; sector < sector_count; ++sector) {
    uint8_t phase = 0u;
    hal_status_t status = backend->read_phase(context, sector, &phase);
    if (status != HAL_OK) {
      return status;
    }
    if (phase == kErased) {
      status = copy_and_mark(backend, context, sector, JH_OTA_SWAP_SLOT_SCRATCH,
                             JH_OTA_SWAP_SLOT_PROGRAM, kScratchReady);
      if (status != HAL_OK) {
        return status;
      }
      phase = kScratchReady;
    }
    if (phase == kScratchReady) {
      status = copy_and_mark(backend, context, sector, JH_OTA_SWAP_SLOT_PROGRAM,
                             JH_OTA_SWAP_SLOT_STAGING, kProgramReady);
      if (status != HAL_OK) {
        return status;
      }
      phase = kProgramReady;
    }
    if (phase == kProgramReady) {
      status = copy_and_mark(backend, context, sector, JH_OTA_SWAP_SLOT_STAGING,
                             JH_OTA_SWAP_SLOT_SCRATCH, kStagingReady);
      if (status != HAL_OK) {
        return status;
      }
      phase = kStagingReady;
    }
    if (phase != kStagingReady) {
      return HAL_EPROTO;
    }
  }
  return HAL_OK;
}
