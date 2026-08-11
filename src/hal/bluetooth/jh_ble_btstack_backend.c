#include "jh_ble_backend.h"

#if defined(JH_BLUETOOTH_PUBLIC_BLE)

#include "btstack.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"
#include "jh_ble_controller.h"
#include "jh_ble_peripheral_gatt.h"
#include "jh_ble_stream_session.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "jh_btstack_run_loop.h"

#include <stddef.h>
#include <string.h>

typedef struct {
  hal_mutex_t mutex;
  const jh_ble_controller_t *controller;
  jh_ble_backend_event_fn event_handler;
  void *event_context;
  hal_ble_advertising_config_t advertising;
  hal_ble_scan_config_t scan;
  uint16_t connection;
  uint16_t disconnect_connection;
  bool started;
  bool ready;
  bool connected;
  bool advertising_requested;
  bool advertising_active;
  bool advertising_pending;
  bool scan_requested;
  bool scan_active;
  bool scan_pending;
  bool disconnect_pending;
  bool bootstrapping;
  bool stopping;
  bool shutdown_requested;
  bool shutdown_complete;
  bool hci_handler_registered;
  bool faulted;
#ifdef HAL_ENABLE_BLE_STREAM
  uint8_t stream_version;
  uint16_t stream_capabilities;
  uint16_t stream_mtu;
  bool stream_published;
  bool stream_subscribed;
  bool stream_waiting_can_send;
  uint8_t stream_pending_frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t stream_pending_frame_length;
#endif
} jh_ble_btstack_state_t;

static jh_ble_btstack_state_t s_ble;
static btstack_packet_callback_registration_t s_hci_events;

enum {
  JH_BLE_ADV_TYPE_CONNECTABLE_UNDIRECTED = 0u,
  JH_BLE_ADV_CHANNEL_MAP_ALL = 0x07u,
  JH_BLE_ADV_FILTER_POLICY_ALL = 0u,
  JH_BLE_SCAN_TYPE_PASSIVE = 0u,
  JH_BLE_SCAN_FILTER_POLICY_ALL = 0u,
};

static hal_mutex_t backend_mutex(void) {
  return jh_hal_mutex_create_once(&s_ble.mutex);
}

static void emit_event(const jh_ble_backend_event_t *event) {
  jh_ble_backend_event_fn handler = NULL;
  void *context = NULL;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL || event == NULL) {
    return;
  }
  hal_mutex_lock(mutex);
  handler = s_ble.event_handler;
  context = s_ble.event_context;
  hal_mutex_unlock(mutex);
  if (handler != NULL) {
    handler(context, event);
  }
}

static void emit_error(hal_status_t status, bool fatal) {
  const jh_ble_backend_event_t event = {
      .type = JH_BLE_BACKEND_EVENT_ERROR,
      .status = status,
      .fatal = fatal,
  };
  emit_event(&event);
}

static hal_ble_address_type_t address_type_from_btstack(uint8_t type) {
  switch (type) {
  case BD_ADDR_TYPE_LE_PUBLIC:
    return HAL_BLE_ADDRESS_PUBLIC;
  case BD_ADDR_TYPE_LE_RANDOM:
    return HAL_BLE_ADDRESS_RANDOM;
  case BD_ADDR_TYPE_LE_PUBLIC_IDENTITY:
    return HAL_BLE_ADDRESS_PUBLIC_IDENTITY;
  case BD_ADDR_TYPE_LE_RANDOM_IDENTITY:
    return HAL_BLE_ADDRESS_RANDOM_IDENTITY;
  default:
    return HAL_BLE_ADDRESS_UNKNOWN;
  }
}

