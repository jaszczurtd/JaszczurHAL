#pragma once

#include <hal/core/hal_status.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t jh_c85_hid_device_start(void);
hal_status_t jh_c85_hid_device_service(void);
void jh_c85_hid_device_get_info(bool *controller_ready, bool *hid_connected,
                                uint32_t *report_count);

#ifdef __cplusplus
}
#endif
