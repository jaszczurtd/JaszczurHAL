#pragma once

#include "hal/hal_ble.h"
#include "hal/hal_status.h"

#ifdef HAL_ENABLE_BLE_STREAM
#include "hal/hal_ble_stream.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_BLE_BACKEND_EVENT_READY = 0,
  JH_BLE_BACKEND_EVENT_ADVERTISING_STARTED,
  JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED,
  JH_BLE_BACKEND_EVENT_CONNECTED,
  JH_BLE_BACKEND_EVENT_DISCONNECTED,
  JH_BLE_BACKEND_EVENT_MTU_UPDATED,
  JH_BLE_BACKEND_EVENT_SCAN_STARTED,
  JH_BLE_BACKEND_EVENT_SCAN_STOPPED,
  JH_BLE_BACKEND_EVENT_ADVERTISING_REPORT,
  JH_BLE_BACKEND_EVENT_ERROR
#ifdef HAL_ENABLE_BLE_STREAM
  ,
  JH_BLE_BACKEND_EVENT_STREAM_WRITE,
  JH_BLE_BACKEND_EVENT_STREAM_SUBSCRIPTION,
  JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND
#endif
} jh_ble_backend_event_type_t;

typedef struct {
  jh_ble_backend_event_type_t type;
  hal_status_t status;
  uint16_t native_connection;
  hal_ble_address_t address;
  hal_ble_advertising_report_t advertising_report;
  uint16_t mtu;
  uint8_t disconnect_reason;
  bool fatal;
#ifdef HAL_ENABLE_BLE_STREAM
  /* Copied RX frame; valid for JH_BLE_BACKEND_EVENT_STREAM_WRITE. */
  uint8_t stream_frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  uint8_t stream_frame_length;
  bool stream_subscribed;
#endif
} jh_ble_backend_event_t;

typedef void (*jh_ble_backend_event_fn)(void *context,
                                        const jh_ble_backend_event_t *event);

typedef struct {
  void *context;
  hal_status_t (*start)(void *context, jh_ble_backend_event_fn event_handler,
                        void *event_context);
  hal_status_t (*stop)(void *context);
  hal_status_t (*service)(void *context);
  hal_status_t (*advertising_start)(void *context,
                                    const hal_ble_advertising_config_t *config);
  hal_status_t (*advertising_stop)(void *context);
  hal_status_t (*disconnect)(void *context, uint16_t native_connection);
  hal_status_t (*scan_start)(void *context,
                             const hal_ble_scan_config_t *config);
  hal_status_t (*scan_stop)(void *context);
#ifdef HAL_ENABLE_BLE_STREAM
  /* Publish one TX frame. HAL_EAGAIN asks the caller to retry after
     JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND. */
  hal_status_t (*stream_notify)(void *context, uint16_t native_connection,
                                const uint8_t *frame, size_t length);
  /* Values served from the read-only version and capabilities
     characteristics. */
  hal_status_t (*stream_publish)(void *context, uint8_t protocol_version,
                                 uint16_t capabilities);
  /* Deactivate stream GATT access and cancel an accepted notification. */
  hal_status_t (*stream_unpublish)(void *context);
#endif
} jh_ble_backend_t;

const jh_ble_backend_t *jh_ble_backend_instance(void);

#ifdef __cplusplus
}
#endif