static hal_ble_advertising_event_type_t
advertising_event_type_from_btstack(uint8_t type) {
  switch (type) {
  case 0u:
    return HAL_BLE_ADV_EVENT_CONNECTABLE_UNDIRECTED;
  case 1u:
    return HAL_BLE_ADV_EVENT_CONNECTABLE_DIRECTED;
  case 2u:
    return HAL_BLE_ADV_EVENT_SCANNABLE_UNDIRECTED;
  case 3u:
    return HAL_BLE_ADV_EVENT_NON_CONNECTABLE_UNDIRECTED;
  case 4u:
    return HAL_BLE_ADV_EVENT_SCAN_RESPONSE;
  default:
    return HAL_BLE_ADV_EVENT_UNKNOWN;
  }
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }

  const uint8_t event_type = hci_event_packet_get_type(packet);
  if (event_type == GAP_EVENT_ADVERTISING_REPORT) {
    const uint8_t data_length =
        gap_event_advertising_report_get_data_length(packet);
    if (data_length > HAL_BLE_LEGACY_ADV_MAX_DATA_LEN) {
      emit_error(HAL_EOVERFLOW, false);
      return;
    }
    bd_addr_t address = {0u};
    gap_event_advertising_report_get_address(packet, address);
    jh_ble_backend_event_t event = {
        .type = JH_BLE_BACKEND_EVENT_ADVERTISING_REPORT,
        .status = HAL_OK,
    };
    event.advertising_report.address.type = address_type_from_btstack(
        gap_event_advertising_report_get_address_type(packet));
    memcpy(event.advertising_report.address.bytes, address,
           sizeof(event.advertising_report.address.bytes));
    event.advertising_report.event_type = advertising_event_type_from_btstack(
        gap_event_advertising_report_get_advertising_event_type(packet));
    event.advertising_report.rssi =
        (int8_t)gap_event_advertising_report_get_rssi(packet);
    event.advertising_report.data_length = data_length;
    memcpy(event.advertising_report.data,
           gap_event_advertising_report_get_data(packet), data_length);
    emit_event(&event);
    return;
  }
  if (event_type == BTSTACK_EVENT_STATE &&
      btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
    bd_addr_t address = {0u};
    gap_local_bd_addr(address);
    hal_mutex_t mutex = backend_mutex();
    if (mutex == NULL) {
      emit_error(HAL_ENOMEM, true);
      return;
    }
    hal_mutex_lock(mutex);
    s_ble.ready = true;
    hal_mutex_unlock(mutex);
    jh_ble_backend_event_t event = {
        .type = JH_BLE_BACKEND_EVENT_READY,
        .status = HAL_OK,
    };
    memcpy(event.address.bytes, address, sizeof(event.address.bytes));
    event.address.type = HAL_BLE_ADDRESS_PUBLIC;
    emit_event(&event);
    return;
  }

  if (event_type == HCI_EVENT_LE_META &&
      hci_event_le_meta_get_subevent_code(packet) ==
          HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
    const uint8_t status =
        hci_subevent_le_connection_complete_get_status(packet);
    if (status != ERROR_CODE_SUCCESS) {
      emit_error(HAL_EIO, false);
      return;
    }
    bd_addr_t peer = {0u};
    hci_subevent_le_connection_complete_get_peer_address(packet, peer);
    const uint16_t connection =
        hci_subevent_le_connection_complete_get_connection_handle(packet);
    const uint8_t peer_type =
        hci_subevent_le_connection_complete_get_peer_address_type(packet);
    hal_mutex_t mutex = backend_mutex();
    if (mutex == NULL) {
      emit_error(HAL_ENOMEM, true);
      return;
    }
    hal_mutex_lock(mutex);
    s_ble.connection = connection;
    s_ble.connected = true;
    s_ble.advertising_active = false;
#ifdef HAL_ENABLE_BLE_STREAM
    s_ble.stream_mtu = ATT_DEFAULT_MTU;
    s_ble.stream_subscribed = false;
    s_ble.stream_waiting_can_send = false;
    memset(s_ble.stream_pending_frame, 0, sizeof(s_ble.stream_pending_frame));
    s_ble.stream_pending_frame_length = 0u;
#endif
    hal_mutex_unlock(mutex);
    jh_ble_backend_event_t event = {
        .type = JH_BLE_BACKEND_EVENT_CONNECTED,
        .status = HAL_OK,
        .native_connection = connection,
    };
    memcpy(event.address.bytes, peer, sizeof(event.address.bytes));
    event.address.type = address_type_from_btstack(peer_type);
    emit_event(&event);
    return;
  }

  if (event_type == HCI_EVENT_DISCONNECTION_COMPLETE &&
      hci_event_disconnection_complete_get_status(packet) ==
          ERROR_CODE_SUCCESS) {
    const uint16_t connection =
        hci_event_disconnection_complete_get_connection_handle(packet);
    const uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
    hal_mutex_t mutex = backend_mutex();
    if (mutex == NULL) {
      emit_error(HAL_ENOMEM, true);
      return;
    }
    hal_mutex_lock(mutex);
    if (connection == s_ble.connection) {
      s_ble.connection = HCI_CON_HANDLE_INVALID;
      s_ble.connected = false;
#ifdef HAL_ENABLE_BLE_STREAM
      s_ble.stream_mtu = 0u;
      s_ble.stream_subscribed = false;
      s_ble.stream_waiting_can_send = false;
      memset(s_ble.stream_pending_frame, 0, sizeof(s_ble.stream_pending_frame));
      s_ble.stream_pending_frame_length = 0u;
#endif
      if (s_ble.advertising_requested) {
        s_ble.advertising_pending = true;
      }
    }
    hal_mutex_unlock(mutex);
    const jh_ble_backend_event_t event = {
        .type = JH_BLE_BACKEND_EVENT_DISCONNECTED,
        .status = HAL_OK,
        .native_connection = connection,
        .disconnect_reason = reason,
    };
    emit_event(&event);
  }
}

