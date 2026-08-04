/* Portable, bounded adaptation of Pico SDK's BSD-3-Clause CYW43 transport. */
#include "jh_btstack_hci_transport_cyw43.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack_run_loop.h"
#include "cybt_shared_bus_driver.h"
#include "cyw43.h"
#include "cyw43_configport.h"
#include "hci.h"
#include "hci_cmd.h"
#include "jh_btstack_chipset_cyw43.h"

#define JH_CYW43_PACKET_HEADER_SIZE 4u
#define JH_BTSTACK_CYW43_MAX_HCI_PROCESS_LOOP_COUNT 8u

#if HCI_OUTGOING_PRE_BUFFER_SIZE < JH_CYW43_PACKET_HEADER_SIZE
#error "BTstack must reserve the four-byte CYW43 outgoing pre-buffer"
#endif
#if (HCI_ACL_CHUNK_SIZE_ALIGNMENT & 3) != 0
#error "BTstack ACL chunks must be four-byte aligned for CYW43"
#endif
#if HCI_INCOMING_PRE_BUFFER_SIZE < JH_CYW43_PACKET_HEADER_SIZE
#undef HCI_INCOMING_PRE_BUFFER_SIZE
#define HCI_INCOMING_PRE_BUFFER_SIZE JH_CYW43_PACKET_HEADER_SIZE
#endif

#define JH_INCOMING_PRE_BUFFER_ALIGNED                                         \
  ((HCI_INCOMING_PRE_BUFFER_SIZE + 3u) & ~3u)

static void (*s_packet_handler)(uint8_t, uint8_t *, uint16_t);
static btstack_data_source_t s_data_source;
static bool s_ready;
static jh_btstack_cyw43_transport_snapshot_t s_snapshot = {
    .last_status = HAL_NONE,
    .host_buffer_size_status = 0xffu,
    .controller_to_host_flow_control_status = 0xffu,
};

static uint8_t
    s_incoming[JH_INCOMING_PRE_BUFFER_ALIGNED + HCI_INCOMING_PACKET_BUFFER_SIZE]
    __attribute__((aligned(4)));
static uint8_t *const s_receive_buffer =
    &s_incoming[JH_INCOMING_PRE_BUFFER_ALIGNED - JH_CYW43_PACKET_HEADER_SIZE];

static hal_status_t map_cybt_status(int status) {
  switch (status) {
  case CYBT_SUCCESS:
    return HAL_OK;
  case CYBT_ERR_BADARG:
    return HAL_EINVAL;
  case CYBT_ERR_OUT_OF_MEMORY:
  case CYBT_ERR_INIT_MEMPOOL_FAILED:
    return HAL_ENOMEM;
  case CYBT_ERR_TIMEOUT:
    return HAL_ETIMEOUT;
  case CYBT_ERR_QUEUE_ALMOST_FULL:
  case CYBT_ERR_QUEUE_FULL:
  case CYBT_ERR_SEND_QUEUE_FAILED:
    return HAL_EBUSY;
  default:
    return HAL_EIO;
  }
}

static void record_received_packet(uint8_t packet_type, const uint8_t *packet,
                                   uint16_t size) {
  if (packet_type == HCI_ACL_DATA_PACKET) {
    ++s_snapshot.rx_acl_packets;
    return;
  }
  if (packet_type != HCI_EVENT_PACKET) {
    return;
  }

  ++s_snapshot.rx_event_packets;
  if (packet == NULL || size < 6u || packet[0] != HCI_EVENT_COMMAND_COMPLETE) {
    return;
  }
  const uint16_t opcode = (uint16_t)packet[3] | ((uint16_t)packet[4] << 8u);
  if (opcode == HCI_OPCODE_HCI_HOST_BUFFER_SIZE) {
    s_snapshot.host_buffer_size_status = packet[5];
  } else if (opcode == HCI_OPCODE_HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL) {
    s_snapshot.controller_to_host_flow_control_status = packet[5];
  }
}

