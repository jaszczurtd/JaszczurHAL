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
} jh_eeprom_provider_info_t;

typedef struct {
  hal_status_t (*initialize)(const jh_eeprom_provider_config_t *config,
                             jh_eeprom_provider_info_t *out_info);
  hal_status_t (*read)(uint16_t addr, uint8_t *out, uint16_t len);
  hal_status_t (*write)(uint16_t addr, const uint8_t *data, uint16_t len,
                        hal_eeprom_progress_callback_t progress, void *ctx);
  hal_status_t (*commit)(hal_eeprom_progress_callback_t progress, void *ctx);
  hal_status_t (*reset)(hal_eeprom_progress_callback_t progress, void *ctx);
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
} jh_eeprom_flash_backend_t;

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
