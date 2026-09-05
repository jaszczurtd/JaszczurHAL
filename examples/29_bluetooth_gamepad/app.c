#include <hal/bluetooth/hal_gamepad.h>
#include <hal/bluetooth/jh_bluetooth_classic_hid_memory_probe.h>
#include <hal/bluetooth/jh_btstack_diagnostics.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/core/jh_endian.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <string.h>

enum {
  SUMMARY_PERIOD_MS = 5000u,
  RECONNECT_RETRY_MS = 250u,
  STACK_GUARD_BYTES = 32u,
  STACK_PROBE_SAFETY_BYTES = 256u,
  STACK_PROBE_PATTERN = 0xa5u,
};

#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM)
extern char __StackBottom;
extern char __StackTop;
#endif

#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
#include <hal/bluetooth/hal_ble.h>
#include <hal/bluetooth/jh_gamepad_bond_kv_provider.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#endif

static hal_gamepad_t s_gamepad = NULL;
static hal_status_t s_runtimeStatus = HAL_NONE;
static hal_gamepad_state_t s_previousState = HAL_GAMEPAD_STATE_UNINITIALIZED;
static bool s_started = false;
static bool s_pairingOpened = false;
static bool s_pairingReplySent = false;
static bool s_reconnectStarted = false;
static bool s_reconnectAttempted = false;
static uint32_t s_reconnectAttemptMs;
static uint32_t s_lastSummaryMs;

static void stackProbeStart(void) {
#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM)
  volatile uint8_t marker = 0u;
  const uintptr_t bottom = (uintptr_t)&__StackBottom + STACK_GUARD_BYTES;
  const uintptr_t current = (uintptr_t)&marker;
  if (current <= bottom + STACK_PROBE_SAFETY_BYTES) {
    return;
  }
  volatile uint8_t *cursor = (volatile uint8_t *)bottom;
  const uintptr_t limit = current - STACK_PROBE_SAFETY_BYTES;
  while ((uintptr_t)cursor < limit) {
    *cursor++ = STACK_PROBE_PATTERN;
  }
#endif
}

static size_t stackHighWater(void) {
#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM)
  const uintptr_t bottom = (uintptr_t)&__StackBottom + STACK_GUARD_BYTES;
  const uintptr_t top = (uintptr_t)&__StackTop;
  const volatile uint8_t *cursor = (const volatile uint8_t *)bottom;
  while ((uintptr_t)cursor < top && *cursor == STACK_PROBE_PATTERN) {
    ++cursor;
  }
  return top - (uintptr_t)cursor;
#else
  return 0u;
#endif
}

static void reportResourceInfo(void) {
  jh_bluetooth_classic_hid_memory_snapshot_t pools = {0};
  jh_btstack_cyw43_transport_snapshot_t transport = {0};
  jh_bluetooth_classic_hid_memory_probe_snapshot(&pools);
  jh_btstack_cyw43_transport_snapshot(&transport);
  deb("C10 resources stack=%u hci=%u/%u/%u l2cap-services=%u/%u/%u "
      "l2cap-channels=%u/%u/%u link-keys=%u/%u/%u hid=%u/%u/%u "
      "transport=%s rx=%lu/%lu/%lu tx=%lu/%lu/%lu drain=%lu",
      (unsigned)stackHighWater(), (unsigned)pools.hci_connections.high_water,
      (unsigned)pools.hci_connections.capacity,
      (unsigned)pools.hci_connections.allocation_failures,
      (unsigned)pools.l2cap_services.high_water,
      (unsigned)pools.l2cap_services.capacity,
      (unsigned)pools.l2cap_services.allocation_failures,
      (unsigned)pools.l2cap_channels.high_water,
      (unsigned)pools.l2cap_channels.capacity,
      (unsigned)pools.l2cap_channels.allocation_failures,
      (unsigned)pools.link_keys.high_water, (unsigned)pools.link_keys.capacity,
      (unsigned)pools.link_keys.allocation_failures,
      (unsigned)pools.hid_connections.high_water,
      (unsigned)pools.hid_connections.capacity,
      (unsigned)pools.hid_connections.allocation_failures,
      hal_status_to_string(transport.last_status),
      (unsigned long)transport.rx_packets,
      (unsigned long)transport.rx_event_packets,
      (unsigned long)transport.rx_acl_packets,
      (unsigned long)transport.tx_packets,
      (unsigned long)transport.tx_command_packets,
      (unsigned long)transport.tx_acl_packets,
      (unsigned long)transport.drain_budget_hits);
}

