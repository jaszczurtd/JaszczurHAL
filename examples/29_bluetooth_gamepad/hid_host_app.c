#include <hal/bluetooth/hal_bluetooth_classic.h>
#include <hal/bluetooth/hal_bluetooth_hid_host.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

#include <stdio.h>
#include <string.h>

enum {
  EXAMPLE_HID_PROFILE_RULES_ID = 1u,
  EXAMPLE_HID_COMMAND_CAPACITY = 32u,
};

static hal_bluetooth_classic_t s_classic = NULL;
static hal_bluetooth_hid_host_t s_hid = NULL;
static hal_bluetooth_classic_address_t s_selectedAddress;
static hal_status_t s_runtimeStatus = HAL_NONE;
static bool s_started = false;
static bool s_scanStarted = false;
static bool s_connectStarted = false;
static bool s_pairingPrompted = false;
static bool s_pairingAuthorized = false;
static bool s_descriptorSeen = false;
static bool s_inputSeen = false;
static bool s_peerSaved = false;
static bool s_passReported = false;
static size_t s_descriptorLength = 0u;
static char s_command[EXAMPLE_HID_COMMAND_CAPACITY];
static size_t s_commandLength = 0u;

static void selectHidPeer(const hal_bluetooth_classic_scan_result_t *result) {
  if (s_connectStarted) {
    return;
  }
  if (!result->services_resolved) {
    const hal_status_t status =
        hal_bluetooth_classic_sdp_query(s_classic, &result->address);
    if (status != HAL_OK && status != HAL_EBUSY) {
      derr("HID SDP query failed: %s", hal_status_to_string(status));
    }
    return;
  }
  if ((result->services & HAL_BLUETOOTH_CLASSIC_SERVICE_HID) == 0u) {
    return;
  }

  s_selectedAddress = result->address;
  const hal_status_t status =
      hal_bluetooth_hid_host_connect(s_hid, &s_selectedAddress);
  if (status == HAL_OK) {
    s_connectStarted = true;
    (void)hal_bluetooth_classic_scan_stop(s_classic);
    deb("Connecting to a generic HID peer named '%s'", result->name);
  } else {
    derr("HID connect failed: %s", hal_status_to_string(status));
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
    if (status != HAL_OK) {
      return;
    }
    selectHidPeer(&result);
  }
}

static void handlePairing(void) {
  hal_bluetooth_classic_info_t info = {0};
  if (hal_bluetooth_classic_get_info(s_classic, &info) != HAL_OK ||
      !info.pairing_pending || s_pairingPrompted) {
    return;
  }
  s_pairingPrompted = true;
  deb("HID pairing pending; use AUTHORIZE only after a trusted local gesture, "
      "or REJECT");
}

static void readDescriptor(void) {
  if (s_descriptorSeen) {
    return;
  }
  uint8_t descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN] = {0};
  size_t length = 0u;
  const hal_status_t status = hal_bluetooth_hid_host_descriptor(
      s_hid, descriptor, sizeof(descriptor), &length);
  if (status == HAL_OK) {
    s_descriptorSeen = true;
    s_descriptorLength = length;
    deb("Copied generic HID descriptor: %u bytes", (unsigned)length);
  } else if (status != HAL_EAGAIN) {
    derr("HID descriptor failed: %s", hal_status_to_string(status));
  }
}

static void reportAcceptance(void) {
  if (s_passReported || !s_descriptorSeen || !s_inputSeen) {
    return;
  }
  s_passReported = true;
  deb("JHC85-HID-PASS descriptor=%u rawInput=1", (unsigned)s_descriptorLength);
}

static void printInfo(void) {
  hal_bluetooth_classic_info_t classicInfo = {0};
  hal_bluetooth_hid_info_t hidInfo = {0};
  const hal_status_t classicStatus =
      hal_bluetooth_classic_get_info(s_classic, &classicInfo);
  const hal_status_t hidStatus =
      hal_bluetooth_hid_host_get_info(s_hid, &hidInfo);
  char line[192];
  const int length = snprintf(
      line, sizeof(line),
      "JHC85-HID-INFO classic=%s state=%u pairing=%u peers=%u hid=%s "
      "hidState=%u descriptor=%u input=%u saved=%u",
      hal_status_to_string(classicStatus), (unsigned)classicInfo.state,
      classicInfo.pairing_pending ? 1u : 0u, (unsigned)classicInfo.peer_count,
      hal_status_to_string(hidStatus), (unsigned)hidInfo.state,
      s_descriptorSeen ? 1u : 0u, s_inputSeen ? 1u : 0u, s_peerSaved ? 1u : 0u);
  if (length > 0 && (size_t)length < sizeof(line)) {
    hal_serial_println(line);
  }
}

