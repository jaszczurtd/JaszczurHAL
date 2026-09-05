#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLE

/**
 * @file hal_ble.h
 * @brief Bluetooth Low Energy Peripheral and Observer API.
 *
 * The first release supports one Peripheral connection, legacy advertising,
 * passive legacy scanning, ATT MTU reporting, and bounded event/report queues.
 * All operations are nonblocking unless documented otherwise. HAL_OK means
 * that an asynchronous operation was accepted; its result is reported through
 * hal_ble_event_t.
 */

#include "hal/core/hal_status.h"
#include "hal/core/hal_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of a BLE device address in bytes. */
#define HAL_BLE_ADDRESS_LEN 6u
/** Buffer size required for a formatted BLE address and terminator. */
#define HAL_BLE_ADDRESS_TEXT_SIZE HAL_TEXT_MAC_STRING_SIZE
/** Maximum legacy advertising or scan-response payload size in bytes. */
#define HAL_BLE_LEGACY_ADV_MAX_DATA_LEN 31u
/** Default ATT MTU in bytes before negotiation. */
#define HAL_BLE_DEFAULT_ATT_MTU 23u
/** Sentinel value that never identifies a connection or advertising set. */
#define HAL_BLE_INVALID_HANDLE UINT32_C(0)
/** Minimum legacy advertising interval in 0.625 ms units. */
#define HAL_BLE_ADVERTISING_INTERVAL_MIN 0x0020u
/** Maximum legacy advertising interval in 0.625 ms units. */
#define HAL_BLE_ADVERTISING_INTERVAL_MAX 0x4000u
/** Minimum passive scan interval/window in 0.625 ms units. */
#define HAL_BLE_SCAN_INTERVAL_MIN 0x0004u
/** Maximum passive scan interval/window in 0.625 ms units. */
#define HAL_BLE_SCAN_INTERVAL_MAX 0x4000u

#ifndef HAL_BLE_EVENT_QUEUE_DEPTH
/** Number of copied BLE events retained before overflow. */
#define HAL_BLE_EVENT_QUEUE_DEPTH 8u
#endif

#ifndef HAL_BLE_SCAN_REPORT_QUEUE_DEPTH
/** Number of copied advertising reports retained before overflow. */
#define HAL_BLE_SCAN_REPORT_QUEUE_DEPTH 8u
#endif

#if HAL_BLE_EVENT_QUEUE_DEPTH < 2u
#error "HAL_BLE_EVENT_QUEUE_DEPTH must be at least 2"
#endif

#if HAL_BLE_SCAN_REPORT_QUEUE_DEPTH < 2u
#error "HAL_BLE_SCAN_REPORT_QUEUE_DEPTH must be at least 2"
#endif

/** Opaque, generation-checked handle for one Peripheral connection. */
typedef uint32_t hal_ble_connection_handle_t;
/** Opaque, generation-checked handle for the legacy advertising request. */
typedef uint32_t hal_ble_advertising_handle_t;

/** Address category reported by the controller. */
typedef enum {
  HAL_BLE_ADDRESS_PUBLIC = 0,
  HAL_BLE_ADDRESS_RANDOM = 1,
  HAL_BLE_ADDRESS_PUBLIC_IDENTITY = 2,
  HAL_BLE_ADDRESS_RANDOM_IDENTITY = 3,
  HAL_BLE_ADDRESS_UNKNOWN = 255
} hal_ble_address_type_t;

/** BLE device address in stack-independent byte order with its category. */
typedef struct {
  uint8_t bytes[HAL_BLE_ADDRESS_LEN];
  hal_ble_address_type_t type;
} hal_ble_address_t;

/** BLE subsystem lifecycle and active-role state. */
typedef enum {
  HAL_BLE_STATE_UNINITIALIZED = 0,
  HAL_BLE_STATE_STARTING,
  HAL_BLE_STATE_READY,
  HAL_BLE_STATE_ADVERTISING,
  HAL_BLE_STATE_CONNECTED,
  HAL_BLE_STATE_SCANNING,
  HAL_BLE_STATE_FAILED
} hal_ble_state_t;

/** Connectable legacy advertising parameters and copied payload. */
typedef struct {
  uint16_t interval_min; /**< Minimum interval in 0.625 ms units. */
  uint16_t interval_max; /**< Maximum interval in 0.625 ms units. */
  uint8_t data_length;
  uint8_t data[HAL_BLE_LEGACY_ADV_MAX_DATA_LEN];
} hal_ble_advertising_config_t;