#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
enum {
  BLE_SCAN_INTERVAL_60_MS = 0x0060u,
  BLE_SCAN_WINDOW_60_MS = 0x0060u,
  BLE_MANUFACTURER_DATA_TYPE = 0xffu,
  TELTONIKA_COMPANY_ID = 0x089au,
  GAMEPAD_BOND_KV_KEY = 0xd001u,
  COMMAND_CAPACITY = 24u,
};

static jh_gamepad_bond_kv_context_t s_bondContext;
static hal_gamepad_bond_provider_t s_bondProvider;
static uint32_t s_bleReports;
static uint32_t s_teltonikaReports;
static char s_command[COMMAND_CAPACITY];
static size_t s_commandLength;
static hal_status_t s_closeClassicStatus = HAL_NONE;
static hal_status_t s_closeBleStatus = HAL_NONE;
static hal_status_t s_reopenBleStatus = HAL_NONE;

static bool isTeltonikaReport(const hal_ble_advertising_report_t *report) {
  size_t offset = 0u;
  hal_ble_advertising_field_t field = {0};
  while (hal_ble_advertising_field_next(report, &offset, &field) == HAL_OK) {
    if (field.type == BLE_MANUFACTURER_DATA_TYPE && field.data_length >= 2u &&
        jh_load_le16(field.data) == TELTONIKA_COMPANY_ID) {
      return true;
    }
  }
  return false;
}

static void drainBleReports(void) {
  for (;;) {
    hal_ble_advertising_report_t report = {0};
    const hal_status_t status = hal_ble_scan_report_next(&report);
    if (status == HAL_OK) {
      ++s_bleReports;
      if (isTeltonikaReport(&report)) {
        ++s_teltonikaReports;
        deb("C10 BLE teltonika=%lu reports=%lu rssi=%d",
            (unsigned long)s_teltonikaReports, (unsigned long)s_bleReports,
            (int)report.rssi);
      }
      continue;
    }
    if (status == HAL_EOVERFLOW) {
      derr("C10 BLE scan report queue overflow");
      continue;
    }
    return;
  }
}

static void onBleEvent(const hal_ble_event_t *event, void *context) {
  (void)context;
  switch (event->type) {
  case HAL_BLE_EVENT_CONTROLLER_READY:
    deb("C10 BLE controller ready");
    break;
  case HAL_BLE_EVENT_SCAN_STARTED:
    deb("C10 BLE passive scan started");
    break;
  case HAL_BLE_EVENT_SCAN_STOPPED:
    deb("C10 BLE passive scan stopped");
    break;
  case HAL_BLE_EVENT_ERROR:
    derr("C10 BLE error: %s", hal_status_to_string(event->status));
    break;
  case HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE:
  case HAL_BLE_EVENT_ADVERTISING_STARTED:
  case HAL_BLE_EVENT_ADVERTISING_STOPPED:
  case HAL_BLE_EVENT_CONNECTED:
  case HAL_BLE_EVENT_DISCONNECTED:
  case HAL_BLE_EVENT_MTU_UPDATED:
    break;
  }
}

static hal_status_t startBleScan(void) {
  const hal_ble_scan_config_t config = {
      .interval = BLE_SCAN_INTERVAL_60_MS,
      .window = BLE_SCAN_WINDOW_60_MS,
      .filter_duplicates = true,
  };
  return hal_ble_scan_start(&config);
}

