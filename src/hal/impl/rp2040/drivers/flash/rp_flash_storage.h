#pragma once

#include "hal/core/hal_status.h"

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

hal_status_t
jh_rp_flash_storage_replace_published(const jh_rp_flash_partition_t *partition,
                                      uint32_t offset, const void *data,
                                      size_t size, size_t publish_size);

#ifdef JH_RP_FLASH_FAULT_INJECTION
typedef enum {
  JH_RP_FLASH_REPLACE_FAIL_NONE = 0,
  JH_RP_FLASH_REPLACE_FAIL_AFTER_INVALIDATE,
  JH_RP_FLASH_REPLACE_FAIL_AFTER_BODY,
  JH_RP_FLASH_REPLACE_FAIL_AFTER_VERIFY,
  JH_RP_FLASH_REPLACE_FAIL_AFTER_PUBLISH,
} jh_rp_flash_replace_fail_phase_t;

/** Test fixture hook; never enable in production firmware. */
void jh_rp_flash_storage_set_replace_fail_phase(
    jh_rp_flash_replace_fail_phase_t phase);
#endif

#ifdef __cplusplus
}
#endif
