#pragma once

#include "hal/bluetooth/jh_bluetooth_classic_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

void jh_bluetooth_avrcp_target_backend_event(
    const jh_bluetooth_classic_backend_event_t *event);

#if HAL_TARGET_IS_MOCK
void hal_mock_bluetooth_avrcp_runtime_full_reset(void);
#endif

#ifdef __cplusplus
}
#endif
