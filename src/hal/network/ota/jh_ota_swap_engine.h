#pragma once

#include "hal/core/hal_status.h"
#include "hal/network/ota/jh_ota_image.h"

#include <stdbool.h>
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

/** Flash access the boot applier needs besides the sector swap. */
typedef struct {
  jh_ota_swap_backend_t swap;
  /** Write @p state to the record that is not @p current_index. */
  hal_status_t (*write_state)(void *context, const jh_ota_boot_state_t *state,
                              uint8_t current_index);
  /** Erase the whole phase journal. */
  hal_status_t (*erase_phase)(void *context);
  /** true when the first @p size bytes of @p slot hash to @p sha256. */
  bool (*slot_matches)(void *context, jh_ota_swap_slot_t slot, uint32_t size,
                       const uint8_t sha256[JH_OTA_SHA256_BYTES]);
} jh_ota_boot_backend_t;

typedef enum {
  /** Start the image in the program slot. */
  JH_OTA_BOOT_ACTION_LAUNCH = 0,
  /** Reset and run the applier again; the journal resumes the swap. */
  JH_OTA_BOOT_ACTION_RESTART = 1
} jh_ota_boot_action_t;

/**
 * @brief Apply the boot state once, as the boot image does on every reset.
 *
 * A pending image is verified only before the swap starts; a swap already
 * recorded in the phase journal resumes without the staging check, because
 * its sectors are spread over both slots. A swap that fails after the program
 * slot changed asks for a restart instead of launching a mixed image.
 *
 * @param backend      Flash access; every callback must be set.
 * @param context      Passed to every callback.
 * @param state        State selected at boot; updated with what was written.
 * @param state_index  Index of the record @p state was read from.
 * @param sector_count Sectors per slot; one journal byte each.
 * @return What the boot image does next. Invalid arguments launch the program.
 */
jh_ota_boot_action_t jh_ota_boot_apply(const jh_ota_boot_backend_t *backend,
                                       void *context,
                                       jh_ota_boot_state_t *state,
                                       uint8_t state_index,
                                       uint32_t sector_count);

#ifdef __cplusplus
}
#endif
