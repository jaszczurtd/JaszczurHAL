#!/usr/bin/env python3
"""Validate the private Classic HID Host build and sanitized Zero 2 capture."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys


ROOT = Path(sys.argv[1]).resolve()
FIXTURE_DIR = ROOT / "tests" / "hardware" / "bluetooth_gamepad"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def hex_bytes(value: str) -> bytes:
    return bytes.fromhex(value)


capture_path = FIXTURE_DIR / "zero2_android_dinput.json"
capture_text = capture_path.read_text(encoding="utf-8")
capture = json.loads(capture_text)

require(capture["schemaVersion"] == 1, "unsupported gamepad fixture schema")
require(
    re.search(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", capture_text) is None,
    "sanitized capture contains a Bluetooth device address",
)
require(
    capture["anonymization"]["omittedFields"]
    == [
        "bluetoothDeviceAddress",
        "hostDeviceAddress",
        "linkKey",
        "hostName",
        "usbSerialNumber",
    ],
    "fixture anonymization boundary changed",
)
device = capture["device"]
require(
    device["mode"] == "Android D-input"
    and device["transport"] == "Bluetooth Classic HID"
    and device["firmware"] == "unavailable",
    "fixture identity does not describe the characterized Zero 2 mode",
)
require(
    device["pnp"]
    == {"vendorId": "0x2dc8", "productId": "0x3230", "version": "0x0100"},
    "fixture PnP identity changed",
)

descriptor = capture["reportDescriptor"]
require(
    len(hex_bytes(descriptor["bytes"])) == descriptor["length"] == 137,
    "captured HID report descriptor length is inconsistent",
)
require(
    descriptor["fnv1a32"] == "0x6e8fcc1a",
    "captured HID report descriptor hash changed",
)
require(
    descriptor["btstackExtraction"]["length"] == 136
    and descriptor["btstackExtraction"]["fnv1a32"] == "0xe51f7bbe",
    "BTstack descriptor extraction differs from the characterized capture",
)
require(
    capture["sdp"]["pnpRecordHandle"] == "0x00010000",
    "characterized PnP record handle changed",
)
require(
    descriptor["inputReports"]
    == [
        {
            "reportId": "0x03",
            "declaredBytesIncludingId": 10,
            "observedBytes": 11,
            "note": "hidraw appends one undeclared trailing zero byte",
        },
        {
            "reportId": "0x05",
            "declaredBytesIncludingId": 21,
            "observedBytes": 0,
            "note": "constant report declared but not observed",
        },
    ],
    "fixture report-size evidence changed",
)

controls = {item["physical"]: item for item in capture["controls"]}
require(
    set(controls)
    == {"A", "B", "X", "Y", "L", "R", "Select", "Start", "Up", "Down", "Left", "Right"},
    "fixture must cover all twelve characterized controls",
)
button_masks = {
    "A": 0x0001,
    "B": 0x0002,
    "X": 0x0008,
    "Y": 0x0010,
    "L": 0x0040,
    "R": 0x0080,
    "Select": 0x0400,
    "Start": 0x0800,
}
for name, mask in button_masks.items():
    report = hex_bytes(controls[name]["pressed"])
    require(len(report) == 11 and report[0] == 0x03, f"{name}: invalid report")
    require(
        int.from_bytes(report[1:3], "little") == mask
        and int(controls[name]["buttonMask"], 0) == mask,
        f"{name}: button mask differs from captured report",
    )

for name, axis_index, value in (
    ("Up", 5, 0),
    ("Down", 5, 255),
    ("Left", 4, 0),
    ("Right", 4, 255),
):
    report = hex_bytes(controls[name]["pressed"])
    require(
        len(report) == 11
        and report[0] == 0x03
        and report[3] == 0x0F
        and report[axis_index] == value
        and controls[name]["axisValue"] == value,
        f"{name}: axis report differs from captured evidence",
    )

raw_sequence = [hex_bytes(item["report"]) for item in capture["rawSequence"]]
require(
    len(raw_sequence) >= 5 and raw_sequence[1] == raw_sequence[2],
    "fixture no longer preserves the repeated-report evidence",
)
require(
    all(len(report) == 11 for report in raw_sequence),
    "raw sequence contains an unexpected report length",
)

btstack_cmake = (ROOT / "cmake" / "jh_btstack.cmake").read_text(encoding="utf-8")
for source_set in (
    "_jh_btstack_base_sources",
    "_jh_btstack_ble_sources",
    "_jh_btstack_classic_hid_sources",
):
    require(source_set in btstack_cmake, f"BTstack source set is missing {source_set}")
for source in (
    "src/btstack_hid.c",
    "src/classic/btstack_link_key_db_memory.c",
    "src/classic/hid_host.c",
    "src/classic/sdp_client.c",
    "src/classic/sdp_util.c",
    "jh_bluetooth_gamepad_parser.c",
    "jh_bluetooth_classic_hid_memory_probe.c",
    "jh_bluetooth_classic_hid_probe_logic.c",
):
    require(source in btstack_cmake, f"Classic HID source set is missing {source}")
require(
    "src/btstack_hid_parser.c" not in btstack_cmake,
    "the platform-neutral gamepad parser still depends on BTstack HID parsing",
)
for forbidden in (
    "src/classic/rfcomm.c",
    "src/classic/sdp_server.c",
    "src/classic/hid_device.c",
    "src/classic/a2dp",
    "src/classic/avrcp",
    "src/classic/hfp",
):
    require(forbidden not in btstack_cmake, f"minimal build includes {forbidden}")
require(
    "ENABLE_CLASSIC=1" in btstack_cmake
    and "ENABLE_SDP_EXTRA_QUERIES=1" in btstack_cmake
    and "JH_BLUETOOTH_CLASSIC_HID_PROBE=1" in btstack_cmake,
    "Classic HID mode definitions are missing",
)
for wrapped_pool_function in (
    "btstack_memory_l2cap_service_get",
    "btstack_memory_l2cap_channel_get",
    "btstack_memory_btstack_link_key_db_memory_entry_get",
    "btstack_memory_hid_host_connection_get",
):
    require(
        f"--wrap={wrapped_pool_function}" in btstack_cmake,
        f"Classic HID memory probe does not wrap {wrapped_pool_function}",
    )

config = (ROOT / "src" / "hal" / "bluetooth" / "btstack_config.h").read_text(
    encoding="utf-8"
)
for expected in (
    "MAX_NR_HCI_CONNECTIONS 1",
    "MAX_NR_HID_HOST_CONNECTIONS 1",
    "MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 1",
    "MAX_NR_L2CAP_CHANNELS 3",
    "MAX_NR_L2CAP_SERVICES 2",
):
    require(expected in config, f"Classic HID pool is missing {expected}")
require(
    "Classic HID probe cannot be combined with a BLE mode" in config,
    "btstack_config.h does not reject mixed Classic/BLE modes",
)

cyw43_cmake = (ROOT / "cmake" / "jh_cyw43_driver.cmake").read_text(
    encoding="utf-8"
)
rp_cmake = (ROOT / "cmake" / "jh_rp_native_sdk.cmake").read_text(
    encoding="utf-8"
)
require(
    "jh_target_enable_btstack_classic_hid" in cyw43_cmake
    and "The private Classic HID probe cannot use a public profile" in cyw43_cmake,
    "CYW43 selector does not isolate the Classic HID mode",
)
require(
    "Bluetooth Classic HID requires a CYW43 network backend" in rp_cmake,
    "RP build does not reject Classic HID on a board without CYW43",
)

probe = (ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_hid_probe.c").read_text(
    encoding="utf-8"
)
for expected in (
    "hci_set_link_key_db(btstack_link_key_db_memory_instance())",
    "sdp_client_init();",
    "hid_host_init(s_hid_descriptor, sizeof(s_hid_descriptor));",
    "jh_bluetooth_gamepad_parser_configure(",
    "jh_bluetooth_gamepad_parser_parse_input(",
    "hid_host_register_packet_handler(packet_handler);",
    "jh_btstack_host_acquire(",
    "JH_BLUETOOTH_HOST_PROFILE_CLASSIC_HID",
    "gap_inquiry_start(",
    "sdp_client_query_uuid16(",
    "sdp_client_service_search(",
    "sdp_parser_init_service_search();",
    "BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE",
    "BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION",
    "HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT",
    "hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_BOOT)",
    "hid_host_send_set_protocol_mode(",
    "s_snapshot.descriptor_matches_capture",
    "gap_ssp_set_auto_accept(0)",
    "gap_ssp_confirmation_response(",
    "gap_pin_code_response(",
    "jh_bluetooth_classic_hid_probe_logic_disconnected(&s_logic)",
):
    require(expected in probe, f"Classic HID probe is missing {expected}")
require(
    "gap_discoverable_control(1" not in probe,
    "C5 probe must not expose unsolicited page-scan pairing",
)
require(
    "valid_zero2_report" not in probe
    and "JH_ZERO2_REPORT_ID" not in probe
    and "report[5]" not in probe,
    "C6 probe still decodes model-specific report offsets",
)
start_body = probe.split(
    "hal_status_t jh_bluetooth_classic_hid_probe_start(void)", 1
)[1].split("hal_status_t jh_bluetooth_classic_hid_probe_service(void)", 1)[0]
require(
    "start_inquiry_cycle" not in start_body and "gap_inquiry_start" not in start_body,
    "C5 probe opens discovery during startup instead of waiting for a command",
)

logic = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_hid_probe_logic.c"
).read_text(encoding="utf-8")
require(
    "JH_CLASSIC_HID_DISCOVERY_WINDOW_MS" in logic,
    "C5 discovery-window logic lost its bounded duration",
)
require(
    "s_known_address" in probe
    and "memcpy(s_candidate_address, s_known_address" in probe,
    "C5 known-device reconnect lost its stable address",
)
pairing_open_body = probe.split(
    "hal_status_t jh_bluetooth_classic_hid_probe_open_pairing_window(void)", 1
)[1].split(
    "hal_status_t jh_bluetooth_classic_hid_probe_authorize_pairing(void)", 1
)[0]
require(
    "JH_CLASSIC_HID_PHASE_KNOWN_IDLE" in pairing_open_body,
    "C5 pairing no longer permits replacing a known device",
)
identity = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_gamepad_identity.h"
).read_text(encoding="utf-8")
for expected in (
    "JH_BLUETOOTH_GAMEPAD_EXPECTED_NAME",
    "JH_BLUETOOTH_GAMEPAD_EXPECTED_VENDOR_ID",
    "JH_BLUETOOTH_GAMEPAD_EXPECTED_PRODUCT_ID",
    "JH_BLUETOOTH_GAMEPAD_EXPECTED_VERSION",
):
    require(expected in identity, f"shared gamepad identity is missing {expected}")
require(
    "const jh_bluetooth_gamepad_snapshot_t *snapshot" in logic
    and "report[" not in logic,
    "C6 probe logic must consume the normalized snapshot",
)

gamepad_parser = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_gamepad_parser.h"
).read_text(encoding="utf-8")
for expected in (
    "JH_BLUETOOTH_GAMEPAD_DESCRIPTOR_MAX = 256u",
    "JH_BLUETOOTH_GAMEPAD_REPORT_MAX = 32u",
    "JH_BLUETOOTH_GAMEPAD_QUEUE_CAPACITY = 16u",
    "jh_bluetooth_gamepad_parser_parse_input(",
    "jh_bluetooth_gamepad_parser_next(",
):
    require(expected in gamepad_parser, f"C6 parser is missing {expected}")

memory_probe = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_hid_memory_probe.c"
).read_text(encoding="utf-8")
require(
    "allocation_failures" in memory_probe and "high_water" in memory_probe,
    "C5 memory probe does not record pool failures and high-water marks",
)

verifier_path = FIXTURE_DIR / "verify_zero2.py"
verifier = verifier_path.read_text(encoding="utf-8")
for expected in (
    "DISCONNECT_TIMEOUT_S = 60.0",
    "RECONNECT_CYCLES = 5",
    "RECONNECT_SETTLE_MS = 3_000",
    "RECONNECT_TIMEOUT_S = 180.0",
    "STABILITY_DURATION_MS = 30 * 60 * 1000",
    'probe.command("DISCOVER")',
    'probe.command("AUTHORIZE")',
    'probe.command("DISCONNECT")',
    "known-pad reconnect in cycle",
    '"--resume-stability"',
    '"hostVerifierResumed"',
    "health_counter",
    '"gamepad": "unavailable"',
    'DEFAULT_RESULT_PATH = Path(__file__).with_name("zero2_pico2w_c6_result.json")',
    '"descriptorsRejected"',
    '"droppedSnapshots"',
    '"reportsRejected"',
):
    require(expected in verifier, f"C5 verifier is missing {expected}")
require(
    all(
        secret not in verifier
        for secret in ("bd_addr_to_str", '"linkKey":', '"deviceAddress":')
    ),
    "C5 verifier exposes an address or key field",
)

result_path = FIXTURE_DIR / "zero2_pico2w_c5_result.json"
if result_path.exists():
    result_text = result_path.read_text(encoding="utf-8")
    result = json.loads(result_text)
    require(result["result"] == "pass", "stored C5 result is not a pass")
    require(
        result["capture"] == capture_path.name
        and result["firmware"]["gamepad"] == "unavailable",
        "stored C5 result is detached from the characterized fixture",
    )
    require(
        re.search(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", result_text) is None,
        "stored C5 result contains a Bluetooth device address",
    )

c6_result_path = FIXTURE_DIR / "zero2_pico2w_c6_result.json"
if c6_result_path.exists():
    c6_result_text = c6_result_path.read_text(encoding="utf-8")
    c6_result = json.loads(c6_result_text)
    require(c6_result["result"] == "pass", "stored C6 result is not a pass")
    require(
        c6_result["capture"] == capture_path.name
        and c6_result["firmware"]["gamepad"] == "unavailable",
        "stored C6 result is detached from the characterized fixture",
    )
    c6_parser = c6_result["parser"]
    require(
        c6_parser["descriptorLimit"] == 256
        and c6_parser["reportLimit"] == 32
        and c6_parser["queueCapacity"] == 16,
        "stored C6 result does not use the frozen parser limits",
    )
    require(
        c6_parser["descriptorsAccepted"] == 1
        and c6_parser["reportsAccepted"] > 0
        and c6_parser["descriptorsRejected"] == 0
        and c6_parser["reportsRejected"] == 0
        and c6_parser["droppedSnapshots"] == 0,
        "stored C6 result reports a parser failure",
    )
    require(
        re.search(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", c6_result_text)
        is None,
        "stored C6 result contains a Bluetooth device address",
    )

manifest = json.loads(
    (FIXTURE_DIR / ".vscode" / "jaszczurhal.project.json").read_text(
        encoding="utf-8"
    )
)
require(
    manifest["target"] == "rp2350-arm"
    and manifest["board"] == "pico2w"
    and manifest["example"]["boards"]["rp2350-arm"] == "pico2w",
    "Classic HID fixture no longer defaults to Pico 2 W",
)
variant = manifest["example"]["variants"][0]
require(
    variant["extraDefines"] == ["JH_BLUETOOTH_CLASSIC_HID_PROBE"],
    "fixture must use only the private Classic HID selector",
)

public_hal = (ROOT / "src" / "hal" / "hal.h").read_text(encoding="utf-8")
require(
    "jh_bluetooth_classic_hid" not in public_hal,
    "private Classic HID probe leaked into the public HAL umbrella",
)