static void att_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }
  switch (hci_event_packet_get_type(packet)) {
  case ATT_EVENT_MTU_EXCHANGE_COMPLETE: {
    const uint16_t connection =
        att_event_mtu_exchange_complete_get_handle(packet);
    const uint16_t mtu = att_event_mtu_exchange_complete_get_MTU(packet);
#ifdef HAL_ENABLE_BLE_STREAM
    hal_mutex_t mutex = backend_mutex();
    if (mutex == NULL) {
      emit_error(HAL_ENOMEM, true);
      break;
    }
    hal_mutex_lock(mutex);
    if (s_ble.connection == connection) {
      s_ble.stream_mtu = mtu;
    }
    hal_mutex_unlock(mutex);
#endif
    const jh_ble_backend_event_t event = {
        .type = JH_BLE_BACKEND_EVENT_MTU_UPDATED,
        .status = HAL_OK,
        .native_connection = connection,
        .mtu = mtu,
    };
    emit_event(&event);
    break;
  }
#ifdef HAL_ENABLE_BLE_STREAM
  case ATT_EVENT_CAN_SEND_NOW: {
    hal_mutex_t mutex = backend_mutex();
    if (mutex == NULL) {
      emit_error(HAL_ENOMEM, true);
      break;
    }
    hal_mutex_lock(mutex);
    s_ble.stream_waiting_can_send = false;
    hal_mutex_unlock(mutex);
    break;
  }
#endif
  default:
    break;
  }
}

#ifdef HAL_ENABLE_BLE_STREAM
/* Read-only profile metadata is served without a session. */
static uint16_t att_read_callback(hci_con_handle_t connection_handle,
                                  uint16_t att_handle, uint16_t offset,
                                  uint8_t *buffer, uint16_t buffer_size) {
  (void)connection_handle;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return 0u;
  }
  hal_mutex_lock(mutex);
  const bool published = s_ble.stream_published;
  const uint8_t version = s_ble.stream_version;
  const uint16_t stream_capabilities = s_ble.stream_capabilities;
  hal_mutex_unlock(mutex);
  if (!published) {
    return 0u;
  }
  if (att_handle ==
      ATT_CHARACTERISTIC_B7CE0004_3C13_4FE2_801F_D71BDAB1369B_01_VALUE_HANDLE) {
    return att_read_callback_handle_blob(&version, sizeof(version), offset,
                                         buffer, buffer_size);
  }
  if (att_handle ==
      ATT_CHARACTERISTIC_B7CE0005_3C13_4FE2_801F_D71BDAB1369B_01_VALUE_HANDLE) {
    uint8_t capabilities[2];
    little_endian_store_16(capabilities, 0, stream_capabilities);
    return att_read_callback_handle_blob(capabilities, sizeof(capabilities),
                                         offset, buffer, buffer_size);
  }
  return 0u;
}

