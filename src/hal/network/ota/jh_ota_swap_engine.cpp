#include "jh_ota_swap_engine.h"

#include <string.h>

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

bool boot_backend_valid(const jh_ota_boot_backend_t *backend) {
  return backend != nullptr && backend->swap.read_phase != nullptr &&
         backend->swap.copy_sector != nullptr &&
         backend->swap.mark_phase != nullptr &&
         backend->write_state != nullptr && backend->erase_phase != nullptr &&
         backend->slot_matches != nullptr;
}

// Any journal mark means the program slot may already hold new sectors.
bool swap_started(const jh_ota_swap_backend_t *swap, void *context,
                  uint32_t sector_count) {
  for (uint32_t sector = 0u; sector < sector_count; ++sector) {
    uint8_t phase = kErased;
    if (swap->read_phase(context, sector, &phase) != HAL_OK ||
        phase != kErased) {
      return true;
    }
  }
  return false;
}

template <typename T> void swap_values(T &first, T &second) {
  T value;
  memcpy(&value, &first, sizeof(value));
  memcpy(&first, &second, sizeof(first));
  memcpy(&second, &value, sizeof(second));
}

void swap_state_roles(jh_ota_boot_state_t *state) {
  swap_values(state->program_size, state->staging_size);
  swap_values(state->program_generation, state->staging_generation);
  swap_values(state->program_sha256, state->staging_sha256);
  swap_values(state->program_version, state->staging_version);
}

hal_status_t write_next_state(const jh_ota_boot_backend_t *backend,
                              void *context, jh_ota_boot_state_t *state,
                              uint8_t *state_index, jh_ota_boot_mode_t mode) {
  state->sequence++;
  state->mode = mode;
  const hal_status_t status =
      backend->write_state(context, state, *state_index);
  if (status == HAL_OK) {
    *state_index ^= 1u;
  }
  return status;
}

jh_ota_boot_action_t complete_swap(const jh_ota_boot_backend_t *backend,
                                   void *context, jh_ota_boot_state_t *state,
                                   uint8_t *state_index, uint32_t sector_count,
                                   jh_ota_boot_mode_t completed_mode) {
  if (jh_ota_swap_execute(&backend->swap, context, sector_count) != HAL_OK) {
    if (swap_started(&backend->swap, context, sector_count)) {
      return JH_OTA_BOOT_ACTION_RESTART;
    }
    (void)write_next_state(backend, context, state, state_index,
                           JH_OTA_BOOT_RECOVERY);
    return JH_OTA_BOOT_ACTION_LAUNCH;
  }
  swap_state_roles(state);
  state->attempts = 0u;
  if (write_next_state(backend, context, state, state_index, completed_mode) !=
      HAL_OK) {
    return JH_OTA_BOOT_ACTION_RESTART;
  }
  (void)backend->erase_phase(context);
  return JH_OTA_BOOT_ACTION_LAUNCH;
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

jh_ota_boot_action_t jh_ota_boot_apply(const jh_ota_boot_backend_t *backend,
                                       void *context,
                                       jh_ota_boot_state_t *state,
                                       uint8_t state_index,
                                       uint32_t sector_count) {
  if (!boot_backend_valid(backend) || state == nullptr || sector_count == 0u) {
    return JH_OTA_BOOT_ACTION_LAUNCH;
  }
  switch (state->mode) {
  case JH_OTA_BOOT_PENDING: {
    if (!swap_started(&backend->swap, context, sector_count) &&
        !backend->slot_matches(context, JH_OTA_SWAP_SLOT_STAGING,
                               state->staging_size, state->staging_sha256)) {
      (void)write_next_state(backend, context, state, &state_index,
                             JH_OTA_BOOT_RECOVERY);
      return JH_OTA_BOOT_ACTION_LAUNCH;
    }
    const jh_ota_boot_action_t action = complete_swap(
        backend, context, state, &state_index, sector_count, JH_OTA_BOOT_TRIAL);
    if (action != JH_OTA_BOOT_ACTION_LAUNCH ||
        state->mode != JH_OTA_BOOT_TRIAL) {
      return action;
    }
    return backend->slot_matches(context, JH_OTA_SWAP_SLOT_PROGRAM,
                                 state->program_size, state->program_sha256)
               ? JH_OTA_BOOT_ACTION_LAUNCH
               : JH_OTA_BOOT_ACTION_RESTART;
  }
  case JH_OTA_BOOT_TRIAL:
    if (state->attempts < state->max_attempts) {
      state->attempts++;
      return write_next_state(backend, context, state, &state_index,
                              JH_OTA_BOOT_TRIAL) == HAL_OK
                 ? JH_OTA_BOOT_ACTION_LAUNCH
                 : JH_OTA_BOOT_ACTION_RESTART;
    }
    if (backend->erase_phase(context) != HAL_OK ||
        write_next_state(backend, context, state, &state_index,
                         JH_OTA_BOOT_ROLLBACK) != HAL_OK) {
      return JH_OTA_BOOT_ACTION_RESTART;
    }
    return complete_swap(backend, context, state, &state_index, sector_count,
                         JH_OTA_BOOT_STABLE);
  case JH_OTA_BOOT_ROLLBACK:
    return complete_swap(backend, context, state, &state_index, sector_count,
                         JH_OTA_BOOT_STABLE);
  default:
    return JH_OTA_BOOT_ACTION_LAUNCH;
  }
}
