#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_BLE

/**
 * @file hal_ble.h
 * @brief Experimental Bluetooth Low Energy Peripheral and Observer API.
 *
 * The first release supports one Peripheral connection, legacy advertising,
 * passive legacy scanning, ATT MTU reporting, and bounded event/report queues.
 * All operations are nonblocking unless documented otherwise. HAL_OK means
 * that an asynchronous operation was accepted; its result is reported through
 * hal_ble_event_t.
 */

#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_BLE_MATURITY_EXPERIMENTAL 1
#define HAL_BLE_ADDRESS_LEN 6u
#define HAL_BLE_ADDRESS_TEXT_SIZE 18u
#define HAL_BLE_LEGACY_ADV_MAX_DATA_LEN 31u
#define HAL_BLE_DEFAULT_ATT_MTU 23u
#define HAL_BLE_INVALID_HANDLE UINT32_C(0)
#define HAL_BLE_ADVERTISING_INTERVAL_MIN 0x0020u
#define HAL_BLE_ADVERTISING_INTERVAL_MAX 0x4000u
#define HAL_BLE_SCAN_INTERVAL_MIN 0x0004u
#define HAL_BLE_SCAN_INTERVAL_MAX 0x4000u

#ifndef HAL_BLE_EVENT_QUEUE_DEPTH
#define HAL_BLE_EVENT_QUEUE_DEPTH 8u
#endif

#ifndef HAL_BLE_SCAN_REPORT_QUEUE_DEPTH
#define HAL_BLE_SCAN_REPORT_QUEUE_DEPTH 8u
#endif

#if HAL_BLE_EVENT_QUEUE_DEPTH < 2u
#error "HAL_BLE_EVENT_QUEUE_DEPTH must be at least 2"
#endif

#if HAL_BLE_SCAN_REPORT_QUEUE_DEPTH < 2u
#error "HAL_BLE_SCAN_REPORT_QUEUE_DEPTH must be at least 2"
#endif

typedef uint32_t hal_ble_connection_handle_t;
typedef uint32_t hal_ble_advertising_handle_t;

typedef enum {
  HAL_BLE_ADDRESS_PUBLIC = 0,
  HAL_BLE_ADDRESS_RANDOM = 1,
  HAL_BLE_ADDRESS_PUBLIC_IDENTITY = 2,
  HAL_BLE_ADDRESS_RANDOM_IDENTITY = 3,
  HAL_BLE_ADDRESS_UNKNOWN = 255
} hal_ble_address_type_t;

typedef struct {
  uint8_t bytes[HAL_BLE_ADDRESS_LEN];
  hal_ble_address_type_t type;
} hal_ble_address_t;

typedef enum {
  HAL_BLE_STATE_UNINITIALIZED = 0,
  HAL_BLE_STATE_STARTING,
  HAL_BLE_STATE_READY,
  HAL_BLE_STATE_ADVERTISING,
  HAL_BLE_STATE_CONNECTED,
  HAL_BLE_STATE_SCANNING,
  HAL_BLE_STATE_FAILED
} hal_ble_state_t;

typedef struct {
  uint16_t interval_min;
  uint16_t interval_max;
  uint8_t data_length;
  uint8_t data[HAL_BLE_LEGACY_ADV_MAX_DATA_LEN];
} hal_ble_advertising_config_t;

typedef struct {
  uint16_t interval;
  uint16_t window;
  bool filter_duplicates;
} hal_ble_scan_config_t;

typedef enum {
  HAL_BLE_ADV_EVENT_CONNECTABLE_UNDIRECTED = 0,
  HAL_BLE_ADV_EVENT_CONNECTABLE_DIRECTED = 1,
  HAL_BLE_ADV_EVENT_SCANNABLE_UNDIRECTED = 2,
  HAL_BLE_ADV_EVENT_NON_CONNECTABLE_UNDIRECTED = 3,
  HAL_BLE_ADV_EVENT_SCAN_RESPONSE = 4,
  HAL_BLE_ADV_EVENT_UNKNOWN = 255
} hal_ble_advertising_event_type_t;

typedef struct {
  hal_ble_address_t address;
  hal_ble_advertising_event_type_t event_type;
  int8_t rssi;
  uint8_t data_length;
  uint8_t data[HAL_BLE_LEGACY_ADV_MAX_DATA_LEN];
} hal_ble_advertising_report_t;