static int att_write_callback(hci_con_handle_t connection_handle,
                              uint16_t att_handle, uint16_t transaction_mode,
                              uint16_t offset, uint8_t *buffer,
                              uint16_t buffer_size) {
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return ATT_ERROR_INSUFFICIENT_RESOURCES;
  }
  hal_mutex_lock(mutex);
  const bool published = s_ble.stream_published;
  hal_mutex_unlock(mutex);
  if (!published) {
    return ATT_ERROR_WRITE_REQUEST_REJECTED;
  }
  if (transaction_mode != ATT_TRANSACTION_MODE_NONE) {
    return ATT_ERROR_WRITE_REQUEST_REJECTED;
  }
  if (att_handle ==
      ATT_CHARACTERISTIC_B7CE0003_3C13_4FE2_801F_D71BDAB1369B_01_CLIENT_CONFIGURATION_HANDLE) {
    if (buffer == NULL || buffer_size < 2u) {
      return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    }
    const bool subscribed =
        little_endian_read_16(buffer, 0) ==
        GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
    hal_mutex_lock(mutex);
    s_ble.stream_subscribed = subscribed;
    if (!subscribed) {
      s_ble.stream_waiting_can_send = false;
      memset(s_ble.stream_pending_frame, 0, sizeof(s_ble.stream_pending_frame));
      s_ble.stream_pending_frame_length = 0u;
    }
    const uint16_t mtu = s_ble.stream_mtu;
    hal_mutex_unlock(mutex);
    jh_ble_backend_event_t event = {0};
    event.type = JH_BLE_BACKEND_EVENT_STREAM_SUBSCRIPTION;
    event.status = HAL_OK;
    event.native_connection = connection_handle;
    event.mtu = mtu;
    event.stream_subscribed = subscribed;
    emit_event(&event);
    return 0;
  }
  if (att_handle ==
      ATT_CHARACTERISTIC_B7CE0002_3C13_4FE2_801F_D71BDAB1369B_01_VALUE_HANDLE) {
    if (offset != 0u) {
      return ATT_ERROR_INVALID_OFFSET;
    }
    if (buffer == NULL || buffer_size == 0u ||
        buffer_size > HAL_BLE_STREAM_MAX_FRAME_LEN) {
      return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    }
    const uint16_t mtu = att_server_get_mtu(connection_handle);
    if (buffer[0] == HAL_BLE_STREAM_PROTOCOL_VERSION &&
        buffer_size >= HAL_BLE_STREAM_FRAME_HEADER_LEN &&
        buffer[1] == JH_BLE_STREAM_FRAME_HELLO &&
        mtu < HAL_BLE_STREAM_MIN_ATT_MTU) {
      return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    }
    jh_ble_backend_event_t event = {0};
    event.type = JH_BLE_BACKEND_EVENT_STREAM_WRITE;
    event.status = HAL_OK;
    event.native_connection = connection_handle;
    event.mtu = mtu;
    memcpy(event.stream_frame, buffer, buffer_size);
    event.stream_frame_length = (uint8_t)buffer_size;
    emit_event(&event);
    return 0;
  }
  return 0;
}
#endif

static void teardown_btstack(void) {
  if (s_ble.scan_active) {
    gap_stop_scan();
  }
  if (s_ble.advertising_active) {
    gap_advertisements_enable(0);
  }
  if (s_ble.hci_handler_registered) {
    hci_remove_event_handler(&s_hci_events);
    s_ble.hci_handler_registered = false;
  }
  att_server_register_packet_handler(NULL);
  att_server_deinit();
  sm_deinit();
  l2cap_deinit();
  hci_close();
  btstack_memory_deinit();
  jh_btstack_run_loop_deinit();
  s_ble.started = false;
  s_ble.ready = false;
  s_ble.connected = false;
  s_ble.advertising_active = false;
  s_ble.advertising_pending = false;
  s_ble.scan_requested = false;
  s_ble.scan_active = false;
  s_ble.scan_pending = false;
  s_ble.disconnect_pending = false;
  s_ble.connection = HCI_CON_HANDLE_INVALID;
#ifdef HAL_ENABLE_BLE_STREAM
  s_ble.stream_subscribed = false;
  s_ble.stream_mtu = 0u;
  s_ble.stream_waiting_can_send = false;
  memset(s_ble.stream_pending_frame, 0, sizeof(s_ble.stream_pending_frame));
  s_ble.stream_pending_frame_length = 0u;
#endif
}

