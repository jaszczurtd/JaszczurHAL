#pragma once

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_RP_FLASH_PARTITION_EEPROM = 0,
  JH_RP_FLASH_PARTITION_LITTLEFS = 1,
  JH_RP_FLASH_PARTITION_OTA_PROGRAM = 2,
  JH_RP_FLASH_PARTITION_OTA_STAGING = 3,
  JH_RP_FLASH_PARTITION_OTA_PHASE = 4,
  JH_RP_FLASH_PARTITION_OTA_SCRATCH = 5,
  JH_RP_FLASH_PARTITION_OTA_STATE_A = 6,
  JH_RP_FLASH_PARTITION_OTA_STATE_B = 7,
} jh_rp_flash_partition_id_t;

typedef struct {
  uint32_t flash_offset;
  uint32_t size;
} jh_rp_flash_partition_t;

hal_status_t
jh_rp_flash_storage_partition(jh_rp_flash_partition_id_t id,
                              jh_rp_flash_partition_t *out_partition);

hal_status_t jh_rp_flash_storage_read(const jh_rp_flash_partition_t *partition,
                                      uint32_t offset, void *out, size_t size);

hal_status_t
jh_rp_flash_storage_program(const jh_rp_flash_partition_t *partition,
                            uint32_t offset, const void *data, size_t size);

hal_status_t jh_rp_flash_storage_erase(const jh_rp_flash_partition_t *partition,
                                       uint32_t offset, size_t size);

hal_status_t
jh_rp_flash_storage_replace(const jh_rp_flash_partition_t *partition,
                            const void *data, size_t size);

/**
 * Program an arbitrary byte range of erased flash in one transaction, page by
 * page; the rest of each touched page is sent as 0xFF and stays unchanged.
 * Verifies the range afterwards (HAL_EIO on mismatch).
 */
hal_status_t
jh_rp_flash_storage_program_bytes(const jh_rp_flash_partition_t *partition,
                                  uint32_t offset, const void *data,
                                  size_t size);

/**
 * Replace a sector-aligned region: erase it unless erased is true, program
 * and verify the body, then program the publish_size prefix last. Body pages
 * that are all 0xFF are not programmed.
 */
hal_status_t jh_rp_flash_storage_replace_published(
    const jh_rp_flash_partition_t *partition, uint32_t offset, const void *data,
    size_t size, size_t publish_size, bool erased);

/* Fault phases the fault-injection hook can force; never used by firmware. */
typedef enum {
  JH_RP_FLASH_FAIL_NONE = 0,
  JH_RP_FLASH_FAIL_AFTER_INVALIDATE, /* replace: erased, nothing programmed */
  JH_RP_FLASH_FAIL_AFTER_BODY,       /* replace: body programmed, unverified */
  JH_RP_FLASH_FAIL_AFTER_VERIFY,     /* replace: body verified, prefix not */
  JH_RP_FLASH_FAIL_AFTER_PUBLISH,    /* replace: complete, then an error */
  JH_RP_FLASH_FAIL_APPEND_TORN,      /* append: first half of the bytes only */
  JH_RP_FLASH_FAIL_APPEND_AFTER_PROGRAM, /* append: complete, then an error */
} jh_rp_flash_fail_phase_t;

#ifdef JH_RP_FLASH_FAULT_INJECTION
/** Test fixture hook; never enable in production firmware. */
void jh_rp_flash_storage_set_fail_phase(jh_rp_flash_fail_phase_t phase);
#endif

#ifdef __cplusplus
}
#endif