/** Passive legacy scan timing and duplicate-filter policy. */
typedef struct {
  uint16_t interval; /**< Scan interval in 0.625 ms units. */
  uint16_t window;   /**< Scan window in 0.625 ms units. */
  bool filter_duplicates;
} hal_ble_scan_config_t;

/** Legacy advertising PDU category attached to a scan report. */
typedef enum {
  HAL_BLE_ADV_EVENT_CONNECTABLE_UNDIRECTED = 0,
  HAL_BLE_ADV_EVENT_CONNECTABLE_DIRECTED = 1,
  HAL_BLE_ADV_EVENT_SCANNABLE_UNDIRECTED = 2,
  HAL_BLE_ADV_EVENT_NON_CONNECTABLE_UNDIRECTED = 3,
  HAL_BLE_ADV_EVENT_SCAN_RESPONSE = 4,
  HAL_BLE_ADV_EVENT_UNKNOWN = 255
} hal_ble_advertising_event_type_t;

/** One copied legacy advertising or scan-response report. */
typedef struct {
  hal_ble_address_t address;
  hal_ble_advertising_event_type_t event_type;
  int8_t rssi;
  uint8_t data_length;
  uint8_t data[HAL_BLE_LEGACY_ADV_MAX_DATA_LEN];
} hal_ble_advertising_report_t;

/** Borrowed view of one parsed advertising-data field. */
typedef struct {
  uint8_t type;
  uint8_t data_length;
  const uint8_t *data;
} hal_ble_advertising_field_t;

/** Asynchronous BLE lifecycle and queue event category. */
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

/** One copied BLE event with connection and peer metadata when applicable. */
typedef struct {
  hal_ble_event_type_t type;
  hal_status_t status;
  hal_ble_connection_handle_t connection;
  hal_ble_advertising_handle_t advertising;
  hal_ble_address_t peer_address;
  uint16_t mtu;
  uint8_t disconnect_reason;
} hal_ble_event_t;

/**
 * @brief Receive one event while hal_ble_poll() is outside the radio lock.
 * @param event Borrowed event valid only for the duration of the callback;
 * never NULL.
 * @param context Value registered by hal_ble_set_event_callback(); may be NULL.
 */
typedef void (*hal_ble_event_callback_t)(const hal_ble_event_t *event,
                                         void *context);

/** BLE subsystem state, active handles, and bounded-queue diagnostics. */
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
  /** Current peer, or a zeroed address while no connection is active. */
  hal_ble_address_t peer_address;
} hal_ble_info_t;

/**
 * @brief Initialize the BLE host and controller.
 * @return HAL_OK on success or when already initialized; HAL_ENOMEM when the
 * runtime lock cannot be created, HAL_EBUSY during another lifecycle change,
 * HAL_ECONFIG for an incomplete backend, or a controller startup error.
 */
hal_status_t hal_ble_initialize(void);

/**
 * @brief Stop BLE and invalidate all connection and advertising handles.
 * @return HAL_OK on success, HAL_EUNINIT before initialization, HAL_EBUSY
 * during another operation, or a controller shutdown error.
 */
hal_status_t hal_ble_deinitialize(void);

/**
 * @brief Service the controller and dispatch callbacks outside the radio lock.
 * @return HAL_OK on success, HAL_EUNINIT before initialization, HAL_EBUSY
 * during a lifecycle operation, or a controller service error.
 */
hal_status_t hal_ble_poll(void);

/**
 * @brief Read a consistent subsystem snapshot.
 * @param out_info Receives the snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT before
 * initialization, or HAL_ENOMEM when the runtime lock cannot be created.
 */
hal_status_t hal_ble_get_info(hal_ble_info_t *out_info);

/**
 * @brief Read the local controller address after the ready event.
 * @param out_address Receives the address; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT before
 * initialization, HAL_EAGAIN while starting, HAL_EHW after startup failure,
 * or HAL_ENOMEM when the runtime lock cannot be created.
 */
hal_status_t hal_ble_get_local_address(hal_ble_address_t *out_address);

/**
 * @brief Format an address as `XX:XX:XX:XX:XX:XX`.
 * @param address Address to format; must not be NULL.
 * @param out Destination buffer; must not be NULL.
 * @param out_size Capacity including the terminator; at least
 * HAL_BLE_ADDRESS_TEXT_SIZE bytes.
 * @return HAL_OK, HAL_EINVAL for invalid input, or a formatting error.
 */