#ifdef HAL_ENABLE_BLE_STREAM
static hal_status_t stream_send_status(uint8_t status) {
  if (status == ERROR_CODE_SUCCESS) {
    return HAL_OK;
  }
  if (status == BTSTACK_ACL_BUFFERS_FULL) {
    return HAL_EAGAIN;
  }
  if (status == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER) {
    return HAL_ENOENT;
  }
  if (status == ERROR_CODE_COMMAND_DISALLOWED) {
    return HAL_EBUSY;
  }
  return HAL_EIO;
}

/* BTstack and L2CAP are touched only from the shared radio service. */
static void service_stream_notification_under_radio_lock(void) {
  uint8_t frame[HAL_BLE_STREAM_MAX_FRAME_LEN];
  size_t length = 0u;
  uint16_t connection = HCI_CON_HANDLE_INVALID;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    emit_error(HAL_ENOMEM, true);
    return;
  }

  hal_mutex_lock(mutex);
  const bool ready = s_ble.started && !s_ble.faulted && s_ble.connected &&
                     s_ble.stream_subscribed &&
                     !s_ble.stream_waiting_can_send &&
                     s_ble.stream_pending_frame_length != 0u;
  if (ready) {
    connection = s_ble.connection;
    length = s_ble.stream_pending_frame_length;
    memcpy(frame, s_ble.stream_pending_frame, length);
  }
  hal_mutex_unlock(mutex);
  if (!ready) {
    return;
  }

  const uint8_t bt_status = att_server_notify(
      connection,
      ATT_CHARACTERISTIC_B7CE0003_3C13_4FE2_801F_D71BDAB1369B_01_VALUE_HANDLE,
      frame, (uint16_t)length);
  memset(frame, 0, sizeof(frame));
  const hal_status_t status = stream_send_status(bt_status);
  if (status == HAL_EAGAIN) {
    hal_mutex_lock(mutex);
    s_ble.stream_waiting_can_send = true;
    hal_mutex_unlock(mutex);
    att_server_request_can_send_now_event(connection);
    return;
  }

  hal_mutex_lock(mutex);
  memset(s_ble.stream_pending_frame, 0, sizeof(s_ble.stream_pending_frame));
  s_ble.stream_pending_frame_length = 0u;
  s_ble.stream_waiting_can_send = false;
  hal_mutex_unlock(mutex);
  const jh_ble_backend_event_t event = {
      .type = JH_BLE_BACKEND_EVENT_STREAM_CAN_SEND,
      .status = status,
      .native_connection = connection,
  };
  emit_event(&event);
}
#endif

static hal_status_t service_under_radio_lock(void *context) {
  (void)context;
  if (__atomic_load_n(&s_ble.bootstrapping, __ATOMIC_ACQUIRE)) {
    return HAL_OK;
  }
  if (__atomic_load_n(&s_ble.shutdown_requested, __ATOMIC_ACQUIRE)) {
    teardown_btstack();
    __atomic_store_n(&s_ble.shutdown_complete, true, __ATOMIC_RELEASE);
    return HAL_OK;
  }
  if (__atomic_load_n(&s_ble.faulted, __ATOMIC_ACQUIRE)) {
    return HAL_EHW;
  }
  if (!s_ble.started) {
    return HAL_OK;
  }

  hal_status_t status = jh_btstack_run_loop_service_once(NULL);
  if (status != HAL_OK) {
    return status;
  }

  hal_ble_advertising_config_t advertising = {0};
  uint16_t disconnect_connection = HCI_CON_HANDLE_INVALID;
  bool apply_advertising = false;
  bool enable_advertising = false;
  hal_ble_scan_config_t scan = {0};
  bool apply_scan = false;
  bool enable_scan = false;
  bool apply_disconnect = false;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_ble.disconnect_pending) {
    apply_disconnect = true;
    disconnect_connection = s_ble.disconnect_connection;
    s_ble.disconnect_pending = false;
  }
  if (s_ble.advertising_pending &&
      (!s_ble.advertising_requested || (s_ble.ready && !s_ble.connected))) {
    apply_advertising = true;
    enable_advertising = s_ble.advertising_requested;
    advertising = s_ble.advertising;
    s_ble.advertising_pending = false;
  }
  if (s_ble.scan_pending && (!s_ble.scan_requested || s_ble.ready)) {
    apply_scan = true;
    enable_scan = s_ble.scan_requested;
    scan = s_ble.scan;
    s_ble.scan_pending = false;
  }
  hal_mutex_unlock(mutex);

  if (apply_disconnect) {
    const uint8_t gap_status = gap_disconnect(disconnect_connection);
    if (gap_status != ERROR_CODE_SUCCESS) {
      emit_error(gap_status == ERROR_CODE_COMMAND_DISALLOWED ? HAL_ESTATE
                                                             : HAL_EIO,
                 false);
    }
  }
  if (apply_advertising) {
    if (enable_advertising) {
      bd_addr_t direct_address = {0u};
      gap_advertisements_set_params(
          advertising.interval_min, advertising.interval_max,
          JH_BLE_ADV_TYPE_CONNECTABLE_UNDIRECTED, BD_ADDR_TYPE_LE_PUBLIC,
          direct_address, JH_BLE_ADV_CHANNEL_MAP_ALL,
          JH_BLE_ADV_FILTER_POLICY_ALL);
      gap_advertisements_set_data(advertising.data_length, advertising.data);
    }
    gap_advertisements_enable(enable_advertising ? 1 : 0);
    hal_mutex_lock(mutex);
    s_ble.advertising_active = enable_advertising;
    hal_mutex_unlock(mutex);
    const jh_ble_backend_event_t event = {
        .type = enable_advertising ? JH_BLE_BACKEND_EVENT_ADVERTISING_STARTED
                                   : JH_BLE_BACKEND_EVENT_ADVERTISING_STOPPED,
        .status = HAL_OK,
    };
    emit_event(&event);
  }
  if (apply_scan) {
    if (enable_scan) {
      gap_set_scan_params(JH_BLE_SCAN_TYPE_PASSIVE, scan.interval, scan.window,
                          JH_BLE_SCAN_FILTER_POLICY_ALL);
      gap_set_scan_duplicate_filter(scan.filter_duplicates);
      gap_start_scan();
    } else {
      gap_stop_scan();
    }
    hal_mutex_lock(mutex);
    s_ble.scan_active = enable_scan;
    hal_mutex_unlock(mutex);
    const jh_ble_backend_event_t event = {
        .type = enable_scan ? JH_BLE_BACKEND_EVENT_SCAN_STARTED
                            : JH_BLE_BACKEND_EVENT_SCAN_STOPPED,
        .status = HAL_OK,
    };
    emit_event(&event);
  }
#ifdef HAL_ENABLE_BLE_STREAM
  service_stream_notification_under_radio_lock();
#endif
  return HAL_OK;
}

static void controller_invalidated(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  jh_btstack_run_loop_invalidate(NULL, generation);
  jh_btstack_cyw43_transport_invalidate();
  if (__atomic_load_n(&s_ble.stopping, __ATOMIC_ACQUIRE)) {
    return;
  }
  __atomic_store_n(&s_ble.faulted, true, __ATOMIC_RELEASE);
  emit_error(HAL_EHW, true);
}

static hal_status_t backend_start(void *context,
                                  jh_ble_backend_event_fn event_handler,
                                  void *event_context) {
  (void)context;
  if (event_handler == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (s_ble.started || s_ble.bootstrapping) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_ble.event_handler = event_handler;
  s_ble.event_context = event_context;
  s_ble.controller = jh_ble_controller_backend();
  s_ble.connection = HCI_CON_HANDLE_INVALID;
  s_ble.bootstrapping = true;
  s_ble.stopping = false;
  s_ble.shutdown_requested = false;
  s_ble.shutdown_complete = false;
  s_ble.faulted = false;
  s_ble.scan_requested = false;
  s_ble.scan_active = false;
  s_ble.scan_pending = false;
  hal_mutex_unlock(mutex);
  if (s_ble.controller == NULL || s_ble.controller->start == NULL ||
      s_ble.controller->stop == NULL || s_ble.controller->service == NULL) {
    s_ble.bootstrapping = false;
    return HAL_ECONFIG;
  }

  hal_status_t status = s_ble.controller->start(s_ble.controller->context,
                                                service_under_radio_lock, NULL,
                                                controller_invalidated, NULL);
  if (status != HAL_OK) {
    s_ble.bootstrapping = false;
    return status;
  }

  btstack_memory_init();
  status = jh_btstack_run_loop_init();
  if (status != HAL_OK) {
    (void)s_ble.controller->stop(s_ble.controller->context);
    s_ble.bootstrapping = false;
    return status;
  }
  hci_init(jh_btstack_cyw43_hci_transport_instance(), NULL);
  l2cap_init();
  sm_init();
#ifdef HAL_ENABLE_BLE_STREAM
  att_server_init(profile_data, att_read_callback, att_write_callback);
#else
  att_server_init(profile_data, NULL, NULL);
#endif
  att_server_register_packet_handler(att_packet_handler);
  memset(&s_hci_events, 0, sizeof(s_hci_events));
  s_hci_events.callback = packet_handler;
  hci_add_event_handler(&s_hci_events);
  s_ble.hci_handler_registered = true;
  if (hci_power_control(HCI_POWER_ON) != 0) {
    teardown_btstack();
    (void)s_ble.controller->stop(s_ble.controller->context);
    s_ble.bootstrapping = false;
    return HAL_EIO;
  }
  s_ble.started = true;
  __atomic_store_n(&s_ble.bootstrapping, false, __ATOMIC_RELEASE);
  return HAL_OK;
}

static hal_status_t backend_stop(void *context) {
  (void)context;
  if (s_ble.controller == NULL) {
    return HAL_OK;
  }
  __atomic_store_n(&s_ble.stopping, true, __ATOMIC_RELEASE);
  __atomic_store_n(&s_ble.shutdown_requested, true, __ATOMIC_RELEASE);
  hal_status_t status = s_ble.controller->service(s_ble.controller->context);
  if (status == HAL_OK &&
      !__atomic_load_n(&s_ble.shutdown_complete, __ATOMIC_ACQUIRE)) {
    status = HAL_EIO;
  }
  const hal_status_t stop_status =
      s_ble.controller->stop(s_ble.controller->context);
  if (status == HAL_OK) {
    status = stop_status;
  }
  s_ble.controller = NULL;
  s_ble.event_handler = NULL;
  s_ble.event_context = NULL;
  s_ble.advertising_requested = false;
  s_ble.scan_requested = false;
  s_ble.stopping = false;
  s_ble.shutdown_requested = false;
  s_ble.shutdown_complete = false;
  return status;
}

static hal_status_t backend_service(void *context) {
  (void)context;
  if (s_ble.controller == NULL || !s_ble.started) {
    return s_ble.faulted ? HAL_EHW : HAL_EUNINIT;
  }
  hal_status_t status = s_ble.controller->service(s_ble.controller->context);
  if (status != HAL_OK) {
    return status;
  }
  jh_btstack_cyw43_transport_snapshot_t snapshot = {0};
  jh_btstack_cyw43_transport_snapshot(&snapshot);
  return snapshot.last_status < HAL_NONE ? snapshot.last_status : HAL_OK;
}

static hal_status_t
backend_advertising_start(void *context,
                          const hal_ble_advertising_config_t *config) {
  (void)context;
  if (config == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.started || s_ble.faulted) {
    const hal_status_t status = s_ble.faulted ? HAL_EHW : HAL_EUNINIT;
    hal_mutex_unlock(mutex);
    return status;
  }
  s_ble.advertising = *config;
  s_ble.advertising_requested = true;
  s_ble.advertising_pending = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

static hal_status_t backend_advertising_stop(void *context) {
  (void)context;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.started || s_ble.faulted) {
    const hal_status_t status = s_ble.faulted ? HAL_EHW : HAL_EUNINIT;
    hal_mutex_unlock(mutex);
    return status;
  }
  s_ble.advertising_requested = false;
  s_ble.advertising_pending = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

static hal_status_t backend_disconnect(void *context,
                                       uint16_t native_connection) {
  (void)context;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.started || s_ble.faulted) {
    const hal_status_t status = s_ble.faulted ? HAL_EHW : HAL_EUNINIT;
    hal_mutex_unlock(mutex);
    return status;
  }
  if (!s_ble.connected || s_ble.connection != native_connection) {
    hal_mutex_unlock(mutex);
    return HAL_ENOENT;
  }
  if (s_ble.disconnect_pending) {
    hal_mutex_unlock(mutex);
    return HAL_EBUSY;
  }
  s_ble.disconnect_connection = native_connection;
  s_ble.disconnect_pending = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

static hal_status_t backend_scan_start(void *context,
                                       const hal_ble_scan_config_t *config) {
  (void)context;
  if (config == NULL) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.started || s_ble.faulted) {
    const hal_status_t status = s_ble.faulted ? HAL_EHW : HAL_EUNINIT;
    hal_mutex_unlock(mutex);
    return status;
  }
  s_ble.scan = *config;
  s_ble.scan_requested = true;
  s_ble.scan_pending = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

static hal_status_t backend_scan_stop(void *context) {
  (void)context;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.started || s_ble.faulted) {
    const hal_status_t status = s_ble.faulted ? HAL_EHW : HAL_EUNINIT;
    hal_mutex_unlock(mutex);
    return status;
  }
  s_ble.scan_requested = false;
  s_ble.scan_pending = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

#ifdef HAL_ENABLE_BLE_STREAM
static hal_status_t backend_stream_notify(void *context,
                                          uint16_t native_connection,
                                          const uint8_t *frame, size_t length) {
  (void)context;
  if (frame == NULL || length == 0u || length > HAL_BLE_STREAM_MAX_FRAME_LEN) {
    return HAL_EINVAL;
  }
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  const bool ready = s_ble.started && !s_ble.faulted && s_ble.connected &&
                     s_ble.connection == native_connection;
  const bool subscribed = s_ble.stream_subscribed;
  if (!ready) {
    const hal_status_t status = s_ble.started ? HAL_ENOENT : HAL_EUNINIT;
    hal_mutex_unlock(mutex);
    return status;
  }
  if (!subscribed) {
    hal_mutex_unlock(mutex);
    return HAL_ESTATE;
  }
  if (s_ble.stream_mtu < HAL_BLE_STREAM_ATT_OVERHEAD ||
      length > (size_t)(s_ble.stream_mtu - HAL_BLE_STREAM_ATT_OVERHEAD)) {
    hal_mutex_unlock(mutex);
    return HAL_EOVERFLOW;
  }
  if (s_ble.stream_pending_frame_length != 0u) {
    hal_mutex_unlock(mutex);
    return HAL_EAGAIN;
  }
  memcpy(s_ble.stream_pending_frame, frame, length);
  s_ble.stream_pending_frame_length = length;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

static hal_status_t backend_stream_publish(void *context,
                                           uint8_t protocol_version,
                                           uint16_t capabilities) {
  (void)context;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_ble.started) {
    hal_mutex_unlock(mutex);
    return HAL_EUNINIT;
  }
  s_ble.stream_version = protocol_version;
  s_ble.stream_capabilities = capabilities;
  s_ble.stream_published = true;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}

static hal_status_t backend_stream_unpublish(void *context) {
  (void)context;
  hal_mutex_t mutex = backend_mutex();
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  s_ble.stream_published = false;
  s_ble.stream_subscribed = false;
  s_ble.stream_waiting_can_send = false;
  s_ble.stream_version = 0u;
  s_ble.stream_capabilities = 0u;
  memset(s_ble.stream_pending_frame, 0, sizeof(s_ble.stream_pending_frame));
  s_ble.stream_pending_frame_length = 0u;
  hal_mutex_unlock(mutex);
  return HAL_OK;
}
#endif

static const jh_ble_backend_t s_backend = {
    .context = NULL,
    .start = backend_start,
    .stop = backend_stop,
    .service = backend_service,
    .advertising_start = backend_advertising_start,
    .advertising_stop = backend_advertising_stop,
    .disconnect = backend_disconnect,
    .scan_start = backend_scan_start,
    .scan_stop = backend_scan_stop,
#ifdef HAL_ENABLE_BLE_STREAM
    .stream_notify = backend_stream_notify,
    .stream_publish = backend_stream_publish,
    .stream_unpublish = backend_stream_unpublish,
#endif
};

const jh_ble_backend_t *jh_ble_backend_instance(void) { return &s_backend; }

#endif
