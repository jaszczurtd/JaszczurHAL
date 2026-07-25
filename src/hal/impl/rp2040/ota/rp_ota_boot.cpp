#if defined(JH_RP_OTA_BOOT_IMAGE)

#include "hal/hal_config.h"
#include "hal/hal_crypto.h"
#include "hal/impl/shared/network/ota/jh_ota_image.h"
#include "hal/impl/shared/network/ota/jh_ota_swap_engine.h"

#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/bootrom.h>
#include <pico/platform.h>

#include <string.h>

namespace {

const uint8_t *flash_ptr(uint32_t offset) {
  return reinterpret_cast<const uint8_t *>((uintptr_t)XIP_BASE + offset);
}

bool digest_matches(uint32_t offset, uint32_t size,
                    const uint8_t expected[JH_OTA_SHA256_BYTES]) {
  if (size == 0u || size > HAL_RP_OTA_SLOT_SIZE) {
    return false;
  }
  hal_sha256_context_t context = {};
  if (hal_sha256_init_ex(&context) != HAL_OK) {
    return false;
  }
  constexpr size_t kChunk = 1024u;
  for (uint32_t position = 0u; position < size; position += kChunk) {
    const size_t chunk = size - position < kChunk ? size - position : kChunk;
    if (hal_sha256_update_ex(&context, flash_ptr(offset + position), chunk) !=
        HAL_OK) {
      return false;
    }
  }
  uint8_t actual[JH_OTA_SHA256_BYTES];
  return hal_sha256_final_ex(&context, actual) == HAL_OK &&
         memcmp(actual, expected, sizeof(actual)) == 0;
}

bool read_state(jh_ota_boot_state_t *out_state, uint8_t *out_index) {
  return jh_ota_boot_state_select(flash_ptr(HAL_RP_OTA_STATE_A_OFFSET),
                                  flash_ptr(HAL_RP_OTA_STATE_B_OFFSET),
                                  out_state, out_index) == HAL_OK;
}

bool write_state(const jh_ota_boot_state_t *state, uint8_t current_index) {
  uint8_t encoded[JH_OTA_STATE_RECORD_SIZE];
  if (jh_ota_boot_state_encode(state, encoded) != HAL_OK) {
    return false;
  }
  const uint32_t offset = current_index == 0u ? HAL_RP_OTA_STATE_B_OFFSET
                                              : HAL_RP_OTA_STATE_A_OFFSET;
  flash_range_erase(offset, FLASH_SECTOR_SIZE);
  flash_range_program(offset, encoded, sizeof(encoded));
  return memcmp(flash_ptr(offset), encoded, sizeof(encoded)) == 0;
}

bool erase_phase(void) {
  flash_range_erase(HAL_RP_OTA_PHASE_OFFSET, FLASH_SECTOR_SIZE);
  for (size_t index = 0u; index < FLASH_SECTOR_SIZE; ++index) {
    if (flash_ptr(HAL_RP_OTA_PHASE_OFFSET)[index] != 0xFFu) {
      return false;
    }
  }
  return true;
}

hal_status_t mark_phase(void *, uint32_t sector_index, uint8_t value) {
  const uint32_t page_offset = sector_index - (sector_index % FLASH_PAGE_SIZE);
  uint8_t page[FLASH_PAGE_SIZE];
  memcpy(page, flash_ptr(HAL_RP_OTA_PHASE_OFFSET + page_offset), sizeof(page));
  page[sector_index % FLASH_PAGE_SIZE] &= value;
  flash_range_program(HAL_RP_OTA_PHASE_OFFSET + page_offset, page,
                      sizeof(page));
  return flash_ptr(HAL_RP_OTA_PHASE_OFFSET)[sector_index] == value ? HAL_OK
                                                                   : HAL_EIO;
}

bool copy_sector_raw(uint32_t destination, uint32_t source) {
  static uint8_t sector_buffer[FLASH_SECTOR_SIZE];
  memcpy(sector_buffer, flash_ptr(source), sizeof(sector_buffer));
  flash_range_erase(destination, FLASH_SECTOR_SIZE);
  flash_range_program(destination, sector_buffer, sizeof(sector_buffer));
  return memcmp(flash_ptr(destination), sector_buffer, sizeof(sector_buffer)) ==
         0;
}

uint32_t slot_offset(jh_ota_swap_slot_t slot, uint32_t sector) {
  const uint32_t relative = sector * FLASH_SECTOR_SIZE;
  switch (slot) {
  case JH_OTA_SWAP_SLOT_PROGRAM:
    return HAL_RP_OTA_PROGRAM_OFFSET + relative;
  case JH_OTA_SWAP_SLOT_STAGING:
    return HAL_RP_OTA_STAGING_OFFSET + relative;
  case JH_OTA_SWAP_SLOT_SCRATCH:
    return HAL_RP_OTA_SCRATCH_OFFSET;
  default:
    return UINT32_MAX;
  }
}

hal_status_t read_phase(void *, uint32_t sector, uint8_t *out_phase) {
  if (out_phase == nullptr || sector >= FLASH_SECTOR_SIZE) {
    return HAL_EINVAL;
  }
  *out_phase = flash_ptr(HAL_RP_OTA_PHASE_OFFSET)[sector];
  return HAL_OK;
}

hal_status_t copy_sector(void *, uint32_t sector,
                         jh_ota_swap_slot_t destination,
                         jh_ota_swap_slot_t source) {
  const uint32_t destination_offset = slot_offset(destination, sector);
  const uint32_t source_offset = slot_offset(source, sector);
  if (destination_offset == UINT32_MAX || source_offset == UINT32_MAX) {
    return HAL_EINVAL;
  }
  return copy_sector_raw(destination_offset, source_offset) ? HAL_OK : HAL_EIO;
}

bool swap_slots(void) {
  static const jh_ota_swap_backend_t backend = {read_phase, copy_sector,
                                                mark_phase};
  const uint32_t sectors = HAL_RP_OTA_SLOT_SIZE / FLASH_SECTOR_SIZE;
  return sectors <= FLASH_SECTOR_SIZE &&
         jh_ota_swap_execute(&backend, nullptr, sectors) == HAL_OK;
}

void swap_state_roles(jh_ota_boot_state_t *state) {
  uint32_t value = state->program_size;
  state->program_size = state->staging_size;
  state->staging_size = value;
  value = state->program_generation;
  state->program_generation = state->staging_generation;
  state->staging_generation = value;

  uint8_t digest[JH_OTA_SHA256_BYTES];
  memcpy(digest, state->program_sha256, sizeof(digest));
  memcpy(state->program_sha256, state->staging_sha256,
         sizeof(state->program_sha256));
  memcpy(state->staging_sha256, digest, sizeof(state->staging_sha256));

  char version[JH_OTA_VERSION_TEXT_SIZE];
  memcpy(version, state->program_version, sizeof(version));
  memcpy(state->program_version, state->staging_version,
         sizeof(state->program_version));
  memcpy(state->staging_version, version, sizeof(state->staging_version));
}

[[noreturn]] void recovery(void) {
  watchdog_reboot(0u, 0u, 100u);
  for (;;) {
    tight_loop_contents();
  }
}

[[gnu::naked, noreturn]] void jump_to_vectors(uintptr_t vectors) {
  (void)vectors;
  asm volatile("cpsid i\n"
               "movs r1, #0\n"
               "ldr r3, =0xe000e010\n"
               "str r1, [r3]\n"
               "ldr r1, =0xffffffff\n"
               "ldr r3, =0xe000e180\n"
               "str r1, [r3]\n"
               "ldr r3, =0xe000e280\n"
               "str r1, [r3]\n"
#if PICO_RP2350
               "ldr r3, =0xe000e184\n"
               "str r1, [r3]\n"
               "ldr r3, =0xe000e284\n"
               "str r1, [r3]\n"
#endif
               "ldr r1, [r0]\n"
               "ldr r2, [r0, #4]\n"
               "ldr r3, =0xe000ed08\n"
               "str r0, [r3]\n"
#if PICO_RP2350
               "movs r3, #0\n"
               "msr msplim, r3\n"
#endif
               "dsb\n"
               "isb\n"
               "msr msp, r1\n"
               "cpsie i\n"
               "bx r2\n");
}

[[noreturn]] void launch_program(void) {
#if PICO_RP2040
  const uintptr_t vectors =
      (uintptr_t)XIP_BASE + HAL_RP_OTA_PROGRAM_OFFSET + FLASH_PAGE_SIZE;
#else
  const uintptr_t vectors = (uintptr_t)XIP_BASE + HAL_RP_OTA_PROGRAM_OFFSET;
#endif
  jump_to_vectors(vectors);
}

void complete_swap(jh_ota_boot_state_t *state, uint8_t *state_index,
                   jh_ota_boot_mode_t completed_mode) {
  if (!swap_slots()) {
    state->sequence++;
    state->mode = JH_OTA_BOOT_RECOVERY;
    (void)write_state(state, *state_index);
    recovery();
  }
  swap_state_roles(state);
  state->sequence++;
  state->mode = completed_mode;
  state->attempts = 0u;
  if (!write_state(state, *state_index)) {
    recovery();
  }
  *state_index ^= 1u;
  (void)erase_phase();
}

} // namespace

