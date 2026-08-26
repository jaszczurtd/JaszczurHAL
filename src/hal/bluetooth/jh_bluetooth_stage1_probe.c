#include "jh_bluetooth_stage1_probe.h"

#include <stddef.h>
#include <string.h>

#include "btstack.h"
#include "hal/core/hal_config.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "jh_btstack_host.h"
#include "jh_stage1_probe_gatt.h"

static btstack_packet_callback_registration_t s_hci_events;
static jh_bluetooth_stage1_snapshot_t s_snapshot;
static uint8_t s_value[32] = {'J', 'H', ' ', 'S', 't', 'a', 'g', 'e', ' ', '1'};
static uint16_t s_value_length = 10u;
static jh_bluetooth_host_reference_t s_host_reference;
static bool s_hci_handler_registered;

enum {
  JH_BLE_AD_TYPE_FIELD_SIZE = 1u,
  JH_BLE_FLAGS_VALUE_SIZE = 1u,
  JH_BLE_COMPLETE_NAME_LENGTH = 14u,
  JH_BLE_UUID16_VALUE_SIZE = 2u,
  JH_BLE_FLAG_GENERAL_DISCOVERABLE = 0x02u,
  JH_BLE_FLAG_BR_EDR_NOT_SUPPORTED = 0x04u,
  JH_BLE_STAGE1_SERVICE_UUID_LOW = 0xf0u,
  JH_BLE_STAGE1_SERVICE_UUID_HIGH = 0xffu,
};

static const uint8_t s_advertising_data[] = {
    JH_BLE_AD_TYPE_FIELD_SIZE + JH_BLE_FLAGS_VALUE_SIZE,
    BLUETOOTH_DATA_TYPE_FLAGS,
    JH_BLE_FLAG_GENERAL_DISCOVERABLE | JH_BLE_FLAG_BR_EDR_NOT_SUPPORTED,
    JH_BLE_AD_TYPE_FIELD_SIZE + JH_BLE_COMPLETE_NAME_LENGTH,
    BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'J',
    'H',
    ' ',
    'B',
    'L',
    'E',
    ' ',
    'S',
    't',
    'a',
    'g',
    'e',
    ' ',
    '1',
    JH_BLE_AD_TYPE_FIELD_SIZE + JH_BLE_UUID16_VALUE_SIZE,
    BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    JH_BLE_STAGE1_SERVICE_UUID_LOW,
    JH_BLE_STAGE1_SERVICE_UUID_HIGH,
};

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }

  switch (hci_event_packet_get_type(packet)) {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
      s_snapshot.controller_ready = true;
      s_snapshot.advertising = true;
      s_snapshot.last_status = HAL_OK;
    }
    break;
  case HCI_EVENT_LE_META:
    if (hci_event_le_meta_get_subevent_code(packet) ==
        HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
      if (hci_subevent_le_connection_complete_get_status(packet) ==
          ERROR_CODE_SUCCESS) {
        s_snapshot.connected = true;
        s_snapshot.advertising = false;
        ++s_snapshot.connection_count;
      } else {
        s_snapshot.connected = false;
        s_snapshot.last_status = HAL_EIO;
      }
    }
    break;
  case HCI_EVENT_DISCONNECTION_COMPLETE:
    s_snapshot.last_disconnect_reason =
        hci_event_disconnection_complete_get_reason(packet);
    if (hci_event_disconnection_complete_get_status(packet) ==
        ERROR_CODE_SUCCESS) {
      s_snapshot.connected = false;
      s_snapshot.advertising = s_snapshot.controller_ready;
    } else {
      s_snapshot.last_status = HAL_EIO;
    }
    break;
  default:
    break;
  }
}

static uint16_t att_read_callback(hci_con_handle_t connection_handle,
                                  uint16_t attribute_handle, uint16_t offset,
                                  uint8_t *buffer, uint16_t buffer_size) {
  (void)connection_handle;
  if (attribute_handle !=
      ATT_CHARACTERISTIC_0000FFF1_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
    return 0u;
  }
  return att_read_callback_handle_blob(s_value, s_value_length, offset, buffer,
                                       buffer_size);
}

