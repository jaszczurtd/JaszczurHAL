#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLE_STREAM

#include "jh_ble_backend.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Deliver one stream-related backend event. The BLE runtime calls this after
 * releasing its own mutex, so the stream keeps a separate lock and never
 * nests under the radio lock.
 */
void jh_ble_stream_on_backend_event(const jh_ble_backend_event_t *event);

/**
 * Report a lost link or a controller generation change. The stream closes any
 * session and zeroes its directional keys.
 */
void jh_ble_stream_on_link_lost(uint32_t generation);

/**
 * Periodic tick driven by hal_ble_poll(). Expires an idle session and lifts
 * the authentication backoff once its window closes.
 */
void jh_ble_stream_on_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE_STREAM */
