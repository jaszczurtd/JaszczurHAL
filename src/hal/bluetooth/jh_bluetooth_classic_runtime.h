#pragma once

#include "hal/bluetooth/hal_bluetooth_classic.h"
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
#include "hal/bluetooth/hal_bluetooth_hid_host.h"
#endif
#include "hal/core/hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

bool jh_bluetooth_classic_handle_valid(hal_bluetooth_classic_t classic);

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
hal_status_t jh_bluetooth_classic_hid_attach(hal_bluetooth_classic_t classic);
hal_status_t jh_bluetooth_classic_hid_detach(hal_bluetooth_classic_t classic);
hal_status_t
jh_bluetooth_classic_hid_get_info(hal_bluetooth_classic_t classic,
                                  hal_bluetooth_hid_info_t *out_info);
hal_status_t jh_bluetooth_classic_hid_connect(
    hal_bluetooth_classic_t classic,
    const hal_bluetooth_classic_address_t *address);
hal_status_t
jh_bluetooth_classic_hid_disconnect(hal_bluetooth_classic_t classic);
hal_status_t
jh_bluetooth_classic_hid_descriptor(hal_bluetooth_classic_t classic,
                                    uint8_t *out_descriptor, size_t capacity,
                                    size_t *out_length);
hal_status_t
jh_bluetooth_classic_hid_report_next(hal_bluetooth_classic_t classic,
                                     hal_bluetooth_hid_report_t *out_report);
hal_status_t
jh_bluetooth_classic_hid_report_send(hal_bluetooth_classic_t classic,
                                     const hal_bluetooth_hid_report_t *report);
hal_status_t
jh_bluetooth_classic_hid_report_request(hal_bluetooth_classic_t classic,
                                        hal_bluetooth_hid_report_type_t type,
                                        uint8_t report_id);
#endif

#if HAL_TARGET_IS_MOCK
void hal_mock_bluetooth_classic_runtime_full_reset(void);
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
void hal_mock_bluetooth_hid_runtime_full_reset(void);
#endif
#endif

#ifdef __cplusplus
}
#endif