static int att_write_callback(hci_con_handle_t connection_handle,
                              uint16_t attribute_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size) {
  (void)connection_handle;
  if (attribute_handle !=
          ATT_CHARACTERISTIC_0000FFF1_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE ||
      transaction_mode != ATT_TRANSACTION_MODE_NONE || offset != 0u ||
      buffer == NULL || buffer_size > sizeof(s_value)) {
    return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
  }
  memcpy(s_value, buffer, buffer_size);
  s_value_length = buffer_size;
  ++s_snapshot.writes_received;
  return 0;
}

static void stage1_profile_invalidated(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  s_snapshot.last_status = HAL_EHW;
  s_snapshot.transport_status = HAL_EHW;
}

static hal_status_t stage1_profile_start(void *context) {
  (void)context;
  sm_init();
  att_server_init(profile_data, att_read_callback, att_write_callback);

  bd_addr_t null_address = {0u, 0u, 0u, 0u, 0u, 0u};
  gap_advertisements_set_params(0x00a0u, 0x00a0u, 0u, 0u, null_address, 0x07u,
                                0x00u);
  gap_advertisements_set_data((uint8_t)sizeof(s_advertising_data),
                              (uint8_t *)s_advertising_data);
  gap_advertisements_enable(1u);

  memset(&s_hci_events, 0, sizeof(s_hci_events));
  s_hci_events.callback = packet_handler;
  hci_add_event_handler(&s_hci_events);
  s_hci_handler_registered = true;
  return HAL_OK;
}

static void stage1_profile_stop(void *context) {
  (void)context;
  gap_advertisements_enable(0u);
  if (s_hci_handler_registered) {
    hci_remove_event_handler(&s_hci_events);
    s_hci_handler_registered = false;
  }
  att_server_deinit();
  sm_deinit();
  s_snapshot.started = false;
  s_snapshot.controller_ready = false;
  s_snapshot.advertising = false;
  s_snapshot.connected = false;
}

static hal_status_t stage1_profile_service(void *context) {
  (void)context;
  return HAL_OK;
}

static const jh_bluetooth_host_profile_ops_t s_profile_ops = {
    .context = NULL,
    .start = stage1_profile_start,
    .stop = stage1_profile_stop,
    .service = stage1_profile_service,
    .invalidated = stage1_profile_invalidated,
};

hal_status_t jh_bluetooth_stage1_start(void) {
  if (s_snapshot.started) {
    return HAL_ESTATE;
  }

  const hal_status_t status = jh_btstack_host_acquire(
      JH_BLUETOOTH_HOST_PROFILE_BLE, &s_profile_ops, &s_host_reference);
  if (status != HAL_OK) {
    s_snapshot.last_status = status;
    return status;
  }
  s_snapshot.started = true;
  s_snapshot.last_status = HAL_OK;
  return HAL_OK;
}

hal_status_t jh_bluetooth_stage1_service(void) {
  if (!s_snapshot.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t service_status =
      jh_btstack_host_service(&s_host_reference);
  if (service_status != HAL_OK) {
    s_snapshot.last_status = service_status;
    return service_status;
  }

  jh_btstack_cyw43_transport_snapshot_t transport;
  jh_btstack_cyw43_transport_snapshot(&transport);
  s_snapshot.rx_packets = transport.rx_packets;
  s_snapshot.rx_event_packets = transport.rx_event_packets;
  s_snapshot.rx_acl_packets = transport.rx_acl_packets;
  s_snapshot.tx_packets = transport.tx_packets;
  s_snapshot.tx_command_packets = transport.tx_command_packets;
  s_snapshot.tx_acl_packets = transport.tx_acl_packets;
  s_snapshot.drain_budget_hits = transport.drain_budget_hits;
  s_snapshot.host_buffer_size_status = transport.host_buffer_size_status;
  s_snapshot.controller_to_host_flow_control_status =
      transport.controller_to_host_flow_control_status;
  s_snapshot.transport_status = transport.last_status;
  if (transport.last_status < HAL_NONE) {
    s_snapshot.last_status = transport.last_status;
    return transport.last_status;
  }
  return HAL_OK;
}

void jh_bluetooth_stage1_snapshot(
    jh_bluetooth_stage1_snapshot_t *out_snapshot) {
  if (out_snapshot != NULL) {
    *out_snapshot = s_snapshot;
  }
}