typedef struct {
  uint8_t type;
  uint8_t data_length;
  const uint8_t *data;
} hal_ble_advertising_field_t;

typedef enum {
  HAL_BLE_EVENT_CONTROLLER_READY = 0,
  HAL_BLE_EVENT_ADVERTISING_STARTED,
  HAL_BLE_EVENT_ADVERTISING_STOPPED,
  HAL_BLE_EVENT_CONNECTED,
  HAL_BLE_EVENT_DISCONNECTED,
  HAL_BLE_EVENT_MTU_UPDATED,
  HAL_BLE_EVENT_ERROR,
  HAL_BLE_EVENT_SCAN_STARTED,
  HAL_BLE_EVENT_SCAN_STOPPED,
  HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE
} hal_ble_event_type_t;

typedef struct {
  hal_ble_event_type_t type;
  hal_status_t status;
  hal_ble_connection_handle_t connection;
  hal_ble_advertising_handle_t advertising;
  hal_ble_address_t peer_address;
  uint16_t mtu;
  uint8_t disconnect_reason;
} hal_ble_event_t;

typedef void (*hal_ble_event_callback_t)(const hal_ble_event_t *event,
                                         void *context);

typedef struct {
  hal_ble_state_t state;
  hal_status_t last_status;
  hal_ble_address_t local_address;
  hal_ble_connection_handle_t connection;
  hal_ble_advertising_handle_t advertising;
  uint16_t mtu;
  uint32_t generation;
  uint32_t dropped_events;
  uint32_t dropped_scan_reports;
  size_t pending_scan_reports;
  bool advertising_requested;
  bool scan_requested;
} hal_ble_info_t;

/** Initialize the BLE host and controller. Idempotent after a successful call.
 */
hal_status_t hal_ble_initialize(void);

/** Stop BLE and invalidate all connection and advertising handles. */
hal_status_t hal_ble_deinitialize(void);

/** Service the controller and dispatch queued callbacks outside the radio lock.
 */
hal_status_t hal_ble_poll(void);

/** Read a consistent subsystem snapshot. */
hal_status_t hal_ble_get_info(hal_ble_info_t *out_info);

/** Read the local controller address after the ready event. */
hal_status_t hal_ble_get_local_address(hal_ble_address_t *out_address);

/** Format an address as XX:XX:XX:XX:XX:XX including the trailing NUL. */
hal_status_t hal_ble_format_address(const hal_ble_address_t *address, char *out,
                                    size_t out_size);

/** Queue connectable legacy advertising with a copied 31-byte payload. */
hal_status_t
hal_ble_advertising_start(const hal_ble_advertising_config_t *config,
                          hal_ble_advertising_handle_t *out_handle);

/** Stop the active or pending advertising request. */
hal_status_t hal_ble_advertising_stop(hal_ble_advertising_handle_t advertising);

/** Queue disconnection of the current Peripheral link. */
hal_status_t hal_ble_disconnect(hal_ble_connection_handle_t connection);

/** Read the negotiated ATT MTU for the current connection. */
hal_status_t hal_ble_get_mtu(hal_ble_connection_handle_t connection,
                             uint16_t *out_mtu);

/** Start passive legacy scanning. Advertising and connections must be idle. */
hal_status_t hal_ble_scan_start(const hal_ble_scan_config_t *config);

/** Stop active or pending passive scanning. */
hal_status_t hal_ble_scan_stop(void);

/**
 * Pop one copied scan report. HAL_EOVERFLOW acknowledges dropped reports;
 * call again to receive the oldest retained report.
 */
hal_status_t hal_ble_scan_report_next(hal_ble_advertising_report_t *out_report);

/**
 * Parse the next length-prefixed AD field from a copied report. Start with an
 * offset of zero. HAL_EAGAIN means end of payload and HAL_EIO malformed data.
 */
hal_status_t
hal_ble_advertising_field_next(const hal_ble_advertising_report_t *report,
                               size_t *offset,
                               hal_ble_advertising_field_t *out_field);

/** Pop one copied event, or return HAL_EAGAIN when the queue is empty. */
hal_status_t hal_ble_event_next(hal_ble_event_t *out_event);

/** Register one callback drained by hal_ble_poll(), or pass NULL to disable it.
 */
hal_status_t hal_ble_set_event_callback(hal_ble_event_callback_t callback,
                                        void *context);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE */
