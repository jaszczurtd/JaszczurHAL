#include "jh_bluetooth_hci_transport.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JH_HCI_COMMAND_DATA_PACKET 0x01u
#define JH_HCI_ACL_DATA_PACKET 0x02u
#define JH_HCI_EVENT_PACKET 0x04u
#define JH_HCI_EVENT_COMMAND_COMPLETE 0x0eu
#define JH_HCI_OPCODE_HOST_BUFFER_SIZE 0x0c33u
#define JH_HCI_OPCODE_SET_CONTROLLER_TO_HOST_FLOW_CONTROL 0x0c31u

static bool controller_is_valid(const jh_bluetooth_controller_t *controller) {
  return controller != NULL && controller->hci_init != NULL &&
         controller->hci_read != NULL && controller->hci_write != NULL &&
         controller->read_factory_address != NULL;
}

static void record_received_packet(jh_bluetooth_hci_transport_t *transport,
                                   uint8_t packet_type, const uint8_t *packet,
                                   uint16_t size) {
  if (packet_type == JH_HCI_ACL_DATA_PACKET) {
    ++transport->snapshot.rx_acl_packets;
    return;
  }
  if (packet_type != JH_HCI_EVENT_PACKET) {
    return;
  }

  ++transport->snapshot.rx_event_packets;
  if (packet == NULL || size < 6u ||
      packet[0] != JH_HCI_EVENT_COMMAND_COMPLETE) {
    return;
  }
  const uint16_t opcode = (uint16_t)packet[3] | ((uint16_t)packet[4] << 8u);
  if (opcode == JH_HCI_OPCODE_HOST_BUFFER_SIZE) {
    transport->snapshot.host_buffer_size_status = packet[5];
  } else if (opcode == JH_HCI_OPCODE_SET_CONTROLLER_TO_HOST_FLOW_CONTROL) {
    transport->snapshot.controller_to_host_flow_control_status = packet[5];
  }
}

hal_status_t jh_bluetooth_hci_transport_init(
    jh_bluetooth_hci_transport_t *transport,
    const jh_bluetooth_controller_t *controller, uint8_t *receive_buffer,
    uint32_t receive_capacity, jh_bluetooth_hci_packet_fn packet_handler,
    void *packet_context) {
  if (transport == NULL || !controller_is_valid(controller) ||
      receive_buffer == NULL ||
      receive_capacity < JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE ||
      ((uintptr_t)receive_buffer & 3u) != 0u || packet_handler == NULL) {
    return HAL_EINVAL;
  }
  memset(transport, 0, sizeof(*transport));
  transport->controller = controller;
  transport->packet_handler = packet_handler;
  transport->packet_context = packet_context;
  transport->receive_buffer = receive_buffer;
  transport->receive_capacity = receive_capacity;
  transport->snapshot.last_status = HAL_NONE;
  transport->snapshot.host_buffer_size_status = 0xffu;
  transport->snapshot.controller_to_host_flow_control_status = 0xffu;
  transport->initialized = true;
  return HAL_OK;
}

hal_status_t
jh_bluetooth_hci_transport_open(jh_bluetooth_hci_transport_t *transport) {
  if (transport == NULL || !transport->initialized) {
    return HAL_EUNINIT;
  }
  if (transport->ready) {
    return HAL_EBUSY;
  }
  hal_status_t status =
      transport->controller->hci_init(transport->controller->context);
  if (status == HAL_OK) {
    status = transport->controller->read_factory_address(
        transport->controller->context, transport->address);
  }
  if (status != HAL_OK) {
    transport->snapshot.last_status = status;
    return status;
  }
  ++transport->address[5];
  transport->ready = true;
  transport->snapshot.last_status = HAL_OK;
  return HAL_OK;
}

hal_status_t
jh_bluetooth_hci_transport_close(jh_bluetooth_hci_transport_t *transport) {
  if (transport == NULL || !transport->initialized) {
    return HAL_EUNINIT;
  }
  transport->ready = false;
  transport->in_service = false;
  return HAL_OK;
}

