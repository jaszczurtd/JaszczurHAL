#include "jh_bluetooth_stage1_probe.h"

#include <stddef.h>
#include <string.h>

#include "btstack.h"
#include "hal/hal_config.h"
#include "jh_ble_controller.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "jh_btstack_run_loop.h"
#include "jh_stage1_probe_gatt.h"

static btstack_packet_callback_registration_t s_hci_events;
static jh_bluetooth_stage1_snapshot_t s_snapshot;
static uint8_t s_value[32] = {'J', 'H', ' ', 'S', 't', 'a', 'g', 'e', ' ', '1'};
static uint16_t s_value_length = 10u;
static const jh_ble_controller_t *s_controller;

static const uint8_t s_advertising_data[] = {
    0x02u,
    BLUETOOTH_DATA_TYPE_FLAGS,
    0x06u,
    0x0fu,
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
    0x03u,
    BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    0xf0u,
    0xffu,
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

static void controller_invalidated(void *context, uint32_t generation) {
  jh_btstack_run_loop_invalidate(context, generation);
  jh_btstack_cyw43_transport_invalidate();
  s_snapshot.last_status = HAL_EHW;
  s_snapshot.transport_status = HAL_EHW;
}

hal_status_t jh_bluetooth_stage1_start(void) {
  if (s_snapshot.started) {
    return HAL_ESTATE;
  }

  btstack_memory_init();
  hal_status_t status = jh_btstack_run_loop_init();
  if (status != HAL_OK) {
    s_snapshot.last_status = status;
    return status;
  }
  s_controller = jh_ble_controller_backend();
  if (s_controller == NULL || s_controller->start == NULL ||
      s_controller->stop == NULL || s_controller->service == NULL) {
    s_snapshot.last_status = HAL_ECONFIG;
    return HAL_ECONFIG;
  }
  status = s_controller->start(s_controller->context,
                               jh_btstack_run_loop_service_once, NULL,
                               controller_invalidated, NULL);
  if (status != HAL_OK) {
    s_snapshot.last_status = status;
    return status;
  }

  hci_init(jh_btstack_cyw43_hci_transport_instance(), NULL);
  l2cap_init();
  sm_init();
  att_server_init(profile_data, att_read_callback, att_write_callback);

  bd_addr_t null_address = {0u, 0u, 0u, 0u, 0u, 0u};
  gap_advertisements_set_params(0x00a0u, 0x00a0u, 0u, 0u, null_address, 0x07u,
                                0x00u);
  gap_advertisements_set_data((uint8_t)sizeof(s_advertising_data),
                              (uint8_t *)s_advertising_data);
  gap_advertisements_enable(1u);

  s_hci_events.callback = packet_handler;
  hci_add_event_handler(&s_hci_events);

  const int power_status = hci_power_control(HCI_POWER_ON);
  if (power_status != 0) {
    (void)s_controller->stop(s_controller->context);
    s_snapshot.last_status = HAL_EIO;
    return HAL_EIO;
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
      s_controller->service(s_controller->context);
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
