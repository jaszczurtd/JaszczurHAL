#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLE_STREAM

#include "jh_ble_backend.h"

#include <stddef.h>
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

/** Queue a payload only while the expected authenticated session is active. */
hal_status_t jh_ble_stream_send_for_session(const void *data, size_t length,
                                            uint32_t expected_generation,
                                            uint64_t expected_session_id);

/** Pop a payload only while the expected authenticated session is active. */
hal_status_t jh_ble_stream_receive_for_session(
    void *out, size_t capacity, size_t *out_length,
    hal_ble_stream_payload_info_t *out_payload_info,
    uint32_t expected_generation, uint64_t expected_session_id);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE_STREAM */
