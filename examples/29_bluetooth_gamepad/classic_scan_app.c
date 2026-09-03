#include <hal/bluetooth/hal_bluetooth_classic.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  CLASSIC_COMMAND_CAPACITY = 32u,
  CLASSIC_OBSERVED_PEER_CAPACITY = HAL_BLUETOOTH_CLASSIC_SCAN_QUEUE_DEPTH,
  CLASSIC_EXAMPLE_PROFILE_ID = 0x8501u,
};

typedef struct {
  hal_bluetooth_classic_address_t address;
  bool used;
  bool sdp_pending;
} observed_peer_t;

static hal_bluetooth_classic_t s_classic = NULL;
static hal_status_t s_runtimeStatus = HAL_NONE;
static observed_peer_t s_observedPeers[CLASSIC_OBSERVED_PEER_CAPACITY];
static char s_command[CLASSIC_COMMAND_CAPACITY];
static size_t s_commandLength;
static size_t s_sdpPeerIndex;
static bool s_started;
static bool s_initialScanStarted;
static bool s_sdpActive;
static bool s_lastPairingPending;

static bool addressEqual(const hal_bluetooth_classic_address_t *left,
                         const hal_bluetooth_classic_address_t *right) {
  return memcmp(left->bytes, right->bytes, HAL_BLUETOOTH_CLASSIC_ADDRESS_LEN) ==
         0;
}

static size_t rememberPeer(const hal_bluetooth_classic_address_t *address) {
  size_t freeIndex = CLASSIC_OBSERVED_PEER_CAPACITY;
  for (size_t index = 0u; index < CLASSIC_OBSERVED_PEER_CAPACITY; ++index) {
    if (s_observedPeers[index].used &&
        addressEqual(&s_observedPeers[index].address, address)) {
      return index;
    }
    if (!s_observedPeers[index].used &&
        freeIndex == CLASSIC_OBSERVED_PEER_CAPACITY) {
      freeIndex = index;
    }
  }
  if (freeIndex < CLASSIC_OBSERVED_PEER_CAPACITY) {
    s_observedPeers[freeIndex].address = *address;
    s_observedPeers[freeIndex].used = true;
  }
  return freeIndex;
}

static void printInfo(void) {
  hal_bluetooth_classic_info_t info = {0};
  const hal_status_t status = hal_bluetooth_classic_get_info(s_classic, &info);
  if (status != HAL_OK) {
    derr("Classic info failed: %s", hal_status_to_string(status));
    return;
  }
  deb("Classic info state=%u status=%s generation=%lu scan=%u pending=%u "
      "dropped=%lu pairing=%u method=%u peers=%u",
      (unsigned)info.state, hal_status_to_string(info.last_status),
      (unsigned long)info.generation, info.scan_active ? 1u : 0u,
      (unsigned)info.pending_scan_results,
      (unsigned long)info.dropped_scan_results, info.pairing_pending ? 1u : 0u,
      (unsigned)info.pairing_method, (unsigned)info.peer_count);
}

static bool parsePeerIndex(const char *prefix, size_t *outIndex) {
  const size_t prefixLength = strlen(prefix);
  if (strncmp(s_command, prefix, prefixLength) != 0) {
    return false;
  }
  const char *text = &s_command[prefixLength];
  if (*text < '0' || *text > '9' || text[1] != '\0') {
    *outIndex = CLASSIC_OBSERVED_PEER_CAPACITY;
    return true;
  }
  *outIndex = (size_t)(*text - '0');
  return true;
}

static hal_status_t requireObservedPeer(size_t index) {
  return index < CLASSIC_OBSERVED_PEER_CAPACITY && s_observedPeers[index].used
             ? HAL_OK
             : HAL_ENOENT;
}