static hal_status_t openBleObserver(void) {
  hal_status_t status = hal_ble_initialize();
  if (status == HAL_OK) {
    status = hal_ble_set_event_callback(onBleEvent, NULL);
  }
  if (status == HAL_OK) {
    status = startBleScan();
  }
  if (status != HAL_OK) {
    (void)hal_ble_deinitialize();
  }
  return status;
}

static hal_status_t initializeBondProvider(void) {
  uint16_t eepromSize = 0u;
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (status == HAL_OK) {
    status = hal_eeprom_size_ex(&eepromSize);
  }
  if (status == HAL_OK) {
    status = hal_kv_init_ex(0u, eepromSize);
  }
  if (status == HAL_OK) {
    s_bondProvider =
        jh_gamepad_bond_kv_provider(&s_bondContext, GAMEPAD_BOND_KV_KEY);
  }
  return status;
}

static hal_status_t openGamepad(void) {
  s_pairingOpened = false;
  s_pairingReplySent = false;
  s_reconnectStarted = false;
  s_reconnectAttempted = false;
  return hal_gamepad_open_ex(&s_gamepad, &s_bondProvider);
}
#endif

static const char *stateName(hal_gamepad_state_t state) {
  switch (state) {
  case HAL_GAMEPAD_STATE_UNINITIALIZED:
    return "uninitialized";
  case HAL_GAMEPAD_STATE_STARTING:
    return "starting";
  case HAL_GAMEPAD_STATE_READY:
    return "ready";
  case HAL_GAMEPAD_STATE_DISCOVERING:
    return "discovering";
  case HAL_GAMEPAD_STATE_CONNECTING:
    return "connecting";
  case HAL_GAMEPAD_STATE_CONNECTED:
    return "connected";
  case HAL_GAMEPAD_STATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static hal_status_t initializeRuntime(void) {
  hal_status_t status = HAL_OK;
  jh_bluetooth_classic_hid_memory_probe_reset();
#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  status = initializeBondProvider();
  if (status != HAL_OK) {
    return status;
  }
  status = hal_ble_initialize();
  if (status != HAL_OK) {
    return status;
  }
  status = openGamepad();
  if (status != HAL_OK) {
    (void)hal_ble_deinitialize();
    return status;
  }

  status = hal_gamepad_close(s_gamepad);
  s_closeClassicStatus = status;
  deb("C10 lifecycle close-classic-with-ble=%s", hal_status_to_string(status));
  s_gamepad = NULL;
  if (status != HAL_OK) {
    (void)hal_ble_deinitialize();
    return status;
  }
  status = openGamepad();
  if (status != HAL_OK) {
    (void)hal_ble_deinitialize();
    return status;
  }

  status = hal_ble_deinitialize();
  s_closeBleStatus = status;
  deb("C10 lifecycle close-ble-with-classic=%s", hal_status_to_string(status));
  if (status != HAL_OK) {
    (void)hal_gamepad_close(s_gamepad);
    s_gamepad = NULL;
    return status;
  }
  status = openBleObserver();
  s_reopenBleStatus = status;
  deb("C10 lifecycle reopen-ble-with-classic=%s", hal_status_to_string(status));
  if (status != HAL_OK) {
    (void)hal_gamepad_close(s_gamepad);
    s_gamepad = NULL;
  }
#else
  status = hal_gamepad_open(&s_gamepad);
#endif
  return status;
}

static void printSnapshot(const hal_gamepad_snapshot_t *snapshot) {
  deb("gamepad generation=%lu connected=%u buttons=0x%08lX dpad=0x%02X "
      "axes=0x%03X x=%d y=%d rx=%d ry=%d",
      (unsigned long)snapshot->generation, snapshot->connected ? 1u : 0u,
      (unsigned long)snapshot->buttons, (unsigned)snapshot->dpad,
      (unsigned)snapshot->axes_present, (int)snapshot->axes[HAL_GAMEPAD_AXIS_X],
      (int)snapshot->axes[HAL_GAMEPAD_AXIS_Y],
      (int)snapshot->axes[HAL_GAMEPAD_AXIS_RX],
      (int)snapshot->axes[HAL_GAMEPAD_AXIS_RY]);
}

static void drainSnapshots(void) {
  for (;;) {
    hal_gamepad_snapshot_t snapshot = {0};
    const hal_status_t status = hal_gamepad_snapshot_next(s_gamepad, &snapshot);
    if (status == HAL_OK) {
      printSnapshot(&snapshot);
      continue;
    }
    if (status == HAL_EOVERFLOW) {
      derr("gamepad input queue overflow; continuing with retained state");
      continue;
    }
    if (status != HAL_EAGAIN && status != HAL_EUNINIT) {
      derr("gamepad snapshot failed: %s", hal_status_to_string(status));
    }
    return;
  }
}

static void handleState(const hal_gamepad_info_t *info) {
  if (info->state != s_previousState) {
    deb("gamepad state=%s status=%s known=%u pending=%u dropped=%lu",
        stateName(info->state), hal_status_to_string(info->last_status),
        info->known_device ? 1u : 0u, info->pairing_pending ? 1u : 0u,
        (unsigned long)info->dropped_snapshots);
    s_previousState = info->state;
  }

  if (!info->pairing_pending) {
    s_pairingReplySent = false;
  } else if (!s_pairingReplySent) {
    const hal_status_t status = hal_gamepad_pairing_authorize(s_gamepad);
    if (status == HAL_OK) {
      s_pairingReplySent = true;
      deb("gamepad pairing authorized");
    } else {
      derr("gamepad pairing authorization failed: %s",
           hal_status_to_string(status));
    }
  }

  if (info->state == HAL_GAMEPAD_STATE_CONNECTED) {
    s_reconnectStarted = false;
    s_reconnectAttempted = false;
    return;
  }
  if (info->state != HAL_GAMEPAD_STATE_READY) {
    return;
  }
  s_reconnectStarted = false;
  if (!info->known_device && !info->pairing_window_open) {
    s_pairingOpened = false;
  }
  if (info->known_device && !s_reconnectStarted) {
    const uint32_t now = hal_millis();
    if (s_reconnectAttempted &&
        !hal_elapsed_u32(now, s_reconnectAttemptMs, RECONNECT_RETRY_MS)) {
      return;
    }
    s_reconnectAttempted = true;
    s_reconnectAttemptMs = now;
    const hal_status_t status = hal_gamepad_reconnect(s_gamepad);
    if (status == HAL_OK) {
      s_reconnectStarted = true;
    } else if (status != HAL_EBUSY && status != HAL_EAGAIN) {
      derr("gamepad reconnect failed: %s", hal_status_to_string(status));
    }
  } else if (!info->known_device && !s_pairingOpened) {
    const hal_status_t status = hal_gamepad_pairing_open(s_gamepad);
    if (status == HAL_OK) {
      s_pairingOpened = true;
      deb("gamepad pairing window opened");
    } else {
      derr("gamepad pairing window failed: %s", hal_status_to_string(status));
    }
  }
}

#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
static void reportCombinedInfo(void) {
  hal_ble_info_t ble = {0};
  hal_gamepad_info_t gamepad = {0};
  const hal_status_t bleStatus = hal_ble_get_info(&ble);
  const hal_status_t gamepadStatus = hal_gamepad_get_info(s_gamepad, &gamepad);
  deb("C10 info runtime=%s lifecycle=%s/%s/%s ble=%s state=%u scan=%u "
      "reports=%lu "
      "teltonika=%lu pending=%lu dropped=%lu gamepad=%s state=%s known=%u "
      "pairing=%u snapshots=%lu dropped=%lu",
      hal_status_to_string(s_runtimeStatus),
      hal_status_to_string(s_closeClassicStatus),
      hal_status_to_string(s_closeBleStatus),
      hal_status_to_string(s_reopenBleStatus), hal_status_to_string(bleStatus),
      (unsigned)ble.state, ble.scan_requested ? 1u : 0u,
      (unsigned long)s_bleReports, (unsigned long)s_teltonikaReports,
      (unsigned long)ble.pending_scan_reports,
      (unsigned long)ble.dropped_scan_reports,
      hal_status_to_string(gamepadStatus), stateName(gamepad.state),
      gamepad.known_device ? 1u : 0u, gamepad.pairing_pending ? 1u : 0u,
      (unsigned long)gamepad.pending_snapshots,
      (unsigned long)gamepad.dropped_snapshots);
  reportResourceInfo();
}

static void handleCommand(void) {
  hal_status_t status = HAL_EINVAL;
  if (strcmp(s_command, "INFO") == 0) {
    reportCombinedInfo();
    status = HAL_OK;
  } else if (strcmp(s_command, "BLE_START") == 0) {
    status = startBleScan();
  } else if (strcmp(s_command, "BLE_STOP") == 0) {
    status = hal_ble_scan_stop();
  } else if (strcmp(s_command, "DISCONNECT") == 0) {
    status = hal_gamepad_disconnect(s_gamepad);
  }
  deb("C10 command=%s status=%s", s_command, hal_status_to_string(status));
}

static void pollCommands(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      return;
    }
    if (value == '\r' || value == '\n') {
      if (s_commandLength != 0u) {
        s_command[s_commandLength] = '\0';
        handleCommand();
        s_commandLength = 0u;
      }
      continue;
    }
    if (s_commandLength + 1u >= sizeof(s_command)) {
      s_commandLength = 0u;
      derr("C10 command overflow");
      continue;
    }
    s_command[s_commandLength++] = (char)value;
  }
}
#endif