int main() {
  jh_ota_boot_state_t state = {};
  uint8_t state_index = 0u;
  if (!read_state(&state, &state_index)) {
    launch_program();
  }

  if (state.mode == JH_OTA_BOOT_PENDING) {
    if (!digest_matches(HAL_RP_OTA_STAGING_OFFSET, state.staging_size,
                        state.staging_sha256)) {
      state.sequence++;
      state.mode = JH_OTA_BOOT_RECOVERY;
      (void)write_state(&state, state_index);
      launch_program();
    }
    complete_swap(&state, &state_index, JH_OTA_BOOT_TRIAL);
    if (!digest_matches(HAL_RP_OTA_PROGRAM_OFFSET, state.program_size,
                        state.program_sha256)) {
      recovery();
    }
  } else if (state.mode == JH_OTA_BOOT_TRIAL) {
    if (state.attempts >= state.max_attempts) {
      if (!erase_phase()) {
        recovery();
      }
      state.sequence++;
      state.mode = JH_OTA_BOOT_ROLLBACK;
      if (!write_state(&state, state_index)) {
        recovery();
      }
      state_index ^= 1u;
      complete_swap(&state, &state_index, JH_OTA_BOOT_STABLE);
    } else {
      state.sequence++;
      state.attempts++;
      if (!write_state(&state, state_index)) {
        recovery();
      }
      state_index ^= 1u;
    }
  } else if (state.mode == JH_OTA_BOOT_ROLLBACK) {
    complete_swap(&state, &state_index, JH_OTA_BOOT_STABLE);
  } else if (state.mode == JH_OTA_BOOT_RECOVERY) {
    launch_program();
  }

  launch_program();
}

#endif /* JH_RP_OTA_BOOT_IMAGE */
