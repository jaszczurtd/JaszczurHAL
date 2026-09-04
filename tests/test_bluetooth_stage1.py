#!/usr/bin/env python3
"""Validate the private Bluetooth controller and Stage 1 probe boundary."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(sys.argv[1]).resolve()
BTSTACK_REPO = "https://github.com/jaszczurtd/btstack.git"
BTSTACK_REF = "0cfae0eb5aa61650924168b368c5ea93b4b363e4"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


pin = (ROOT / "third_party/btstack_version.conf").read_text(encoding="utf-8")
require(
    f"BTSTACK_REPO={BTSTACK_REPO}" in pin
    and f"BTSTACK_REF={BTSTACK_REF}" in pin,
    "BTstack does not match the reviewed JaszczurHAL fork revision",
)

btstack = ROOT / "third_party/BTstack"
if btstack.is_dir():
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=btstack,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    require(
        head == BTSTACK_REF,
        "managed BTstack checkout differs from the reviewed fork revision",
    )

btstack_cmake = (ROOT / "cmake/jh_btstack.cmake").read_text(encoding="utf-8")
cyw43_cmake = (ROOT / "cmake/jh_cyw43_driver.cmake").read_text(encoding="utf-8")
rp_cmake = (ROOT / "cmake/jh_rp_native_sdk.cmake").read_text(encoding="utf-8")
stm32_cmake = (ROOT / "cmake/targets/stm32g474.cmake").read_text(
    encoding="utf-8"
)
for forbidden in ("pico_cyw43_arch", "pico_btstack_cyw43"):
    require(forbidden not in btstack_cmake, f"Stage 1 links forbidden {forbidden}")
require(
    "jh_target_enable_btstack_stage1" in btstack_cmake
    and "ENABLE_BLE=1" in btstack_cmake
    and "jh_bluetooth_host_runtime.c" in btstack_cmake
    and "jh_btstack_host.c" in btstack_cmake
    and "jh_bluetooth_hci_transport.c" in btstack_cmake
    and "jh_btstack_run_loop.c" in btstack_cmake,
    "Bluetooth integration does not own the JH transport and run loop",
)
require(
    "BLUETOOTH" in cyw43_cmake
    and "cybt_shared_bus.c.upstream" in cyw43_cmake
    and "cybt_shared_bus_driver.c.upstream" in cyw43_cmake,
    "CYW43 shared-bus sources are not gated by Bluetooth",
)
for recipe, text in (("RP", rp_cmake), ("STM32", stm32_cmake)):
    require(
        "JH_BLUETOOTH_STAGE1_PROBE" in text
        and "jh_target_enable_cyw43_feature_stack" in text,
        f"{recipe} recipe does not conditionally integrate the private probe",
    )
require(
    "jh_target_enable_btstack_stage1" in cyw43_cmake,
    "the shared CYW43 feature helper does not select the Stage 1 BTstack mode",
)

config = (
    ROOT / "src/hal/bluetooth/btstack_config.h"
).read_text(encoding="utf-8")
for expected in (
    "ENABLE_LE_PERIPHERAL",
    "MAX_NR_HCI_CONNECTIONS 1",
    "MAX_NR_CONTROLLER_ACL_BUFFERS 3",
    "ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL",
):
    require(expected in config, f"bounded BTstack config is missing {expected}")
for forbidden in ("ENABLE_LE_CENTRAL", "ENABLE_MESH"):
    require(forbidden not in config, f"Stage 1 unexpectedly enables {forbidden}")
require(
    "#if defined(JH_BLUETOOTH_CLASSIC_HID_PROBE)" in config
    and "#else\n/* BLE Peripheral sizing validated by the hardware gates. */"
    in config,
    "Classic sizing is not isolated from the Stage 1 BLE configuration",
)

shared_bus_dir = ROOT / "src/hal/network/cyw43/vendor/src"
shared_bus = "\n".join(
    (shared_bus_dir / name).read_text(encoding="utf-8")
    for name in (
        "cybt_shared_bus.c.upstream",
        "cybt_shared_bus_driver.c.upstream",
    )
)
for forbidden in ("assert(", "panic(", "cyw43_malloc", "cyw43_free"):
    require(forbidden not in shared_bus, f"shared bus retains fatal/dynamic path {forbidden}")

transport_adapter = (
    ROOT
    / "src/hal/bluetooth/jh_btstack_hci_transport_cyw43.c"
).read_text(encoding="utf-8")
transport = (
    ROOT / "src/hal/bluetooth/jh_bluetooth_hci_transport.c"
).read_text(encoding="utf-8")
transport_header = (
    ROOT / "src/hal/bluetooth/jh_bluetooth_hci_transport.h"
).read_text(encoding="utf-8")
require(
    "length != 0u && length < JH_BLUETOOTH_HCI_FRAME_HEADER_SIZE" in transport,
    "HCI transport does not reject truncated CYW43 packet headers",
)
require(
    "JH_BLUETOOTH_HCI_SERVICE_BUDGET 8u" in transport_header,
    "HCI receive drain is no longer bounded",
)
for diagnostic in (
    "rx_event_packets",
    "rx_acl_packets",
    "tx_command_packets",
    "tx_acl_packets",
    "JH_HCI_OPCODE_HOST_BUFFER_SIZE",
    "JH_HCI_OPCODE_SET_CONTROLLER_TO_HOST_FLOW_CONTROL",
):
    require(
        diagnostic in transport,
        f"shared HCI transport diagnostics are missing {diagnostic}",
    )
require(
    "cyw43_bluetooth_hci_" not in transport,
    "host-testable HCI core directly depends on CYW43",
)
require(
    "jh_bluetooth_controller_backend()" in transport_adapter
    and "jh_btstack_run_loop_notify()" in transport_adapter
    and "btstack_run_loop_poll_data_sources_from_irq" not in transport_adapter,
    "BTstack adapter bypasses the target controller or JH run loop",
)

controller = (
    ROOT / "src/hal/bluetooth/jh_bluetooth_controller_cyw43.c"
).read_text(encoding="utf-8")
for operation in (
    "cyw43_bluetooth_hci_init",
    "cyw43_bluetooth_hci_read",
    "cyw43_bluetooth_hci_write",
    "jh_cyw43_port_get_mac",
):
    require(operation in controller, f"CYW43 controller omits {operation}")

for backend in (
    ROOT
    / "src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_bluetooth_controller.cpp",
    ROOT
    / "src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_bluetooth_controller.cpp",
):
    backend_text = backend.read_text(encoding="utf-8")
    require(
        "jh_bluetooth_controller_backend" in backend_text
        and "jh_bluetooth_controller_cyw43_instance" in backend_text,
        f"{backend.name} does not bind the private Bluetooth controller boundary",
    )

run_loop = (
    ROOT / "src/hal/bluetooth/jh_btstack_run_loop.c"
).read_text(encoding="utf-8")
require(
    "jh_btstack_run_loop_service_once" in run_loop
    and "btstack_run_loop_embedded_execute_once" in run_loop,
    "JH run loop lacks an explicit service-once boundary",
)

probe = (
    ROOT / "src/hal/bluetooth/jh_bluetooth_stage1_probe.c"
).read_text(encoding="utf-8")
require(
    "hci_subevent_le_connection_complete_get_status(packet)" in probe,
    "Stage 1 reports failed LE connection events as successful connections",
)
require(
    "hci_event_disconnection_complete_get_reason(packet)" in probe,
    "Stage 1 does not preserve the HCI disconnect reason",
)
require(
    probe.index("sm_init();") < probe.index("att_server_init("),
    "Stage 1 must initialize the Security Manager before the ATT server",
)
require(
    "att_server_register_packet_handler(packet_handler);" not in probe,
    "Stage 1 must not receive connection events through the deprecated ATT forwarder",
)
require(
    "jh_btstack_host_acquire(" in probe and "jh_btstack_host_service(" in probe,
    "Stage 1 probe bypasses the shared Bluetooth host runtime",
)
require(
    "btstack_run_loop_embedded_execute_once" not in probe,
    "Stage 1 probe bypasses the JH-owned run loop",
)

host_runtime = (
    ROOT / "src/hal/bluetooth/jh_bluetooth_host_runtime.c"
).read_text(encoding="utf-8")
for expected in (
    "JH_BLUETOOTH_HOST_TRANSITION_ADD_PROFILE",
    "JH_BLUETOOTH_HOST_TRANSITION_STOP",
    "next_generation",
    "controller_invalidated",
):
    require(expected in host_runtime, f"shared host runtime is missing {expected}")

host_runtime_test = (
    ROOT / "tests/test_bluetooth_host_runtime.cpp"
).read_text(encoding="utf-8")
for expected in (
    "test_profiles_and_duplicate_references_share_one_host",
    "test_profile_and_power_failures_roll_back_in_reverse_order",
    "test_failed_second_profile_does_not_reset_the_running_host",
    "test_invalidation_makes_handles_stale_and_allows_clean_restart",
):
    require(expected in host_runtime_test, f"host lifecycle coverage is missing {expected}")

radio_facade = (
    ROOT
    / "src/hal/network/cyw43/jh_cyw43_radio.cpp"
).read_text(encoding="utf-8")
for transition in (
    "jh_board_runtime_set_available(kCyw43Capabilities)",
    "jh_board_runtime_set_failed(kCyw43Capabilities)",
    "jh_board_runtime_set_inactive(kCyw43Capabilities)",
):
    require(
        transition in radio_facade,
        f"shared CYW43 lifecycle is missing board transition {transition}",
    )

for backend in (
    ROOT / "src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_platform.cpp",
    ROOT
    / "src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_platform.cpp",
):
    require(
        "jh_cyw43_radio_backend_runtime" in backend.read_text(encoding="utf-8"),
        f"{backend.name} does not expose the shared CYW43 owner",
    )

lwip_service = (
    ROOT
    / "src/hal/network/cyw43/jh_cyw43_lwip.cpp"
).read_text(encoding="utf-8")
require(
    lwip_service.index("jh_cyw43_driver_service(&host_wake)")
    < lwip_service.index("jh_cyw43_radio_service_clients()")
    < lwip_service.index("sys_check_timeouts();"),
    "shared CYW43 service order must be driver, client stacks, then lwIP timers",
)

host_test = (ROOT / "tests/test_bluetooth_hci_transport.cpp").read_text(
    encoding="utf-8"
)
for expected in (
    "test_receive_drain_is_bounded_and_resumes_next_service",
    "test_malformed_and_failed_reads_propagate_hal_status",
    "test_packet_handler_cannot_reenter_transport_service",
):
    require(expected in host_test, f"fake HCI coverage is missing {expected}")

manifest = json.loads(
    (
        ROOT
        / "tests/hardware/bluetooth_stage1/.vscode/jaszczurhal.project.json"
    ).read_text(encoding="utf-8")
)
require(
    manifest["example"]["targets"]
    == ["stm32g474", "rp2350-arm", "rp2040"],
    "hardware probe does not cover all Stage 1 targets",
)
require(
    manifest["example"]["boards"]
    == {
        "stm32g474": "nucleo-g474re-pim730",
        "rp2350-arm": "pico2w",
        "rp2040": "picow",
    },
    "hardware probe selects unexpected boards",
)
variants = {item["id"]: item for item in manifest["example"]["variants"]}
require(set(variants) == {"bluetooth", "wifi-only"}, "memory baseline is incomplete")
require(
    variants["bluetooth"].get("extraDefines")
    == ["JH_BLUETOOTH_STAGE1_PROBE"],
    "Bluetooth sources are not explicitly feature-gated",
)
require(
    "JH_BLUETOOTH_STAGE1_PROBE"
    not in variants["wifi-only"].get("extraDefines", []),
    "WiFi-only baseline enables the Bluetooth probe",
)

public_hal = (ROOT / "src/hal/hal.h").read_text(encoding="utf-8")
require(
    "jh_bluetooth_stage1_probe" not in public_hal,
    "private Stage 1 probe leaked into the public HAL umbrella",
)