hal_status_t hal_ble_format_address(const hal_ble_address_t *address, char *out,
                                    size_t out_size);

/**
 * @brief Queue connectable legacy advertising with a copied payload.
 * @param config Advertising intervals and at most
 * HAL_BLE_LEGACY_ADV_MAX_DATA_LEN bytes; must not be NULL.
 * @param out_handle Receives the new handle; must not be NULL.
 * @return HAL_OK when queued; HAL_EINVAL for invalid input, HAL_EUNINIT before
 * initialization, HAL_EBUSY during another operation, or HAL_ESTATE when the
 * current BLE state cannot advertise.
 */
hal_status_t
hal_ble_advertising_start(const hal_ble_advertising_config_t *config,
                          hal_ble_advertising_handle_t *out_handle);

/**
 * @brief Stop the active or pending advertising request.
 * @param advertising Handle returned by hal_ble_advertising_start().
 * @return HAL_OK when queued; HAL_EINVAL for an invalid handle,
 * HAL_EUNINIT before initialization, HAL_EBUSY during another operation, or
 * HAL_ESTATE when advertising is not active.
 */
hal_status_t hal_ble_advertising_stop(hal_ble_advertising_handle_t advertising);

/**
 * @brief Queue disconnection of the current Peripheral link.
 * @param connection Current connection handle.
 * @return HAL_OK when queued; HAL_EINVAL for an invalid handle,
 * HAL_EUNINIT before initialization, HAL_EBUSY during another operation, or
 * HAL_ESTATE when no matching connection is active.
 */
hal_status_t hal_ble_disconnect(hal_ble_connection_handle_t connection);

/**
 * @brief Read the negotiated ATT MTU for the current connection.
 * @param connection Current connection handle.
 * @param out_mtu Receives the MTU; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid input, HAL_EUNINIT before
 * initialization, or HAL_ESTATE when the connection is not active.
 */
hal_status_t hal_ble_get_mtu(hal_ble_connection_handle_t connection,
                             uint16_t *out_mtu);

/**
 * @brief Start passive legacy scanning.
 * @param config Scan interval, window and duplicate policy; must not be NULL.
 * @return HAL_OK when queued; HAL_EINVAL for invalid timing, HAL_EUNINIT
 * before initialization, HAL_EBUSY during another operation, or HAL_ESTATE
 * while advertising, connected or already scanning.
 */
hal_status_t hal_ble_scan_start(const hal_ble_scan_config_t *config);

/**
 * @brief Stop active or pending passive scanning.
 * @return HAL_OK when queued, HAL_EUNINIT before initialization, HAL_EBUSY
 * during another operation, or HAL_ESTATE when scanning is not active.
 */
hal_status_t hal_ble_scan_stop(void);

/**
 * @brief Pop one copied scan report.
 * @param out_report Receives the oldest retained report; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT before
 * initialization, HAL_EAGAIN when empty, or HAL_EOVERFLOW once after reports
 * were dropped. Call again after HAL_EOVERFLOW to receive retained data.
 */
hal_status_t hal_ble_scan_report_next(hal_ble_advertising_report_t *out_report);

/**
 * @brief Parse the next length-prefixed AD field from a copied report.
 * @param report Copied report to parse; must not be NULL.
 * @param offset In/out byte offset; initialize to zero.
 * @param out_field Receives a borrowed view into @p report; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid pointers or offset, HAL_EAGAIN at the
 * end of the payload, or HAL_EIO for malformed AD data.
 */
hal_status_t
hal_ble_advertising_field_next(const hal_ble_advertising_report_t *report,
                               size_t *offset,
                               hal_ble_advertising_field_t *out_field);

/**
 * @brief Pop one copied event.
 * @param out_event Receives the oldest event; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT before
 * initialization, HAL_EAGAIN when empty, or HAL_EOVERFLOW once after drops.
 */
hal_status_t hal_ble_event_next(hal_ble_event_t *out_event);

/**
 * @brief Register the callback drained by hal_ble_poll().
 * @param callback Callback to register, or NULL to disable callbacks.
 * @param context Opaque value passed to @p callback; may be NULL.
 * @return HAL_OK, HAL_EUNINIT before initialization, or HAL_ENOMEM when the
 * runtime lock cannot be created.
 */
hal_status_t hal_ble_set_event_callback(hal_ble_event_callback_t callback,
                                        void *context);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLE */
