#include "jh_ble_backend.h"

#if defined(JH_BLUETOOTH_PUBLIC_BLE)

#include "btstack.h"
#include "hal/hal_sync.h"
#include "hal/impl/shared/hal_mutex_once.h"
#include "jh_ble_controller.h"
#include "jh_ble_peripheral_gatt.h"
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
  if (packet_type != HCI_EVENT_PACKET || packet == NULL ||
      hci_event_packet_get_type(packet) != ATT_EVENT_MTU_EXCHANGE_COMPLETE) {
    return;
  }
  const jh_ble_backend_event_t event = {
      .type = JH_BLE_BACKEND_EVENT_MTU_UPDATED,
      .status = HAL_OK,
      .native_connection = att_event_mtu_exchange_complete_get_handle(packet),
      .mtu = att_event_mtu_exchange_complete_get_MTU(packet),
  };
  emit_event(&event);
}

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
}

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
  att_server_init(profile_data, NULL, NULL);
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
};

const jh_ble_backend_t *jh_ble_backend_instance(void) { return &s_backend; }

#endif