static void executeCommand(void) {
  s_command[s_commandLength] = '\0';
  hal_status_t status = HAL_EINVAL;
  size_t peerIndex = CLASSIC_OBSERVED_PEER_CAPACITY;

  if (strcmp(s_command, "SCAN") == 0) {
    status = hal_bluetooth_classic_scan_start(s_classic, 10000u);
  } else if (strcmp(s_command, "STOP") == 0) {
    status = hal_bluetooth_classic_scan_stop(s_classic);
  } else if (strcmp(s_command, "AUTHORIZE") == 0) {
    status = hal_bluetooth_classic_pairing_authorize(s_classic);
  } else if (strcmp(s_command, "REJECT") == 0) {
    status = hal_bluetooth_classic_pairing_reject(s_classic);
  } else if (strcmp(s_command, "INFO") == 0) {
    status = HAL_OK;
  } else if (parsePeerIndex("SDP ", &peerIndex)) {
    status = requireObservedPeer(peerIndex);
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_sdp_query(
          s_classic, &s_observedPeers[peerIndex].address);
      if (status == HAL_OK) {
        s_sdpActive = true;
        s_sdpPeerIndex = peerIndex;
      }
    }
  } else if (parsePeerIndex("PAIR ", &peerIndex)) {
    status = requireObservedPeer(peerIndex);
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_pair(s_classic,
                                          &s_observedPeers[peerIndex].address);
    }
  } else if (parsePeerIndex("SAVE ", &peerIndex)) {
    status = requireObservedPeer(peerIndex);
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_peer_save(
          s_classic, &s_observedPeers[peerIndex].address,
          CLASSIC_EXAMPLE_PROFILE_ID);
    }
  } else if (parsePeerIndex("FORGET ", &peerIndex)) {
    status = requireObservedPeer(peerIndex);
    if (status == HAL_OK) {
      status = hal_bluetooth_classic_peer_forget(
          s_classic, &s_observedPeers[peerIndex].address);
    }
  }

  deb("Classic command '%s': %s", s_command, hal_status_to_string(status));
  printInfo();
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
      if (s_commandLength > 0u) {
        executeCommand();
      }
      continue;
    }
    if (s_commandLength + 1u >= sizeof(s_command)) {
      s_commandLength = 0u;
      derr("Classic command overflow");
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
      derr("Classic scan queue overflow; continuing with retained results");
      continue;
    }
    if (status == HAL_EAGAIN) {
      return;
    }
    if (status != HAL_OK) {
      derr("Classic scan result failed: %s", hal_status_to_string(status));
      return;
    }

    const size_t peerIndex = rememberPeer(&result.address);
    if (peerIndex == CLASSIC_OBSERVED_PEER_CAPACITY) {
      derr("Classic observed-peer table is full");
      continue;
    }
    deb("Classic peer index=%u name='%s' class=0x%06lX rssi=%d "
        "services=0x%02lX resolved=%u",
        (unsigned)peerIndex, result.name, (unsigned long)result.class_of_device,
        result.rssi_valid ? (int)result.rssi : 0,
        (unsigned long)result.services, result.services_resolved ? 1u : 0u);

    if (result.services_resolved) {
      s_observedPeers[peerIndex].sdp_pending = false;
      if (s_sdpActive && s_sdpPeerIndex == peerIndex) {
        s_sdpActive = false;
      }
    } else {
      s_observedPeers[peerIndex].sdp_pending = true;
    }
  }
}

static void servicePendingSdp(const hal_bluetooth_classic_info_t *info) {
  if (info->scan_active || s_sdpActive) {
    return;
  }
  for (size_t index = 0u; index < CLASSIC_OBSERVED_PEER_CAPACITY; ++index) {
    if (!s_observedPeers[index].used || !s_observedPeers[index].sdp_pending) {
      continue;
    }
    const hal_status_t status = hal_bluetooth_classic_sdp_query(
        s_classic, &s_observedPeers[index].address);
    if (status == HAL_OK) {
      s_sdpActive = true;
      s_sdpPeerIndex = index;
      deb("Classic SDP query started for peer %u", (unsigned)index);
    } else if (status != HAL_EBUSY) {
      s_observedPeers[index].sdp_pending = false;
      derr("Classic SDP query for peer %u failed: %s", (unsigned)index,
           hal_status_to_string(status));
    }
    return;
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("JaszczurHAL generic Bluetooth Classic scan example");
  deb("Commands: SCAN, STOP, SDP n, PAIR n, AUTHORIZE, REJECT, SAVE n, "
      "FORGET n, INFO");
}

void app_task0(void) {
  if (!s_started) {
    s_started = true;
    s_runtimeStatus = hal_bluetooth_classic_open(&s_classic);
    if (s_runtimeStatus != HAL_OK) {
      derr("Classic open failed: %s", hal_status_to_string(s_runtimeStatus));
    }
  }
  if (s_runtimeStatus != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }

  const hal_status_t pollStatus = hal_bluetooth_classic_poll(s_classic);
  if (pollStatus != HAL_OK && pollStatus != HAL_EOVERFLOW) {
    s_runtimeStatus = pollStatus;
    derr("Classic poll failed: %s", hal_status_to_string(pollStatus));
    hal_delay_ms(1u);
    return;
  }

  hal_bluetooth_classic_info_t info = {0};
  if (hal_bluetooth_classic_get_info(s_classic, &info) != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }

  drainScanResults();
  servicePendingSdp(&info);
  serviceCommands();

  if (info.state == HAL_BLUETOOTH_CLASSIC_STATE_READY &&
      !s_initialScanStarted) {
    const hal_status_t status =
        hal_bluetooth_classic_scan_start(s_classic, 10000u);
    if (status == HAL_OK) {
      s_initialScanStarted = true;
      deb("Classic inquiry started for 10 seconds");
    } else {
      derr("Classic inquiry failed: %s", hal_status_to_string(status));
    }
  }

  if (info.pairing_pending != s_lastPairingPending) {
    s_lastPairingPending = info.pairing_pending;
    printInfo();
  }
  hal_delay_ms(1u);
}
