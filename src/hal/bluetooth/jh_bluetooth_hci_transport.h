#pragma once

#include "hal/core/hal_status.h"
#include "jh_bluetooth_controller.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE 4u
#define JH_BLUETOOTH_HCI_SERVICE_BUDGET 8u

typedef void (*jh_bluetooth_hci_packet_fn)(void *context, uint8_t packet_type,
                                           uint8_t *packet, uint16_t size);

typedef struct {
  hal_status_t last_status;
  uint32_t service_calls;
  uint32_t rx_packets;
  uint32_t rx_event_packets;
  uint32_t rx_acl_packets;
  uint32_t tx_packets;
  uint32_t tx_command_packets;
  uint32_t tx_acl_packets;
  uint32_t drain_budget_hits;
  uint8_t host_buffer_size_status;
  uint8_t controller_to_host_flow_control_status;
} jh_bluetooth_hci_transport_snapshot_t;

typedef struct {
  const jh_bluetooth_controller_t *controller;
  jh_bluetooth_hci_packet_fn packet_handler;
  void *packet_context;
  uint8_t *receive_buffer;
  uint32_t receive_capacity;
  uint8_t address[6];
  jh_bluetooth_hci_transport_snapshot_t snapshot;
  bool initialized;
  bool ready;
  bool in_service;
} jh_bluetooth_hci_transport_t;

hal_status_t jh_bluetooth_hci_transport_init(
    jh_bluetooth_hci_transport_t *transport,
    const jh_bluetooth_controller_t *controller, uint8_t *receive_buffer,
    uint32_t receive_capacity, jh_bluetooth_hci_packet_fn packet_handler,
    void *packet_context);
hal_status_t
jh_bluetooth_hci_transport_open(jh_bluetooth_hci_transport_t *transport);
hal_status_t
jh_bluetooth_hci_transport_close(jh_bluetooth_hci_transport_t *transport);
void jh_bluetooth_hci_transport_invalidate(
    jh_bluetooth_hci_transport_t *transport, hal_status_t status);
hal_status_t
jh_bluetooth_hci_transport_service(jh_bluetooth_hci_transport_t *transport);
hal_status_t
jh_bluetooth_hci_transport_send(jh_bluetooth_hci_transport_t *transport,
                                uint8_t packet_type, uint8_t *packet,
                                uint16_t size);
hal_status_t jh_bluetooth_hci_transport_address(
    const jh_bluetooth_hci_transport_t *transport, uint8_t out_address[6]);
void jh_bluetooth_hci_transport_snapshot(
    const jh_bluetooth_hci_transport_t *transport,
    jh_bluetooth_hci_transport_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
