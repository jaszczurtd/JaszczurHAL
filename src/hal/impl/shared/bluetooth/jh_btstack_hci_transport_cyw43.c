/* Portable BTstack adaptation of JH's bounded CYW43 HCI transport. */
#include "jh_btstack_hci_transport_cyw43.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack_run_loop.h"
#include "hci.h"
#include "jh_ble_controller.h"
#include "jh_ble_hci_transport.h"
#include "jh_btstack_chipset_cyw43.h"
#include "jh_btstack_run_loop.h"

#if HCI_OUTGOING_PRE_BUFFER_SIZE < JH_BLE_HCI_FRAME_HEADER_SIZE
#error "BTstack must reserve the four-byte CYW43 outgoing pre-buffer"
#endif
#if (HCI_ACL_CHUNK_SIZE_ALIGNMENT & 3) != 0
#error "BTstack ACL chunks must be four-byte aligned for CYW43"
#endif
#if HCI_INCOMING_PRE_BUFFER_SIZE < JH_BLE_HCI_FRAME_HEADER_SIZE
#undef HCI_INCOMING_PRE_BUFFER_SIZE
#define HCI_INCOMING_PRE_BUFFER_SIZE JH_BLE_HCI_FRAME_HEADER_SIZE
#endif

#define JH_INCOMING_PRE_BUFFER_ALIGNED                                         \
  ((HCI_INCOMING_PRE_BUFFER_SIZE + 3u) & ~3u)

static void (*s_packet_handler)(uint8_t, uint8_t *, uint16_t);
static btstack_data_source_t s_data_source;
static jh_ble_hci_transport_t s_transport_runtime;
static bool s_data_source_ready;

static uint8_t
    s_incoming[JH_INCOMING_PRE_BUFFER_ALIGNED + HCI_INCOMING_PACKET_BUFFER_SIZE]
    __attribute__((aligned(4)));
static uint8_t *const s_receive_buffer =
    &s_incoming[JH_INCOMING_PRE_BUFFER_ALIGNED - JH_BLE_HCI_FRAME_HEADER_SIZE];

static void receive_packet(void *context, uint8_t packet_type, uint8_t *packet,
                           uint16_t size) {
  (void)context;
  if (s_packet_handler != NULL) {
    s_packet_handler(packet_type, packet, size);
  }
}

static void data_source_process(btstack_data_source_t *data_source,
                                btstack_data_source_callback_type_t type) {
  if (data_source != &s_data_source || type != DATA_SOURCE_CALLBACK_POLL) {
    return;
  }
  (void)jh_ble_hci_transport_service(&s_transport_runtime);
}

static void transport_init(const void *config) {
  (void)config;
  (void)jh_ble_hci_transport_init(
      &s_transport_runtime, jh_ble_controller_backend(), s_receive_buffer,
      JH_BLE_HCI_FRAME_HEADER_SIZE + HCI_INCOMING_PACKET_BUFFER_SIZE,
      receive_packet, NULL);
}

static int transport_open(void) {
  const hal_status_t status = jh_ble_hci_transport_open(&s_transport_runtime);
  if (status != HAL_OK) {
    return (int)status;
  }

  bd_addr_t address = {0u};
  if (jh_ble_hci_transport_address(&s_transport_runtime, address) != HAL_OK) {
    (void)jh_ble_hci_transport_close(&s_transport_runtime);
    return (int)HAL_EIO;
  }
  hci_set_chipset(jh_btstack_chipset_cyw43_instance());
  hci_set_bd_addr(address);

  memset(&s_data_source, 0, sizeof(s_data_source));
  btstack_run_loop_set_data_source_handler(&s_data_source, data_source_process);
  btstack_run_loop_enable_data_source_callbacks(&s_data_source,
                                                DATA_SOURCE_CALLBACK_POLL);
  btstack_run_loop_add_data_source(&s_data_source);
  s_data_source_ready = true;
  return 0;
}

static int transport_close(void) {
  if (s_data_source_ready) {
    btstack_run_loop_disable_data_source_callbacks(&s_data_source,
                                                   DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_remove_data_source(&s_data_source);
    s_data_source_ready = false;
  }
  const hal_status_t status = jh_ble_hci_transport_close(&s_transport_runtime);
  return status == HAL_OK ? 0 : (int)status;
}

static void transport_register_packet_handler(void (*handler)(uint8_t,
                                                              uint8_t *,
                                                              uint16_t)) {
  s_packet_handler = handler;
}

static int transport_can_send_now(uint8_t packet_type) {
  (void)packet_type;
  return s_data_source_ready ? 1 : 0;
}

static int transport_send_packet(uint8_t packet_type, uint8_t *packet,
                                 int size) {
  if (size < 0 || size > UINT16_MAX || s_packet_handler == NULL) {
    return (int)HAL_EINVAL;
  }
  const hal_status_t status = jh_ble_hci_transport_send(
      &s_transport_runtime, packet_type, packet, (uint16_t)size);
  if (status != HAL_OK) {
    return (int)status;
  }
  static uint8_t packet_sent_event[] = {HCI_EVENT_TRANSPORT_PACKET_SENT, 0u};
  s_packet_handler(HCI_EVENT_PACKET, packet_sent_event,
                   (uint16_t)sizeof(packet_sent_event));
  return 0;
}

static const hci_transport_t s_transport = {
    .name = "JH CYW43",
    .init = transport_init,
    .open = transport_open,
    .close = transport_close,
    .register_packet_handler = transport_register_packet_handler,
    .can_send_packet_now = transport_can_send_now,
    .send_packet = transport_send_packet,
    .set_baudrate = NULL,
    .reset_link = NULL,
    .set_sco_config = NULL,
};

const hci_transport_t *jh_btstack_cyw43_hci_transport_instance(void) {
  return &s_transport;
}

void jh_btstack_cyw43_transport_snapshot(
    jh_btstack_cyw43_transport_snapshot_t *out_snapshot) {
  jh_ble_hci_transport_snapshot(&s_transport_runtime, out_snapshot);
}

void jh_btstack_cyw43_transport_invalidate(void) {
  jh_ble_hci_transport_invalidate(&s_transport_runtime, HAL_EHW);
}

/* Called by the single JH-owned CYW43 poll path when host-wake has work. */
void cyw43_bluetooth_hci_process(void) {
  if (s_data_source_ready) {
    jh_btstack_run_loop_notify();
  }
}
