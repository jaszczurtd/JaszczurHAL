#include <hal/bluetooth/hal_bluetooth_classic.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

#include "hci_dump.h"
#include "jh_btstack_hci_transport_cyw43.h"
#include "rp2040_cyw43_gspi.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  TRACE_CAPACITY = 160u,
  TRACE_PACKET_BYTES = 80u,
  COMMAND_CAPACITY = 32u,
  TRACE_HCI_COMMAND_PACKET = 0x01u,
  TRACE_HCI_ACL_PACKET = 0x02u,
  TRACE_HCI_EVENT_PACKET = 0x04u,
  TRACE_HCI_EVENT_INQUIRY_COMPLETE = 0x01u,
  TRACE_HCI_EVENT_INQUIRY_RESULT = 0x02u,
  TRACE_HCI_EVENT_COMMAND_COMPLETE = 0x0eu,
  TRACE_HCI_EVENT_COMMAND_STATUS = 0x0fu,
  TRACE_HCI_EVENT_INQUIRY_RESULT_WITH_RSSI = 0x22u,
  TRACE_HCI_EVENT_EXTENDED_INQUIRY_RESULT = 0x2fu,
  TRACE_HCI_OPCODE_INQUIRY = 0x0401u,
  TRACE_HCI_OPCODE_INQUIRY_CANCEL = 0x0402u,
  TRACE_HCI_OPCODE_READ_LOCAL_VERSION = 0x1001u,
  TRACE_HCI_OPCODE_READ_LOCAL_COMMANDS = 0x1002u,
  TRACE_HCI_OPCODE_READ_LOCAL_FEATURES = 0x1003u,
  TRACE_HCI_OPCODE_READ_BUFFER_SIZE = 0x1005u,
};

typedef struct {
  uint32_t sequence;
  uint16_t original_length;
  uint8_t packet_type;
  uint8_t captured_length;
  bool incoming;
  uint8_t packet[TRACE_PACKET_BYTES];
} trace_record_t;

static hal_bluetooth_classic_t s_classic;
static trace_record_t s_trace[TRACE_CAPACITY];
static size_t s_traceRead;
static size_t s_traceCount;
static uint32_t s_traceSequence;
static uint32_t s_traceDropped;
static char s_command[COMMAND_CAPACITY];
static size_t s_commandLength;
static bool s_started;
static bool s_readyReported;

static uint16_t readLe16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
}

static void traceReset(void) {
  s_traceRead = 0u;
  s_traceCount = 0u;
  s_traceDropped = 0u;
}

static void traceLogPacket(uint8_t packetType, uint8_t incoming,
                           uint8_t *packet, uint16_t length) {
  if (packet == NULL) {
    return;
  }
  if (s_traceCount == TRACE_CAPACITY) {
    s_traceRead = (s_traceRead + 1u) % TRACE_CAPACITY;
    --s_traceCount;
    ++s_traceDropped;
  }
  const size_t index = (s_traceRead + s_traceCount) % TRACE_CAPACITY;
  trace_record_t *record = &s_trace[index];
  memset(record, 0, sizeof(*record));
  record->sequence = ++s_traceSequence;
  record->original_length = length;
  record->packet_type = packetType;
  record->incoming = incoming != 0u;
  record->captured_length =
      length <= TRACE_PACKET_BYTES ? (uint8_t)length : TRACE_PACKET_BYTES;
  memcpy(record->packet, packet, record->captured_length);
  ++s_traceCount;
}

static void traceLogMessage(int level, const char *format, va_list arguments) {
  (void)level;
  (void)format;
  (void)arguments;
}

static const hci_dump_t s_hciDump = {
    .reset = traceReset,
    .log_packet = traceLogPacket,
    .log_message = traceLogMessage,
};

static bool inquiryAddressByte(const trace_record_t *record, size_t index) {
  if (record->packet_type != TRACE_HCI_EVENT_PACKET ||
      record->captured_length < 3u) {
    return false;
  }
  const uint8_t event = record->packet[0];
  if (event != TRACE_HCI_EVENT_INQUIRY_RESULT &&
      event != TRACE_HCI_EVENT_INQUIRY_RESULT_WITH_RSSI &&
      event != TRACE_HCI_EVENT_EXTENDED_INQUIRY_RESULT) {
    return false;
  }
  const size_t addressBytes = (size_t)record->packet[2] * 6u;
  return index >= 3u && index < 3u + addressBytes;
}

