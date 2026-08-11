#pragma once

#include "hal/core/hal_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_OTA_SWAP_SLOT_PROGRAM = 0,
  JH_OTA_SWAP_SLOT_STAGING = 1,
  JH_OTA_SWAP_SLOT_SCRATCH = 2
} jh_ota_swap_slot_t;

typedef hal_status_t (*jh_ota_swap_read_phase_fn)(void *context,
                                                  uint32_t sector,
                                                  uint8_t *out_phase);
typedef hal_status_t (*jh_ota_swap_copy_sector_fn)(
    void *context, uint32_t sector, jh_ota_swap_slot_t destination,
    jh_ota_swap_slot_t source);
typedef hal_status_t (*jh_ota_swap_mark_phase_fn)(void *context,
                                                  uint32_t sector,
                                                  uint8_t phase);

typedef struct {
  jh_ota_swap_read_phase_fn read_phase;
  jh_ota_swap_copy_sector_fn copy_sector;
  jh_ota_swap_mark_phase_fn mark_phase;
} jh_ota_swap_backend_t;

hal_status_t jh_ota_swap_execute(const jh_ota_swap_backend_t *backend,
                                 void *context, uint32_t sector_count);

#ifdef __cplusplus
}
#endif