void jh_bluetooth_hci_transport_invalidate(
    jh_bluetooth_hci_transport_t *transport, hal_status_t status) {
  if (transport != NULL && transport->initialized) {
    transport->ready = false;
    transport->in_service = false;
    transport->snapshot.last_status = status < HAL_NONE ? status : HAL_EHW;
  }
}

hal_status_t
jh_bluetooth_hci_transport_service(jh_bluetooth_hci_transport_t *transport) {
  if (transport == NULL || !transport->initialized || !transport->ready) {
    return HAL_EUNINIT;
  }
  if (transport->in_service) {
    return HAL_EBUSY;
  }

  transport->in_service = true;
  ++transport->snapshot.service_calls;
  bool had_work = false;
  uint32_t loop_count = 0u;
  hal_status_t status = HAL_OK;
  do {
    uint32_t length = 0u;
    status = transport->controller->hci_read(
        transport->controller->context, transport->receive_buffer,
        transport->receive_capacity, &length);
    if (status != HAL_OK) {
      break;
    }
    if (length > transport->receive_capacity ||
        (length != 0u && length < JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE)) {
      status = HAL_EPROTO;
      break;
    }
    had_work = length != 0u;
    if (had_work) {
      const uint32_t payload_length =
          length - JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE;
      if (payload_length > UINT16_MAX) {
        status = HAL_EOVERFLOW;
        break;
      }
      const uint8_t packet_type = transport->receive_buffer[3];
      uint8_t *const packet =
          &transport->receive_buffer[JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE];
      ++transport->snapshot.rx_packets;
      record_received_packet(transport, packet_type, packet,
                             (uint16_t)payload_length);
      transport->packet_handler(transport->packet_context, packet_type, packet,
                                (uint16_t)payload_length);
    }
    ++loop_count;
  } while (had_work && loop_count < JH_BLUETOOTH_HCI_SERVICE_BUDGET);

  if (status == HAL_OK && had_work &&
      loop_count == JH_BLUETOOTH_HCI_SERVICE_BUDGET) {
    ++transport->snapshot.drain_budget_hits;
  }
  transport->in_service = false;
  transport->snapshot.last_status = status;
  return status;
}

hal_status_t
jh_bluetooth_hci_transport_send(jh_bluetooth_hci_transport_t *transport,
                                uint8_t packet_type, uint8_t *packet,
                                uint16_t size) {
  if (transport == NULL || !transport->initialized || !transport->ready) {
    return HAL_EUNINIT;
  }
  if (packet == NULL) {
    transport->snapshot.last_status = HAL_EINVAL;
    return HAL_EINVAL;
  }
  uint8_t *const frame = packet - JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE;
  frame[3] = packet_type;
  const hal_status_t status = transport->controller->hci_write(
      transport->controller->context, frame,
      (size_t)size + JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE);
  transport->snapshot.last_status = status;
  if (status != HAL_OK) {
    return status;
  }
  ++transport->snapshot.tx_packets;
  if (packet_type == JH_HCI_COMMAND_DATA_PACKET) {
    ++transport->snapshot.tx_command_packets;
  } else if (packet_type == JH_HCI_ACL_DATA_PACKET) {
    ++transport->snapshot.tx_acl_packets;
  }
  return HAL_OK;
}

hal_status_t jh_bluetooth_hci_transport_address(
    const jh_bluetooth_hci_transport_t *transport, uint8_t out_address[6]) {
  if (transport == NULL || !transport->initialized || !transport->ready) {
    return HAL_EUNINIT;
  }
  if (out_address == NULL) {
    return HAL_EINVAL;
  }
  memcpy(out_address, transport->address, sizeof(transport->address));
  return HAL_OK;
}

void jh_bluetooth_hci_transport_snapshot(
    const jh_bluetooth_hci_transport_t *transport,
    jh_bluetooth_hci_transport_snapshot_t *out_snapshot) {
  if (transport != NULL && out_snapshot != NULL) {
    *out_snapshot = transport->snapshot;
  }
}