static size_t traceVisibleLength(const trace_record_t *record) {
  if (record->packet_type == TRACE_HCI_COMMAND_PACKET &&
      record->captured_length >= 3u) {
    const uint16_t opcode = readLe16(record->packet);
    switch (opcode) {
    case TRACE_HCI_OPCODE_INQUIRY:
    case TRACE_HCI_OPCODE_INQUIRY_CANCEL:
    case 0x080fu: /* Write Default Link Policy Settings. */
    case 0x0c01u: /* Set Event Mask. */
    case 0x0c18u: /* Write Page Timeout. */
    case 0x0c1au: /* Write Scan Enable. */
    case 0x0c24u: /* Write Class of Device. */
    case 0x0c31u: /* Set Controller to Host Flow Control. */
    case 0x0c33u: /* Host Buffer Size. */
    case 0x0c45u: /* Write Inquiry Mode. */
    case 0x0c56u: /* Write Simple Pairing Mode. */
    case 0x0c63u: /* Set Event Mask Page 2. */
    case 0x0c7au: /* Write Secure Connections Host Support. */
      return record->captured_length;
    default:
      return 3u;
    }
  }
  if (record->packet_type == TRACE_HCI_ACL_PACKET) {
    return record->captured_length < 4u ? record->captured_length : 4u;
  }
  if (record->packet_type != TRACE_HCI_EVENT_PACKET ||
      record->captured_length < 2u) {
    return record->captured_length;
  }
  switch (record->packet[0]) {
  case TRACE_HCI_EVENT_INQUIRY_COMPLETE:
  case TRACE_HCI_EVENT_INQUIRY_RESULT:
  case TRACE_HCI_EVENT_COMMAND_STATUS:
  case TRACE_HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
    return record->captured_length;
  case TRACE_HCI_EVENT_EXTENDED_INQUIRY_RESULT:
    /* Preserve inquiry metadata through RSSI, but never expose the EIR body. */
    return record->captured_length < 17u ? record->captured_length : 17u;
  case TRACE_HCI_EVENT_COMMAND_COMPLETE:
    if (record->captured_length >= 5u) {
      const uint16_t opcode = readLe16(&record->packet[3]);
      if (opcode == TRACE_HCI_OPCODE_READ_LOCAL_VERSION ||
          opcode == TRACE_HCI_OPCODE_READ_LOCAL_COMMANDS ||
          opcode == TRACE_HCI_OPCODE_READ_LOCAL_FEATURES ||
          opcode == TRACE_HCI_OPCODE_READ_BUFFER_SIZE) {
        return record->captured_length;
      }
    }
    return record->captured_length < 6u ? record->captured_length : 6u;
  default:
    return 2u;
  }
}

static uint16_t traceCode(const trace_record_t *record) {
  if (record->packet_type == TRACE_HCI_COMMAND_PACKET &&
      record->captured_length >= 2u) {
    return readLe16(record->packet);
  }
  return record->captured_length != 0u ? record->packet[0] : 0u;
}

static const char *traceKind(uint8_t packetType) {
  switch (packetType) {
  case TRACE_HCI_COMMAND_PACKET:
    return "CMD";
  case TRACE_HCI_ACL_PACKET:
    return "ACL";
  case TRACE_HCI_EVENT_PACKET:
    return "EVT";
  default:
    return "OTHER";
  }
}

static void tracePrintRecord(const trace_record_t *record) {
  char bytes[TRACE_PACKET_BYTES * 3u + 1u];
  size_t used = 0u;
  const size_t visible = traceVisibleLength(record);
  for (size_t index = 0u; index < visible && used + 3u < sizeof(bytes);
       ++index) {
    if (inquiryAddressByte(record, index)) {
      bytes[used++] = 'X';
      bytes[used++] = 'X';
    } else {
      const int written = snprintf(&bytes[used], sizeof(bytes) - used, "%02X",
                                   record->packet[index]);
      if (written != 2) {
        break;
      }
      used += 2u;
    }
    if (index + 1u < visible) {
      bytes[used++] = ' ';
    }
  }
  bytes[used] = '\0';
  deb("JHHCI seq=%lu dir=%s kind=%s code=0x%04X size=%u captured=%u "
      "bytes=%s%s",
      (unsigned long)record->sequence, record->incoming ? "RX" : "TX",
      traceKind(record->packet_type), (unsigned)traceCode(record),
      (unsigned)record->original_length, (unsigned)record->captured_length,
      bytes, visible < record->captured_length ? " [redacted]" : "");
}

static void traceDump(void) {
  deb("JHHCI-DUMP records=%u dropped=%lu", (unsigned)s_traceCount,
      (unsigned long)s_traceDropped);
  while (s_traceCount != 0u) {
    tracePrintRecord(&s_trace[s_traceRead]);
    s_traceRead = (s_traceRead + 1u) % TRACE_CAPACITY;
    --s_traceCount;
  }
}

