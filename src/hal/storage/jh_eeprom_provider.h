#pragma once

#include "hal/storage/hal_eeprom.h"

#ifdef HAL_ENABLE_EEPROM

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  hal_eeprom_type_t requested_type;
  uint16_t requested_size;
  uint8_t i2c_addr;
} jh_eeprom_provider_config_t;

typedef struct {
  hal_eeprom_type_t type;
  uint16_t size;
  uint16_t erase_size;
  uint16_t program_size;
  /** Granularity of append(): an appended range starts and ends on it, and a
   *  unit once programmed is never programmed again before an erase. */
  uint16_t append_size;
} jh_eeprom_provider_info_t;

typedef struct {
  hal_status_t (*initialize)(const jh_eeprom_provider_config_t *config,
                             jh_eeprom_provider_info_t *out_info);
  hal_status_t (*read)(uint16_t addr, uint8_t *out, uint16_t len);
  hal_status_t (*write)(uint16_t addr, const uint8_t *data, uint16_t len,
                        hal_eeprom_progress_callback_t progress, void *ctx);
  hal_status_t (*commit)(hal_eeprom_progress_callback_t progress, void *ctx);
  hal_status_t (*replace_region)(uint16_t addr, const uint8_t *data,
                                 uint16_t len, uint16_t publish_size,
                                 hal_eeprom_progress_callback_t progress,
                                 void *ctx);
  hal_status_t (*reset)(hal_eeprom_progress_callback_t progress, void *ctx);
  /* Program an erased range without erasing anything, then verify it. */
  hal_status_t (*append)(uint16_t addr, const uint8_t *data, uint16_t len,
                         hal_eeprom_progress_callback_t progress, void *ctx);
  /* Return an erase-aligned range to the erased state (0xFF). */
  hal_status_t (*erase)(uint16_t addr, uint16_t len,
                        hal_eeprom_progress_callback_t progress, void *ctx);
  /* Report whether every byte of the range reads as erased (0xFF). */
  hal_status_t (*region_erased)(uint16_t addr, uint16_t len, bool *out_erased);
} jh_eeprom_provider_ops_t;

typedef struct {
  hal_eeprom_type_t type;
  uint8_t *mirror;
  uint16_t mirror_capacity;
  bool clamp_oversized_request;
  bool clear_full_storage_on_reset;
  void *context;
  hal_status_t (*load)(void *context, uint8_t *mirror, uint16_t mirror_capacity,
                       uint16_t *out_storage_size);
  hal_status_t (*store)(void *context, const uint8_t *mirror,
                        uint16_t storage_size,
                        hal_eeprom_progress_callback_t progress, void *ctx);
  /* destination_erased skips the erase when the range already reads 0xFF. */
  hal_status_t (*replace_region)(void *context, uint16_t addr,
                                 const uint8_t *data, uint16_t len,
                                 uint16_t publish_size, bool destination_erased,
                                 hal_eeprom_progress_callback_t progress,
                                 void *ctx);
  /* Program append_size-aligned bytes into an erased range and verify them. */
  hal_status_t (*program)(void *context, uint16_t addr, const uint8_t *data,
                          uint16_t len, hal_eeprom_progress_callback_t progress,
                          void *ctx);
  hal_status_t (*erase)(void *context, uint16_t addr, uint16_t len,
                        hal_eeprom_progress_callback_t progress, void *ctx);
  /* Read the medium itself; resynchronizes the mirror after a failed write. */
  hal_status_t (*read)(void *context, uint16_t addr, uint8_t *out,
                       uint16_t len);
  uint16_t erase_size;
  uint16_t program_size;
  uint16_t append_size;
} jh_eeprom_flash_backend_t;

/** Validate a publication range against the active provider without I/O. */
hal_status_t jh_eeprom_validate_region(uint16_t addr, uint16_t len,
                                       uint16_t publish_size);

/** Run under the facade lock, after validation and before a physical write. */
hal_status_t jh_eeprom_flash_write_begin(void);
/** Pair with a successful begin, including when the physical write fails. */
void jh_eeprom_flash_write_end(void);

/** Replace one independent storage region and publish its prefix last. */
hal_status_t jh_eeprom_replace_region(uint16_t addr, const uint8_t *data,
                                      uint16_t len, uint16_t publish_size);

/**
 * Program bytes into an erased range without an erase. addr and len must be
 * multiples of the provider's append size; HAL_ESTATE when the range is not
 * erased, HAL_EIO when verification fails.
 */
hal_status_t jh_eeprom_append_region(uint16_t addr, const uint8_t *data,
                                     uint16_t len);

/** Erase an erase-aligned range (0xFF afterwards). */
hal_status_t jh_eeprom_erase_region(uint16_t addr, uint16_t len);

/** Report whether every byte of a range reads as erased (0xFF). */
hal_status_t jh_eeprom_region_erased(uint16_t addr, uint16_t len,
                                     bool *out_erased);

/** Report the active provider's append granularity in bytes. */
hal_status_t jh_eeprom_append_size(uint16_t *out_size);

/** Return the target-selected provider for a public EEPROM type. */
const jh_eeprom_provider_ops_t *
jh_eeprom_provider_get_ops(hal_eeprom_type_t type);

/** Return the portable AT24C256 provider for direct driver tests. */
const jh_eeprom_provider_ops_t *jh_at24c256_provider_get_ops(void);

/** Bind a target flash mechanism to the shared buffered-flash provider. */
const jh_eeprom_provider_ops_t *
jh_eeprom_flash_provider_configure(const jh_eeprom_flash_backend_t *backend);

/** Resolve AT24C256 or a target-native flash backend without target branches.
 */
const jh_eeprom_provider_ops_t *jh_eeprom_hardware_provider_get_ops(
    hal_eeprom_type_t type, const jh_eeprom_flash_backend_t *flash_backend);

/** Reset shared facade state after resetting the mock provider. */
void jh_eeprom_mock_reset_facade(void);

#endif /* HAL_ENABLE_EEPROM */
