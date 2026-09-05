#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST

/**
 * @file hal_bluetooth_hid_host.h
 * @brief Generic Bluetooth Classic HID Host API.
 *
 * The API exposes copied report descriptors and raw HID reports. Device-class
 * interpretation belongs to profile adapters such as hal_gamepad.
 */

#include "hal/bluetooth/hal_bluetooth_classic.h"
#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum copied HID report-descriptor size in bytes. */
#define HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN 256u
/** Maximum copied HID report payload size in bytes. */
#define HAL_BLUETOOTH_HID_REPORT_MAX_LEN 32u

#ifndef HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH
/** Number of copied HID reports retained before overflow. */
#define HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH 16u
#endif

#if HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH < 2u
#error "HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH must be at least 2"
#endif

/** HID Host profile lifecycle and connection state. */
typedef enum {
  HAL_BLUETOOTH_HID_STATE_CLOSED = 0,
  HAL_BLUETOOTH_HID_STATE_READY,
  HAL_BLUETOOTH_HID_STATE_CONNECTING,
  HAL_BLUETOOTH_HID_STATE_CONNECTED,
  HAL_BLUETOOTH_HID_STATE_FAILED,
} hal_bluetooth_hid_state_t;

/** HID report direction/type encoded by the report transaction. */
typedef enum {
  HAL_BLUETOOTH_HID_REPORT_INPUT = 1,
  HAL_BLUETOOTH_HID_REPORT_OUTPUT = 2,
  HAL_BLUETOOTH_HID_REPORT_FEATURE = 3,
} hal_bluetooth_hid_report_type_t;

/** One copied, bounded HID report. */
typedef struct {
  hal_bluetooth_hid_report_type_t type;
  uint8_t report_id;
  uint8_t length;
  uint8_t data[HAL_BLUETOOTH_HID_REPORT_MAX_LEN];
} hal_bluetooth_hid_report_t;

/** HID connection, descriptor, and report-queue diagnostics. */
typedef struct {
  hal_bluetooth_hid_state_t state;
  hal_status_t last_status;
  hal_bluetooth_classic_address_t peer_address;
  uint32_t generation;
  uint32_t dropped_reports;
  size_t pending_reports;
  size_t descriptor_length;
  bool descriptor_available;
} hal_bluetooth_hid_info_t;

/** Incomplete implementation type for the opaque HID Host handle. */
typedef struct hal_bluetooth_hid_host_impl_s hal_bluetooth_hid_host_impl_t;
/** Opaque handle for one HID Host profile attached to a Classic manager. */
typedef hal_bluetooth_hid_host_impl_t *hal_bluetooth_hid_host_t;

/**
 * @brief Attach one HID Host profile to an open Classic manager.
 * @param classic Live Classic manager handle retained by the caller.
 * @param out_hid_host Receives the profile handle and is cleared on entry;
 * must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * manager, HAL_EBUSY when already attached, HAL_ENOMEM for allocation failure,
 * or a backend attach error.
 */
hal_status_t
hal_bluetooth_hid_host_open(hal_bluetooth_classic_t classic,
                            hal_bluetooth_hid_host_t *out_hid_host);

/**
 * @brief Close the HID Host profile without closing its Classic manager.
 * @param hid_host Live profile handle.
 * @return HAL_OK, HAL_EUNINIT for an invalid or stale handle, HAL_EBUSY during
 * another profile operation, HAL_ENOMEM, or a backend detach error.
 */
hal_status_t hal_bluetooth_hid_host_close(hal_bluetooth_hid_host_t hid_host);

/**
 * @brief Read HID connection state and queue diagnostics.
 * @param hid_host Live profile handle.
 * @param out_info Receives the snapshot; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EBUSY during another operation, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_hid_host_get_info(hal_bluetooth_hid_host_t hid_host,
                                hal_bluetooth_hid_info_t *out_info);

/**
 * @brief Start an outgoing HID connection to a discovered or saved peer.
 * @param hid_host Live profile handle.
 * @param address Nonzero peer address; must not be NULL.
 * @return HAL_OK when queued; HAL_EINVAL for invalid input, HAL_EUNINIT for an
 * invalid handle, HAL_EBUSY during another operation, HAL_ESTATE unless the
 * profile is ready, or a backend error.
 */
hal_status_t
hal_bluetooth_hid_host_connect(hal_bluetooth_hid_host_t hid_host,
                               const hal_bluetooth_classic_address_t *address);

/**
 * @brief Request asynchronous disconnection of the active HID link.
 * @param hid_host Live profile handle.
 * @return HAL_OK when queued, HAL_EUNINIT for an invalid handle, HAL_EBUSY
 * during another operation, HAL_ESTATE without an active connection, or a
 * backend error.
 */
hal_status_t
hal_bluetooth_hid_host_disconnect(hal_bluetooth_hid_host_t hid_host);

/**
 * @brief Copy the current report descriptor.
 * @param hid_host Live profile handle.
 * @param out_descriptor Destination buffer; must not be NULL.
 * @param capacity Destination capacity in bytes; must be nonzero.
 * @param out_length Receives the descriptor length and is cleared on entry;
 * must not be NULL.
 * @return HAL_OK, HAL_EINVAL for invalid output, HAL_EUNINIT for an invalid
 * handle, HAL_EAGAIN before a descriptor is available, HAL_EOVERFLOW for a
 * short destination, or HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_hid_host_descriptor(hal_bluetooth_hid_host_t hid_host,
                                  uint8_t *out_descriptor, size_t capacity,
                                  size_t *out_length);

/**
 * @brief Pop one copied Input, Output or Feature report.
 * @param hid_host Live profile handle.
 * @param out_report Receives the oldest retained report; must not be NULL.
 * @return HAL_OK, HAL_EINVAL for a NULL output, HAL_EUNINIT for an invalid
 * handle, HAL_EAGAIN when empty, HAL_EOVERFLOW once after dropped reports, or
 * HAL_ENOMEM.
 */
hal_status_t
hal_bluetooth_hid_host_report_next(hal_bluetooth_hid_host_t hid_host,
                                   hal_bluetooth_hid_report_t *out_report);

/**
 * @brief Send a complete Output or Feature report through the control channel.
 * @param hid_host Live profile handle.
 * @param report Copied report with a valid type and bounded length; must not be
 * NULL. Input reports cannot be sent.
 * @return HAL_OK when queued; HAL_EINVAL for invalid input, HAL_EUNINIT for an
 * invalid handle, HAL_EBUSY during another operation, HAL_ESTATE without a
 * connection, or a backend error.
 */
hal_status_t
hal_bluetooth_hid_host_report_send(hal_bluetooth_hid_host_t hid_host,
                                   const hal_bluetooth_hid_report_t *report);

/**
 * @brief Request an Input or Feature report through the control channel.
 * @param hid_host Live profile handle.
 * @param type HAL_BLUETOOTH_HID_REPORT_INPUT or
 * HAL_BLUETOOTH_HID_REPORT_FEATURE.
 * @param report_id Report identifier requested from the peer.
 * @return HAL_OK when queued; HAL_EINVAL for an unsupported type,
 * HAL_EUNINIT for an invalid handle, HAL_EBUSY during another operation,
 * HAL_ESTATE without a connection, or a backend error.
 */
hal_status_t
hal_bluetooth_hid_host_report_request(hal_bluetooth_hid_host_t hid_host,
                                      hal_bluetooth_hid_report_type_t type,
                                      uint8_t report_id);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_HID_HOST */
