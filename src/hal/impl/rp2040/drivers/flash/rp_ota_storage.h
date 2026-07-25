#pragma once

#include "hal/hal_status.h"
#include "hal/impl/shared/network/ota/jh_ota_image.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_rp_ota_storage_begin(uint32_t container_size,
                                     const uint8_t *authentication_key,
                                     size_t authentication_key_size);
hal_status_t jh_rp_ota_storage_write(const uint8_t *data, size_t size,
                                     size_t *out_written);
hal_status_t jh_rp_ota_storage_finish(void);
void jh_rp_ota_storage_abort(void);

hal_status_t jh_rp_ota_storage_confirm_boot(void);
hal_status_t jh_rp_ota_storage_get_state(jh_ota_boot_state_t *out_state);

#ifdef __cplusplus
}
#endif
