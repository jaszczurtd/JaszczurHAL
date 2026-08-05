#pragma once

#include "hal/hal_ble.h"
#include "hal/hal_status.h"

#include <stdbool.h>
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
} jh_ble_backend_t;

const jh_ble_backend_t *jh_ble_backend_instance(void);

#ifdef __cplusplus
}
#endif
