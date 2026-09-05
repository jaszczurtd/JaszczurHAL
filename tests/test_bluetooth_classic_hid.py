#!/usr/bin/env python3
"""Validate the private Classic HID Host build and sanitized Zero 2 capture."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys

from source_assertions import source_has_fragment, source_section


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
btstack_hci = (ROOT / "third_party" / "BTstack" / "src" / "hci.c").read_text(
    encoding="utf-8"
)
require(
    source_has_fragment(btstack_hci, "incoming_classic_collision")
    and source_has_fragment(btstack_hci, "RECEIVED_CONNECTION_REQUEST")
    and source_has_fragment(btstack_hci, "ACCEPTED_CONNECTION_REQUEST"),
    "pinned BTstack fork lacks the Classic connection collision fix",
)
require(
    source_has_fragment(btstack_cmake, '"${_jh_btstack_root}/src/hci.c"')
    and not source_has_fragment(btstack_cmake, "_jh_btstack_patch")
    and not source_has_fragment(btstack_cmake, "git apply"),
    "BTstack must be compiled directly from the pinned fork without local patches",
)
for source_set in (
    "_jh_btstack_base_sources",
    "_jh_btstack_ble_sources",
    "_jh_btstack_classic_sources",
    "_jh_btstack_hid_host_sources",
    "_jh_btstack_hid_device_sources",
    "_jh_btstack_a2dp_sink_sources",
    "_jh_btstack_avrcp_target_sources",
):
    require(
        source_has_fragment(btstack_cmake, source_set),
        f"BTstack source set is missing {source_set}",
    )
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
    require(
        source_has_fragment(btstack_cmake, source),
        f"Classic HID source set is missing {source}",
    )
require(
    source_has_fragment(btstack_cmake, "src/btstack_hid_parser.c")
    and source_has_fragment(btstack_cmake, "src/classic/hid_device.c")
    and source_has_fragment(btstack_cmake, "src/classic/sdp_server.c")
    and source_has_fragment(
        btstack_cmake, "elseif(JH_BTSTACK_CLASSIC_HID_DEVICE_FIXTURE)"
    ),
    "the private non-gamepad HID Device fixture sources are incomplete",
)
require(
    source_has_fragment(btstack_cmake, "if(JH_BTSTACK_BLE)")
    and source_has_fragment(btstack_cmake, "if(JH_BTSTACK_CLASSIC)")
    and source_has_fragment(btstack_cmake, "if(JH_BTSTACK_HID_HOST)")
    and not source_has_fragment(btstack_cmake, "PUBLIC_CLASSIC_BLE")
    and not source_has_fragment(btstack_cmake, "PUBLIC_HID_HOST_BLE"),
    "public BTstack profiles are not composed from independent feature flags",
)
minimal_classic_sources = source_section(
    btstack_cmake,
    "set(_jh_btstack_classic_sources",
    "set(_jh_btstack_hid_host_sources",
)
minimal_hid_host_sources = source_section(
    btstack_cmake,
    "set(_jh_btstack_hid_host_sources",
    "set(_jh_btstack_hid_device_sources",
)
minimal_classic_hid_sources = minimal_classic_sources + minimal_hid_host_sources
for forbidden in (
    "src/classic/rfcomm.c",
    "src/classic/a2dp",
    "src/classic/avrcp",
    "src/classic/hfp",
):
    require(
        not source_has_fragment(minimal_classic_hid_sources, forbidden),
        f"minimal build includes {forbidden}",
    )

minimal_avrcp_target_sources = source_section(
    btstack_cmake,
    "set(_jh_btstack_avrcp_target_sources",
    "set(_jh_jh_base_sources",
)
require(
    source_has_fragment(minimal_avrcp_target_sources, "src/classic/avrcp.c")
    and source_has_fragment(
        minimal_avrcp_target_sources, "src/classic/avrcp_target.c"
    )
    and not source_has_fragment(
        minimal_avrcp_target_sources, "src/classic/avrcp_controller.c"
    ),
    "minimal AVRCP Target build includes an unused Controller role",
)

public_backend = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_btstack_backend.c"
).read_text(encoding="utf-8")
require(
    "Bluetooth Classic HCI connection request class=0x%06lx " in public_backend
    and "link=0x%02x known=%u" in public_backend
    and "connection complete status=0x%02x " in public_backend
    and "handle=0x%04x link=0x%02x encrypted=%u known=%u" in public_backend,
    "public Classic HCI diagnostics are missing sanitized connection context",
)
require(
    "addr=%" not in public_backend
    and not source_has_fragment(public_backend, "bd_addr_to_str"),
    "public Classic HCI diagnostics expose a full Bluetooth address",
)
require(
    source_has_fragment(public_backend, "bool scan_stop_pending;")
    and source_has_fragment(public_backend, "s_backend.scan_stop_pending = true;")
    and source_has_fragment(
        public_backend,
        "if (s_backend.scan_stop_pending) { complete_scan_stop();",
    ),
    "Classic inquiry stop does not wait for BTstack completion before reuse",
)
require(
    source_has_fragment(btstack_cmake, "ENABLE_CLASSIC = 1")
    and source_has_fragment(btstack_cmake, "ENABLE_SDP_EXTRA_QUERIES = 1")
    and source_has_fragment(btstack_cmake, "JH_BLUETOOTH_CLASSIC_HID_PROBE = 1"),
    "Classic HID mode definitions are missing",
)
for wrapped_pool_function in (
    "btstack_memory_l2cap_service_get",
    "btstack_memory_l2cap_channel_get",
    "btstack_memory_btstack_link_key_db_memory_entry_get",
    "btstack_memory_hid_host_connection_get",
):
    require(
        source_has_fragment(btstack_cmake, f"--wrap={wrapped_pool_function}"),
        f"Classic HID memory probe does not wrap {wrapped_pool_function}",
    )

require(
    source_has_fragment(btstack_cmake, "jh_bluetooth_a2dp_memory_probe.c"),
    "A2DP build is missing BTstack pool high-water instrumentation",
)
for wrapped_pool_function in (
    "btstack_memory_hci_connection_get",
    "btstack_memory_l2cap_service_get",
    "btstack_memory_l2cap_channel_get",
    "btstack_memory_btstack_link_key_db_memory_entry_get",
    "btstack_memory_service_record_item_get",
    "btstack_memory_avdtp_stream_endpoint_get",
    "btstack_memory_avdtp_connection_get",
    "btstack_memory_avrcp_connection_get",
):
    require(
        source_has_fragment(btstack_cmake, f"--wrap={wrapped_pool_function}"),
        f"A2DP memory probe does not wrap {wrapped_pool_function}",
    )

config = (ROOT / "src" / "hal" / "bluetooth" / "btstack_config.h").read_text(
    encoding="utf-8"
)
require(
    source_has_fragment(config, '#include "hal/core/hal_project_config_hook.h"'),
    "public BTstack pools do not use the configured Classic peer capacity",
)
for expected in (
    "MAX_NR_HCI_CONNECTIONS 1",
    "MAX_NR_HID_HOST_CONNECTIONS 1",
    "MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 1",
    "MAX_NR_L2CAP_CHANNELS 3",
    "MAX_NR_L2CAP_SERVICES 2",
):
    require(
        source_has_fragment(config, expected),
        f"Classic HID pool is missing {expected}",
    )
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
stm32_static_cmake = (ROOT / "stm32_lib" / "CMakeLists.txt").read_text(
    encoding="utf-8"
)
require(
    source_has_fragment(cyw43_cmake, "jh_target_enable_btstack_classic_hid")
    and "The private Classic HID probe cannot use a public profile" in cyw43_cmake,
    "CYW43 selector does not isolate the Classic HID mode",
)
require(
    "JH_CYW43_FEATURE_UNPARSED_ARGUMENTS" in cyw43_cmake
    and "Unknown CYW43 feature-stack arguments" in cyw43_cmake,
    "CYW43 feature selection does not reject unknown arguments",
)
require(
    source_has_fragment(
        stm32_static_cmake,
        'CLASSIC "${_jh_stm32_has_bluetooth_classic}" '
        'HID_HOST "${_jh_stm32_has_bluetooth_hid_host}"',
    )
    and not source_has_fragment(stm32_static_cmake, "GAMEPAD"),
    "STM32 static library does not pass resolved Classic and HID Host features",
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
    "hid_host_connect(s_candidate_address, HID_PROTOCOL_MODE_REPORT, &hid_cid)",
    "hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_BOOT)",
    "hid_host_send_set_protocol_mode(",
    "s_snapshot.descriptor_matches_capture",
    "gap_ssp_set_auto_accept(0)",
    "gap_ssp_confirmation_response(",
    "gap_pin_code_response(",
    "jh_bluetooth_classic_hid_probe_logic_disconnected(&s_logic)",
):
    require(
        source_has_fragment(probe, expected),
        f"Classic HID probe is missing {expected}",
    )
require(
    not source_has_fragment(probe, "gap_discoverable_control(1"),
    "C5 probe must not expose unsolicited page-scan pairing",
)
require(
    not source_has_fragment(probe, "valid_zero2_report")
    and not source_has_fragment(probe, "JH_ZERO2_REPORT_ID")
    and not source_has_fragment(probe, "report[5]"),
    "C6 probe still decodes model-specific report offsets",
)
start_body = source_section(
    probe,
    "hal_status_t jh_bluetooth_classic_hid_probe_start(",
    "hal_status_t jh_bluetooth_classic_hid_probe_service(void)",
)
require(
    not source_has_fragment(start_body, "start_inquiry_cycle")
    and not source_has_fragment(start_body, "gap_inquiry_start"),
    "C5 probe opens discovery during startup instead of waiting for a command",
)

logic = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_hid_probe_logic.c"
).read_text(encoding="utf-8")
classic_facade = (
    ROOT / "src" / "hal" / "bluetooth" / "hal_bluetooth_classic.cpp"
).read_text(encoding="utf-8")
require(
    source_has_fragment(
        classic_facade,
        "return hal_text_format_mac_ex(address->bytes, out, out_size)",
    ),
    "Classic address formatting does not use the shared text helper",
)
for sensitive_copy in (
    "jh_secure_zeroize(&blob, sizeof(blob))",
    "jh_secure_zeroize(&identity, sizeof(identity))",
    "jh_secure_zeroize(&s_classic.pending_key, sizeof(s_classic.pending_key))",
    "jh_secure_zeroize(&slot->identity, sizeof(slot->identity))",
):
    require(
        source_has_fragment(classic_facade, sensitive_copy),
        f"Classic bonding does not erase sensitive copy: {sensitive_copy}",
    )
require(
    source_has_fragment(logic, "JH_CLASSIC_HID_DISCOVERY_WINDOW_MS"),
    "C5 discovery-window logic lost its bounded duration",
)
require(
    source_has_fragment(probe, "s_known_address")
    and source_has_fragment(probe, "memcpy(s_candidate_address, s_known_address"),
    "C5 known-device reconnect lost its stable address",
)
pairing_open_body = source_section(
    probe,
    "hal_status_t jh_bluetooth_classic_hid_probe_open_pairing_window(void)",
    "hal_status_t jh_bluetooth_classic_hid_probe_authorize_pairing(void)",
)
require(
    source_has_fragment(pairing_open_body, "JH_CLASSIC_HID_PHASE_KNOWN_IDLE"),
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
    require(
        source_has_fragment(identity, expected),
        f"shared gamepad identity is missing {expected}",
    )
require(
    source_has_fragment(logic, "const jh_bluetooth_gamepad_snapshot_t *snapshot")
    and not source_has_fragment(logic, "report["),
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
    require(
        source_has_fragment(gamepad_parser, expected),
        f"C6 parser is missing {expected}",
    )

memory_probe = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_hid_memory_probe.c"
).read_text(encoding="utf-8")
require(
    source_has_fragment(memory_probe, "allocation_failures")
    and source_has_fragment(memory_probe, "high_water")
    and source_has_fragment(memory_probe, "hci_connections"),
    "C5 memory probe does not record pool failures and high-water marks",
)
require(
    source_has_fragment(
        btstack_cmake, "if(JH_BTSTACK_HID_HOST AND NOT JH_BTSTACK_A2DP_SINK)"
    )
    and source_has_fragment(
        btstack_cmake, '"-Wl,--wrap=btstack_memory_hci_connection_get"'
    ),
    "public HID builds do not enable the C10 pool high-water instrumentation",
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
    '"--resume-stability"',
    '"hostVerifierResumed"',
    "health_counter",
    '"gamepad": "unavailable"',
    'DEFAULT_RESULT_PATH = Path(__file__).with_name("zero2_pico2w_c6_result.json")',
    '"descriptorsRejected"',
    '"droppedSnapshots"',
    '"reportsRejected"',
):
    require(
        source_has_fragment(verifier, expected),
        f"C5 verifier is missing {expected}",
    )
require(
    "known-pad reconnect in cycle" in verifier,
    "C5 verifier is missing known-pad reconnect in cycle",
)
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
    not source_has_fragment(public_hal, "jh_bluetooth_classic_hid"),
    "private Classic HID probe leaked into the public HAL umbrella",
)

speaker_example = (
    ROOT / "examples" / "30_bluetooth_speaker" / "app.c"
).read_text(encoding="utf-8")
gamepad_example = (
    ROOT / "examples" / "29_bluetooth_gamepad" / "app.c"
).read_text(encoding="utf-8")
require(
    source_has_fragment(gamepad_example, "if (!info->pairing_pending)")
    and source_has_fragment(gamepad_example, "else if (!s_pairingReplySent)")
    and source_has_fragment(gamepad_example, "s_pairingReplySent = true"),
    "Bluetooth gamepad example must authorize each pairing request only once",
)
require(
    source_has_fragment(gamepad_example, "RECONNECT_RETRY_MS = 250u")
    and source_has_fragment(
        gamepad_example,
        "hal_elapsed_u32(now, s_reconnectAttemptMs, RECONNECT_RETRY_MS)",
    )
    and source_has_fragment(gamepad_example, "status != HAL_EBUSY")
    and source_has_fragment(gamepad_example, "status != HAL_EAGAIN"),
    "Bluetooth gamepad example must rate-limit transient reconnect retries",
)
require(
    source_has_fragment(
        gamepad_example,
        """
        if (info->state != HAL_GAMEPAD_STATE_READY) {
          return;
        }
        s_reconnectStarted = false;
        """,
    ),
    "Bluetooth gamepad example must retry after a failed outgoing connection",
)
require(
    source_has_fragment(
        speaker_example,
        "SPEAKER_CLASS_OF_DEVICE = 0x240414u",
    )
    and source_has_fragment(
        speaker_example,
        "identity.class_of_device = SPEAKER_CLASS_OF_DEVICE",
    ),
    "Bluetooth speaker must advertise the Audio/Rendering Loudspeaker class",
)
require(
    source_has_fragment(
        speaker_example,
        "a2dp.decoded_frames != s_pairing_decoded_frames",
    )
    and not source_has_fragment(
        speaker_example,
        "if (info.peer_count != 0u && info.pairing_window_open) {",
    ),
    "manual replacement pairing must remain open until valid audio",
)
