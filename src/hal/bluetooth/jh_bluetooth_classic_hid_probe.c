#include "jh_bluetooth_classic_hid_probe.h"

#include "btstack_event.h"
#include "btstack_hid_parser.h"
#include "classic/btstack_link_key_db_memory.h"
#include "classic/hid_host.h"
#include "classic/sdp_client.h"
#include "hal/core/hal_config.h"
#include "hci.h"
#include "jh_bluetooth_classic_hid_lifecycle.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "jh_btstack_host.h"
#include "l2cap.h"

#include <stddef.h>
#include <string.h>

enum { JH_CLASSIC_HID_DESCRIPTOR_CAPACITY = 512u };

static uint8_t s_hid_descriptor[JH_CLASSIC_HID_DESCRIPTOR_CAPACITY];
static btstack_hid_parser_t s_hid_parser;
static btstack_packet_callback_registration_t s_hci_events;
static jh_bluetooth_classic_hid_lifecycle_t s_lifecycle;
static jh_bluetooth_classic_hid_probe_snapshot_t s_snapshot;
static jh_bluetooth_host_reference_t s_host_reference;

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }

  const uint8_t event = hci_event_packet_get_type(packet);
  if (event == BTSTACK_EVENT_STATE) {
    s_snapshot.controller_ready =
        btstack_event_state_get_state(packet) == HCI_STATE_WORKING;
    return;
  }
  if (event != HCI_EVENT_HID_META) {
    return;
  }

  ++s_snapshot.hid_events;
  if (hci_event_hid_meta_get_subevent_code(packet) ==
      HID_SUBEVENT_INCOMING_CONNECTION) {
    (void)hid_host_decline_connection(
        hid_subevent_incoming_connection_get_hid_cid(packet));
    ++s_snapshot.rejected_incoming_connections;
  }
}

static hal_status_t link_key_db_start(void *context) {
  (void)context;
  hci_set_link_key_db(btstack_link_key_db_memory_instance());
  return HAL_OK;
}

static void link_key_db_stop(void *context) {
  (void)context;
  hci_set_link_key_db(NULL);
}

static hal_status_t sdp_client_start(void *context) {
  (void)context;
  sdp_client_init();
  return HAL_OK;
}

static void sdp_client_stop(void *context) {
  (void)context;
  sdp_client_deinit();
}

static hal_status_t hid_profile_start(void *context) {
  (void)context;
  btstack_hid_parser_init(&s_hid_parser, NULL, 0u, HID_REPORT_TYPE_INPUT, NULL,
                          0u);
  hid_host_init(s_hid_descriptor, sizeof(s_hid_descriptor));
  return HAL_OK;
}

static void hid_profile_stop(void *context) {
  (void)context;
  (void)l2cap_unregister_service(PSM_HID_INTERRUPT);
  (void)l2cap_unregister_service(PSM_HID_CONTROL);
  hid_host_deinit();
}

static hal_status_t event_handler_start(void *context) {
  (void)context;
  memset(&s_hci_events, 0, sizeof(s_hci_events));
  s_hci_events.callback = packet_handler;
  hid_host_register_packet_handler(packet_handler);
  hci_add_event_handler(&s_hci_events);
  return HAL_OK;
}

static void event_handler_stop(void *context) {
  (void)context;
  hci_remove_event_handler(&s_hci_events);
  hid_host_register_packet_handler(NULL);
  memset(&s_hci_events, 0, sizeof(s_hci_events));
}

static const jh_bluetooth_classic_hid_lifecycle_ops_t s_lifecycle_ops = {
    .context = NULL,
    .link_key_db_start = link_key_db_start,
    .link_key_db_stop = link_key_db_stop,
    .sdp_client_start = sdp_client_start,
    .sdp_client_stop = sdp_client_stop,
    .hid_host_start = hid_profile_start,
    .hid_host_stop = hid_profile_stop,
    .event_handler_start = event_handler_start,
    .event_handler_stop = event_handler_stop,
};

static hal_status_t profile_start(void *context) {
  (void)context;
  const hal_status_t status =
      jh_bluetooth_classic_hid_lifecycle_start(&s_lifecycle, &s_lifecycle_ops);
  s_snapshot.profile_ready = status == HAL_OK;
  return status;
}

static void profile_stop(void *context) {
  (void)context;
  jh_bluetooth_classic_hid_lifecycle_stop(&s_lifecycle, &s_lifecycle_ops);
  s_snapshot.profile_ready = false;
  s_snapshot.controller_ready = false;
}

static hal_status_t profile_service(void *context) {
  (void)context;
  return HAL_OK;
}

static void profile_invalidated(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  s_snapshot.controller_ready = false;
  s_snapshot.profile_ready = false;
  s_snapshot.last_status = HAL_EHW;
  s_snapshot.transport_status = HAL_EHW;
}

static const jh_bluetooth_host_profile_ops_t s_profile_ops = {
    .context = NULL,
    .start = profile_start,
    .stop = profile_stop,
    .service = profile_service,
    .invalidated = profile_invalidated,
};

hal_status_t jh_bluetooth_classic_hid_probe_start(void) {
  if (s_snapshot.started) {
    return HAL_ESTATE;
  }
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  const hal_status_t status = jh_btstack_host_acquire(
      JH_BLUETOOTH_HOST_PROFILE_CLASSIC_HID, &s_profile_ops, &s_host_reference);
  s_snapshot.started = status == HAL_OK;
  s_snapshot.last_status = status;
  return status;
}

hal_status_t jh_bluetooth_classic_hid_probe_service(void) {
  if (!s_snapshot.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t status = jh_btstack_host_service(&s_host_reference);
  s_snapshot.last_status = status;

  jh_btstack_cyw43_transport_snapshot_t transport;
  jh_btstack_cyw43_transport_snapshot(&transport);
  s_snapshot.rx_packets = transport.rx_packets;
  s_snapshot.rx_event_packets = transport.rx_event_packets;
  s_snapshot.rx_acl_packets = transport.rx_acl_packets;
  s_snapshot.tx_packets = transport.tx_packets;
  s_snapshot.tx_command_packets = transport.tx_command_packets;
  s_snapshot.tx_acl_packets = transport.tx_acl_packets;
  s_snapshot.drain_budget_hits = transport.drain_budget_hits;
  s_snapshot.transport_status = transport.last_status;
  return status;
}

hal_status_t jh_bluetooth_classic_hid_probe_stop(void) {
  if (!s_snapshot.started) {
    return HAL_EUNINIT;
  }
  const hal_status_t status = jh_btstack_host_release(&s_host_reference);
  s_snapshot.started = false;
  s_snapshot.last_status = status;
  return status;
}

void jh_bluetooth_classic_hid_probe_snapshot(
    jh_bluetooth_classic_hid_probe_snapshot_t *out_snapshot) {
  if (out_snapshot != NULL) {
    *out_snapshot = s_snapshot;
  }
}