static void printInfo(void) {
  hal_bluetooth_classic_info_t info = {0};
  jh_btstack_cyw43_transport_snapshot_t transport = {0};
  jh_rp2040_cyw43_gspi_clock_t clock = {0};
  const hal_status_t status = hal_bluetooth_classic_get_info(s_classic, &info);
  jh_btstack_cyw43_transport_snapshot(&transport);
  const hal_status_t clockStatus = jh_rp2040_cyw43_gspi_get_clock(&clock);
  deb("JHHCI-INFO status=%s state=%u scan=%u pending=%u queueDropped=%lu "
      "rx=%lu events=%lu aclRx=%lu tx=%lu commands=%lu aclTx=%lu "
      "budgetHits=%lu trace=%u traceDropped=%lu",
      hal_status_to_string(status), (unsigned)info.state,
      info.scan_active ? 1u : 0u, (unsigned)info.pending_scan_results,
      (unsigned long)info.dropped_scan_results,
      (unsigned long)transport.rx_packets,
      (unsigned long)transport.rx_event_packets,
      (unsigned long)transport.rx_acl_packets,
      (unsigned long)transport.tx_packets,
      (unsigned long)transport.tx_command_packets,
      (unsigned long)transport.tx_acl_packets,
      (unsigned long)transport.drain_budget_hits, (unsigned)s_traceCount,
      (unsigned long)s_traceDropped);
  deb("JHHCI-CLOCK status=%s sys=%lu target=%lu actual=%lu divider=%u.%u "
      "program=%u",
      hal_status_to_string(clockStatus), (unsigned long)clock.clk_sys_hz,
      (unsigned long)clock.target_gspi_hz, (unsigned long)clock.actual_gspi_hz,
      (unsigned)clock.divider_int, (unsigned)clock.divider_frac8,
      (unsigned)clock.program);
}

static void executeCommand(void) {
  s_command[s_commandLength] = '\0';
  if (strcmp(s_command, "SCAN") == 0) {
    traceReset();
    const hal_status_t status =
        hal_bluetooth_classic_scan_start(s_classic, 10000u);
    deb("JHHCI-SCAN status=%s", hal_status_to_string(status));
  } else if (strcmp(s_command, "SCAN30") == 0) {
    traceReset();
    const hal_status_t status =
        hal_bluetooth_classic_scan_start(s_classic, 30000u);
    deb("JHHCI-SCAN30 status=%s", hal_status_to_string(status));
  } else if (strcmp(s_command, "STOP") == 0) {
    const hal_status_t status = hal_bluetooth_classic_scan_stop(s_classic);
    deb("JHHCI-STOP status=%s", hal_status_to_string(status));
  } else if (strcmp(s_command, "DUMP") == 0) {
    traceDump();
  } else if (strcmp(s_command, "RESET") == 0) {
    traceReset();
    deb("JHHCI-RESET");
  } else if (strcmp(s_command, "INFO") == 0) {
    printInfo();
  } else {
    derr("JHHCI-COMMAND invalid");
  }
  s_commandLength = 0u;
}

static void serviceCommands(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      return;
    }
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      if (s_commandLength != 0u) {
        executeCommand();
      }
      continue;
    }
    if (s_commandLength + 1u >= sizeof(s_command)) {
      s_commandLength = 0u;
      derr("JHHCI-COMMAND overflow");
      continue;
    }
    s_command[s_commandLength++] = (char)value;
  }
}

static void drainScanResults(void) {
  for (;;) {
    hal_bluetooth_classic_scan_result_t result = {0};
    const hal_status_t status =
        hal_bluetooth_classic_scan_result_next(s_classic, &result);
    if (status == HAL_EOVERFLOW) {
      derr("JHHCI-PEER queue-overflow");
      continue;
    }
    if (status == HAL_EAGAIN) {
      return;
    }
    if (status != HAL_OK) {
      derr("JHHCI-PEER status=%s", hal_status_to_string(status));
      return;
    }
    deb("JHHCI-PEER class=0x%06lX services=0x%02lX resolved=%u "
        "nameLength=%u rssiValid=%u rssi=%d",
        (unsigned long)result.class_of_device, (unsigned long)result.services,
        result.services_resolved ? 1u : 0u, (unsigned)result.name_length,
        result.rssi_valid ? 1u : 0u, result.rssi_valid ? (int)result.rssi : 0);
  }
}

void app_start(void) {
  hal_debug_init_default();
  hci_dump_init(&s_hciDump);
  hci_dump_enable_packet_log(true);
  deb("JaszczurHAL private Bluetooth Classic HCI trace");
  deb("Commands: SCAN, SCAN30, STOP, DUMP, RESET, INFO");
}

void app_task0(void) {
  if (!s_started) {
    s_started = true;
    const hal_status_t status = hal_bluetooth_classic_open(&s_classic);
    if (status != HAL_OK) {
      derr("JHHCI-OPEN status=%s", hal_status_to_string(status));
    }
  }
  if (s_classic == NULL) {
    hal_delay_ms(1u);
    return;
  }
  const hal_status_t pollStatus = hal_bluetooth_classic_poll(s_classic);
  if (pollStatus != HAL_OK && pollStatus != HAL_EOVERFLOW) {
    derr("JHHCI-POLL status=%s", hal_status_to_string(pollStatus));
  }
  drainScanResults();
  serviceCommands();
  if (!s_readyReported) {
    hal_bluetooth_classic_info_t info = {0};
    if (hal_bluetooth_classic_get_info(s_classic, &info) == HAL_OK &&
        info.state == HAL_BLUETOOTH_CLASSIC_STATE_READY) {
      s_readyReported = true;
      deb("JHHCI-READY");
    }
  }
  hal_delay_ms(1u);
}