static void executeCommand(void) {
  s_command[s_commandLength] = '\0';
  hal_status_t status = HAL_EINVAL;
  if (strcmp(s_command, "SCAN") == 0) {
    status = hal_bluetooth_classic_scan_start(s_classic, 10000u);
    if (status == HAL_OK) {
      s_scanStarted = true;
    }
  } else if (strcmp(s_command, "AUTHORIZE") == 0) {
    status = hal_bluetooth_classic_pairing_authorize(s_classic);
    if (status == HAL_OK) {
      s_pairingAuthorized = true;
    }
  } else if (strcmp(s_command, "REJECT") == 0) {
    status = hal_bluetooth_classic_pairing_reject(s_classic);
  } else if (strcmp(s_command, "INFO") == 0) {
    printInfo();
    status = HAL_OK;
  }
  deb("HID command %s: %s", s_command, hal_status_to_string(status));
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
      deb("HID command overflow");
      continue;
    }
    s_command[s_commandLength++] = (char)value;
  }
}

static void drainReports(void) {
  for (;;) {
    hal_bluetooth_hid_report_t report = {0};
    const hal_status_t status =
        hal_bluetooth_hid_host_report_next(s_hid, &report);
    if (status == HAL_EOVERFLOW) {
      derr("HID report queue overflow; continuing with retained reports");
      continue;
    }
    if (status != HAL_OK) {
      return;
    }
    s_inputSeen = s_inputSeen || report.type == HAL_BLUETOOTH_HID_REPORT_INPUT;
    deb("Raw HID report type=%u id=%u length=%u", (unsigned)report.type,
        (unsigned)report.report_id, (unsigned)report.length);
  }
}

static void saveValidatedPeer(void) {
  if (s_peerSaved || !s_pairingAuthorized || !s_descriptorSeen ||
      !s_inputSeen) {
    return;
  }
  const hal_status_t status = hal_bluetooth_classic_peer_save(
      s_classic, &s_selectedAddress, EXAMPLE_HID_PROFILE_RULES_ID);
  if (status == HAL_OK) {
    s_peerSaved = true;
    deb("Validated HID peer saved by the Classic manager");
  } else if (status != HAL_EAGAIN) {
    derr("HID peer save failed: %s", hal_status_to_string(status));
  }
}

void app_start(void) {
  debugInit();
  deb("JaszczurHAL generic Bluetooth Classic HID Host example");
  deb("Commands: SCAN, AUTHORIZE, REJECT, INFO");
}

void app_task0(void) {
  if (!s_started) {
    s_started = true;
    s_runtimeStatus = hal_bluetooth_classic_open(&s_classic);
    if (s_runtimeStatus == HAL_OK) {
      s_runtimeStatus = hal_bluetooth_hid_host_open(s_classic, &s_hid);
    }
    if (s_runtimeStatus != HAL_OK) {
      derr("HID Host open failed: %s", hal_status_to_string(s_runtimeStatus));
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

  hal_bluetooth_classic_info_t classicInfo = {0};
  if (!s_scanStarted &&
      hal_bluetooth_classic_get_info(s_classic, &classicInfo) == HAL_OK &&
      classicInfo.state == HAL_BLUETOOTH_CLASSIC_STATE_READY) {
    const hal_status_t status =
        hal_bluetooth_classic_scan_start(s_classic, 10000u);
    if (status == HAL_OK) {
      s_scanStarted = true;
    }
  }

  drainScanResults();
  handlePairing();
  serviceCommands();
  readDescriptor();
  drainReports();
  reportAcceptance();
  saveValidatedPeer();
  hal_delay_ms(1u);
}