static void process_hci(void) {
  uint32_t loop_count = 0u;
  bool had_work = false;
  do {
    uint32_t length = 0u;
    const int result = cyw43_bluetooth_hci_read(
        s_receive_buffer,
        JH_CYW43_PACKET_HEADER_SIZE + HCI_INCOMING_PACKET_BUFFER_SIZE, &length);
    if (result != CYBT_SUCCESS) {
      s_snapshot.last_status = map_cybt_status(result);
      return;
    }
    if (length >
            JH_CYW43_PACKET_HEADER_SIZE + HCI_INCOMING_PACKET_BUFFER_SIZE ||
        (length != 0u && length < JH_CYW43_PACKET_HEADER_SIZE)) {
      s_snapshot.last_status = HAL_EPROTO;
      return;
    }
    had_work = length != 0u;
    if (had_work) {
      const uint32_t payload_length = length - JH_CYW43_PACKET_HEADER_SIZE;
      if (payload_length > UINT16_MAX || s_packet_handler == NULL) {
        s_snapshot.last_status = HAL_EPROTO;
        return;
      }
      ++s_snapshot.rx_packets;
      const uint8_t packet_type = s_receive_buffer[3];
      uint8_t *const packet = &s_receive_buffer[JH_CYW43_PACKET_HEADER_SIZE];
      record_received_packet(packet_type, packet, (uint16_t)payload_length);
      s_packet_handler(packet_type, packet, (uint16_t)payload_length);
    }
    ++loop_count;
  } while (had_work &&
           loop_count < JH_BTSTACK_CYW43_MAX_HCI_PROCESS_LOOP_COUNT);

  if (had_work) {
    ++s_snapshot.drain_budget_hits;
  }
}

static void data_source_process(btstack_data_source_t *data_source,
                                btstack_data_source_callback_type_t type) {
  if (data_source != &s_data_source || type != DATA_SOURCE_CALLBACK_POLL) {
    s_snapshot.last_status = HAL_EINTERNAL;
    return;
  }
  process_hci();
}

static void transport_init(const void *config) { (void)config; }

static int transport_open(void) {
  const int result = cyw43_bluetooth_hci_init();
  if (result != CYBT_SUCCESS) {
    s_snapshot.last_status = map_cybt_status(result);
    return result;
  }

  bd_addr_t address;
  jh_cyw43_port_get_mac(0, address);
  ++address[BD_ADDR_LEN - 1u];
  hci_set_chipset(jh_btstack_chipset_cyw43_instance());
  hci_set_bd_addr(address);

  memset(&s_data_source, 0, sizeof(s_data_source));
  btstack_run_loop_set_data_source_handler(&s_data_source, data_source_process);
  btstack_run_loop_enable_data_source_callbacks(&s_data_source,
                                                DATA_SOURCE_CALLBACK_POLL);
  btstack_run_loop_add_data_source(&s_data_source);
  s_ready = true;
  s_snapshot.last_status = HAL_OK;
  return 0;
}

static int transport_close(void) {
  if (s_ready) {
    btstack_run_loop_disable_data_source_callbacks(&s_data_source,
                                                   DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_remove_data_source(&s_data_source);
  }
  s_ready = false;
  return 0;
}

static void transport_register_packet_handler(void (*handler)(uint8_t,
                                                              uint8_t *,
                                                              uint16_t)) {
  s_packet_handler = handler;
}

static int transport_can_send_now(uint8_t packet_type) {
  (void)packet_type;
  return s_ready ? 1 : 0;
}

static int transport_send_packet(uint8_t packet_type, uint8_t *packet,
                                 int size) {
  if (!s_ready || packet == NULL || size < 0 || s_packet_handler == NULL) {
    s_snapshot.last_status = HAL_ESTATE;
    return CYBT_ERR_BADARG;
  }
  uint8_t *const buffer = packet - JH_CYW43_PACKET_HEADER_SIZE;
  buffer[3] = packet_type;
  const int result = cyw43_bluetooth_hci_write(
      buffer, (uint32_t)size + JH_CYW43_PACKET_HEADER_SIZE);
  if (result != CYBT_SUCCESS) {
    s_snapshot.last_status = map_cybt_status(result);
    return result;
  }

  ++s_snapshot.tx_packets;
  if (packet_type == HCI_COMMAND_DATA_PACKET) {
    ++s_snapshot.tx_command_packets;
  } else if (packet_type == HCI_ACL_DATA_PACKET) {
    ++s_snapshot.tx_acl_packets;
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
  if (out_snapshot != NULL) {
    *out_snapshot = s_snapshot;
  }
}

/* Called by the single JH-owned CYW43 poll path when host-wake has work. */
void cyw43_bluetooth_hci_process(void) {
  if (s_ready) {
    btstack_run_loop_poll_data_sources_from_irq();
  }
}
