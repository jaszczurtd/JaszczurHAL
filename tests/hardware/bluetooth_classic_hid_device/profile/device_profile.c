#include "device_profile.h"

#include <hal/system/hal_system.h>
#include <tools_c.h>

#include "btstack_defines.h"
#include "btstack_event.h"
#include "classic/btstack_link_key_db_memory.h"
#include "classic/hid_device.h"
#include "classic/sdp_server.h"
#include "classic/sdp_util.h"
#include "gap.h"
#include "hci.h"
#include "jh_btstack_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
  JH_C85_HID_DEVICE_SUBCLASS = 0x2580u,
  JH_C85_HID_REPORT_PERIOD_MS = 250u,
};

static const uint8_t s_mouse_descriptor[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop). */
    0x09, 0x02, /* Usage (Mouse). */
    0xa1, 0x01, /* Collection (Application). */
    0x09, 0x01, /* Usage (Pointer). */
    0xa1, 0x00, /* Collection (Physical). */
    0x05, 0x09, /* Usage Page (Button). */
    0x19, 0x01, /* Usage Minimum (Button 1). */
    0x29, 0x03, /* Usage Maximum (Button 3). */
    0x15, 0x00, /* Logical Minimum (0). */
    0x25, 0x01, /* Logical Maximum (1). */
    0x95, 0x03, /* Report Count (3). */
    0x75, 0x01, /* Report Size (1). */
    0x81, 0x02, /* Input (Data, Variable, Absolute). */
    0x95, 0x01, /* Report Count (1). */
    0x75, 0x05, /* Report Size (5). */
    0x81, 0x03, /* Input (Constant, Variable, Absolute). */
    0x05, 0x01, /* Usage Page (Generic Desktop). */
    0x09, 0x30, /* Usage (X). */
    0x09, 0x31, /* Usage (Y). */
    0x15, 0x81, /* Logical Minimum (-127). */
    0x25, 0x7f, /* Logical Maximum (127). */
    0x75, 0x08, /* Report Size (8). */
    0x95, 0x02, /* Report Count (2). */
    0x81, 0x06, /* Input (Data, Variable, Relative). */
    0xc0,       /* End Collection. */
    0xc0,       /* End Collection. */
};

static uint8_t s_hid_service[320];
static uint32_t s_hid_service_handle;
static btstack_packet_callback_registration_t s_hci_events;
static jh_bluetooth_host_reference_t s_host_reference;
static uint16_t s_hid_cid;
static uint32_t s_next_report_ms;
static uint32_t s_report_count;
static bool s_controller_ready;
static bool s_report_requested;

