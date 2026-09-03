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

#define HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN 256u
#define HAL_BLUETOOTH_HID_REPORT_MAX_LEN 32u

#ifndef HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH
#define HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH 16u
#endif

#if HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH < 2u
#error "HAL_BLUETOOTH_HID_REPORT_QUEUE_DEPTH must be at least 2"
#endif

typedef enum {
  HAL_BLUETOOTH_HID_STATE_CLOSED = 0,
  HAL_BLUETOOTH_HID_STATE_READY,
  HAL_BLUETOOTH_HID_STATE_CONNECTING,
  HAL_BLUETOOTH_HID_STATE_CONNECTED,
  HAL_BLUETOOTH_HID_STATE_FAILED,
} hal_bluetooth_hid_state_t;

typedef enum {
  HAL_BLUETOOTH_HID_REPORT_INPUT = 1,
  HAL_BLUETOOTH_HID_REPORT_OUTPUT = 2,
  HAL_BLUETOOTH_HID_REPORT_FEATURE = 3,
} hal_bluetooth_hid_report_type_t;

typedef struct {
  hal_bluetooth_hid_report_type_t type;
  uint8_t report_id;
  uint8_t length;
  uint8_t data[HAL_BLUETOOTH_HID_REPORT_MAX_LEN];
} hal_bluetooth_hid_report_t;

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

typedef struct hal_bluetooth_hid_host_impl_s hal_bluetooth_hid_host_impl_t;
typedef hal_bluetooth_hid_host_impl_t *hal_bluetooth_hid_host_t;

/** Attach one HID Host profile to an open Classic manager. */
hal_status_t
hal_bluetooth_hid_host_open(hal_bluetooth_classic_t classic,
                            hal_bluetooth_hid_host_t *out_hid_host);

/** Close the HID Host profile without closing its Classic manager. */
hal_status_t hal_bluetooth_hid_host_close(hal_bluetooth_hid_host_t hid_host);

/** Read HID connection state and queue diagnostics. */
hal_status_t
hal_bluetooth_hid_host_get_info(hal_bluetooth_hid_host_t hid_host,
                                hal_bluetooth_hid_info_t *out_info);

/** Start an outgoing HID connection to a discovered or saved peer. */
hal_status_t
hal_bluetooth_hid_host_connect(hal_bluetooth_hid_host_t hid_host,
                               const hal_bluetooth_classic_address_t *address);

/** Request asynchronous disconnection of the active HID link. */
hal_status_t
hal_bluetooth_hid_host_disconnect(hal_bluetooth_hid_host_t hid_host);

/** Copy the current report descriptor. */
hal_status_t
hal_bluetooth_hid_host_descriptor(hal_bluetooth_hid_host_t hid_host,
                                  uint8_t *out_descriptor, size_t capacity,
                                  size_t *out_length);

/** Pop one copied Input, Output or Feature report. */
hal_status_t
hal_bluetooth_hid_host_report_next(hal_bluetooth_hid_host_t hid_host,
                                   hal_bluetooth_hid_report_t *out_report);

/** Send a complete Output or Feature report through the control channel. */
hal_status_t
hal_bluetooth_hid_host_report_send(hal_bluetooth_hid_host_t hid_host,
                                   const hal_bluetooth_hid_report_t *report);

/** Request an Input or Feature report through the control channel. */
hal_status_t
hal_bluetooth_hid_host_report_request(hal_bluetooth_hid_host_t hid_host,
                                      hal_bluetooth_hid_report_type_t type,
                                      uint8_t report_id);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_BLUETOOTH_HID_HOST */