void app_start(void) {
  stackProbeStart();
  hal_debug_init_default();
  deb("JaszczurHAL Bluetooth Classic gamepad example");
#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  deb("BLE and Bluetooth Classic share one controller runtime");
  deb("C10 commands=INFO,BLE_START,BLE_STOP,DISCONNECT");
#endif
}

void app_task0(void) {
  if (!s_started) {
    s_started = true;
    s_runtimeStatus = initializeRuntime();
    if (s_runtimeStatus != HAL_OK) {
      derr("gamepad initialize failed: %s",
           hal_status_to_string(s_runtimeStatus));
    }
  }
  if (s_runtimeStatus != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }

#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  pollCommands();
  const hal_status_t bleStatus = hal_ble_poll();
  if (bleStatus != HAL_OK && bleStatus != HAL_EOVERFLOW) {
    s_runtimeStatus = bleStatus;
    derr("BLE poll failed: %s", hal_status_to_string(bleStatus));
    hal_delay_ms(1u);
    return;
  }
  drainBleReports();
#endif

  const hal_status_t pollStatus = hal_gamepad_poll(s_gamepad);
  if (pollStatus != HAL_OK && pollStatus != HAL_EOVERFLOW) {
    s_runtimeStatus = pollStatus;
    derr("gamepad poll failed: %s", hal_status_to_string(pollStatus));
    hal_delay_ms(1u);
    return;
  }

  hal_gamepad_info_t info = {0};
  const hal_status_t infoStatus = hal_gamepad_get_info(s_gamepad, &info);
  if (infoStatus == HAL_OK) {
    handleState(&info);
  } else {
    derr("gamepad info failed: %s", hal_status_to_string(infoStatus));
  }
  drainSnapshots();
#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  const uint32_t now = hal_millis();
  if (hal_elapsed_u32(now, s_lastSummaryMs, SUMMARY_PERIOD_MS)) {
    s_lastSummaryMs = now;
    reportCombinedInfo();
  }
#else
  const uint32_t now = hal_millis();
  if (hal_elapsed_u32(now, s_lastSummaryMs, SUMMARY_PERIOD_MS)) {
    s_lastSummaryMs = now;
    reportResourceInfo();
  }
#endif
  hal_delay_ms(1u);
}