static void send_mouse_report(void) {
  const int8_t delta = (s_report_count & 1u) == 0u ? 4 : -4;
  const uint8_t message[] = {
      0xa1u,
      0u,
      (uint8_t)delta,
      (uint8_t)(-delta),
  };
  hid_device_send_interrupt_message(s_hid_cid, message, sizeof(message));
  ++s_report_count;
  s_report_requested = false;
  s_next_report_ms = hal_millis() + JH_C85_HID_REPORT_PERIOD_MS;
  if (s_report_count == 1u) {
    deb("JHC85-DEVICE first raw mouse report sent");
  }
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
  (void)channel;
  (void)size;
  if (packet_type != HCI_EVENT_PACKET || packet == NULL) {
    return;
  }

  switch (hci_event_packet_get_type(packet)) {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING &&
        !s_controller_ready) {
      s_controller_ready = true;
      deb("JHC85-DEVICE ready and discoverable");
    }
    break;
  case HCI_EVENT_USER_CONFIRMATION_REQUEST:
    deb("JHC85-DEVICE Just Works pairing auto-authorized");
    break;
  case HCI_EVENT_AUTHENTICATION_COMPLETE:
    deb("JHC85-DEVICE authentication status=0x%02x",
        hci_event_authentication_complete_get_status(packet));
    break;
  case HCI_EVENT_LINK_KEY_NOTIFICATION:
    deb("JHC85-DEVICE volatile link key received");
    break;
  case HCI_EVENT_HID_META:
    switch (hci_event_hid_meta_get_subevent_code(packet)) {
    case HID_SUBEVENT_CONNECTION_OPENED:
      if (hid_subevent_connection_opened_get_status(packet) ==
          ERROR_CODE_SUCCESS) {
        s_hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
        s_next_report_ms = hal_millis();
        deb("JHC85-DEVICE HID channel connected");
      } else {
        deb("JHC85-DEVICE HID connection failed status=0x%02x",
            hid_subevent_connection_opened_get_status(packet));
      }
      break;
    case HID_SUBEVENT_CONNECTION_CLOSED:
      s_hid_cid = 0u;
      s_report_requested = false;
      deb("JHC85-DEVICE HID channel disconnected");
      break;
    case HID_SUBEVENT_CAN_SEND_NOW:
      if (s_hid_cid != 0u) {
        send_mouse_report();
      }
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

static hal_status_t profile_start(void *context) {
  (void)context;
  hci_set_link_key_db(btstack_link_key_db_memory_instance());
  gap_set_local_name("JH C8.5 HID Mouse");
  gap_set_class_of_device(JH_C85_HID_DEVICE_SUBCLASS);
  gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                       LM_LINK_POLICY_ENABLE_SNIFF_MODE);
  gap_set_allow_role_switch(true);
  gap_set_bondable_mode(1);
  gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  gap_ssp_set_authentication_requirement(
      SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_DEDICATED_BONDING);
  gap_ssp_set_auto_accept(1);
  gap_discoverable_control(1);

  sdp_init();
  const hid_sdp_record_t parameters = {
      .hid_device_subclass = JH_C85_HID_DEVICE_SUBCLASS,
      .hid_country_code = 0u,
      .hid_virtual_cable = 0u,
      .hid_remote_wake = 0u,
      .hid_reconnect_initiate = 0u,
      .hid_normally_connectable = true,
      .hid_boot_device = true,
      .hid_ssr_host_max_latency = 0xffffu,
      .hid_ssr_host_min_timeout = 0xffffu,
      .hid_supervision_timeout = 3200u,
      .hid_descriptor = s_mouse_descriptor,
      .hid_descriptor_size = sizeof(s_mouse_descriptor),
      .device_name = "JH C8.5 HID Mouse",
  };
  memset(s_hid_service, 0, sizeof(s_hid_service));
  s_hid_service_handle = sdp_create_service_record_handle();
  hid_create_sdp_record(s_hid_service, s_hid_service_handle, &parameters);
  if (de_get_len(s_hid_service) > sizeof(s_hid_service) ||
      sdp_register_service(s_hid_service) != ERROR_CODE_SUCCESS) {
    sdp_deinit();
    hci_set_link_key_db(NULL);
    return HAL_EIO;
  }

  hid_device_init(true, sizeof(s_mouse_descriptor), s_mouse_descriptor);
  hid_device_register_packet_handler(packet_handler);
  memset(&s_hci_events, 0, sizeof(s_hci_events));
  s_hci_events.callback = packet_handler;
  hci_add_event_handler(&s_hci_events);
  return HAL_OK;
}

static void profile_stop(void *context) {
  (void)context;
  gap_discoverable_control(0);
  hci_remove_event_handler(&s_hci_events);
  hid_device_register_packet_handler(NULL);
  hid_device_deinit();
  if (s_hid_service_handle != 0u) {
    sdp_unregister_service(s_hid_service_handle);
  }
  sdp_deinit();
  hci_set_link_key_db(NULL);
  s_hid_service_handle = 0u;
  s_hid_cid = 0u;
  s_controller_ready = false;
  s_report_requested = false;
}

static hal_status_t profile_service(void *context) {
  (void)context;
  return HAL_OK;
}

static void profile_invalidated(void *context, uint32_t generation) {
  (void)context;
  (void)generation;
  s_hid_cid = 0u;
  s_controller_ready = false;
  s_report_requested = false;
}

static const jh_bluetooth_host_profile_ops_t s_profile_ops = {
    .context = NULL,
    .start = profile_start,
    .stop = profile_stop,
    .service = profile_service,
    .invalidated = profile_invalidated,
};

hal_status_t jh_c85_hid_device_start(void) {
  return jh_btstack_host_acquire(JH_BLUETOOTH_HOST_PROFILE_CLASSIC,
                                 &s_profile_ops, &s_host_reference);
}

hal_status_t jh_c85_hid_device_service(void) {
  const hal_status_t status = jh_btstack_host_service(&s_host_reference);
  if (status != HAL_OK || !s_controller_ready || s_hid_cid == 0u ||
      s_report_requested || (int32_t)(hal_millis() - s_next_report_ms) < 0) {
    return status;
  }
  s_report_requested = true;
  hid_device_request_can_send_now_event(s_hid_cid);
  return HAL_OK;
}

void jh_c85_hid_device_get_info(bool *controller_ready, bool *hid_connected,
                                uint32_t *report_count) {
  if (controller_ready != NULL) {
    *controller_ready = s_controller_ready;
  }
  if (hid_connected != NULL) {
    *hid_connected = s_hid_cid != 0u;
  }
  if (report_count != NULL) {
    *report_count = s_report_count;
  }
}
