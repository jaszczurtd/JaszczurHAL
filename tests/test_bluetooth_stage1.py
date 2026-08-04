#!/usr/bin/env python3
"""Validate the private Bluetooth Stage 1 dependency and build boundary."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(sys.argv[1]).resolve()
BTSTACK_REF = "501e6d2b86e6c92bfb9c390bcf55709938e25ac1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


pin = (ROOT / "third_party/btstack_version.conf").read_text(encoding="utf-8")
require(
    f"BTSTACK_REF={BTSTACK_REF}" in pin,
    "BTstack does not match the revision selected by Pico SDK 2.2.0",
)

pico_sdk = ROOT / "third_party/pico-sdk"
if pico_sdk.is_dir():
    gitlink = subprocess.run(
        ["git", "ls-tree", "HEAD", "lib/btstack"],
        cwd=pico_sdk,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    require(
        re.search(rf"\b{BTSTACK_REF}\tlib/btstack$", gitlink) is not None,
        "managed BTstack pin differs from the pinned Pico SDK gitlink",
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
    and "ENABLE_BLE=1" in btstack_cmake,
    "Stage 1 does not own a BLE-only BTstack source manifest",
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
        and "jh_target_enable_btstack_stage1" in text,
        f"{recipe} recipe does not conditionally integrate the private probe",
    )

config = (
    ROOT / "src/hal/impl/shared/bluetooth/btstack_config.h"
).read_text(encoding="utf-8")
for expected in (
    "ENABLE_LE_PERIPHERAL",
    "MAX_NR_HCI_CONNECTIONS 1",
    "MAX_NR_CONTROLLER_ACL_BUFFERS 3",
    "ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL",
):
    require(expected in config, f"bounded BTstack config is missing {expected}")
for forbidden in ("ENABLE_CLASSIC", "ENABLE_LE_CENTRAL", "ENABLE_MESH"):
    require(forbidden not in config, f"Stage 1 unexpectedly enables {forbidden}")

shared_bus_dir = ROOT / "src/hal/impl/shared/drivers/cyw43-driver/vendor/src"
shared_bus = "\n".join(
    (shared_bus_dir / name).read_text(encoding="utf-8")
    for name in (
        "cybt_shared_bus.c.upstream",
        "cybt_shared_bus_driver.c.upstream",
    )
)
for forbidden in ("assert(", "panic(", "cyw43_malloc", "cyw43_free"):
    require(forbidden not in shared_bus, f"shared bus retains fatal/dynamic path {forbidden}")

transport = (
    ROOT
    / "src/hal/impl/shared/bluetooth/jh_btstack_hci_transport_cyw43.c"
).read_text(encoding="utf-8")
require(
    "length != 0u && length < JH_CYW43_PACKET_HEADER_SIZE" in transport,
    "HCI transport does not reject truncated CYW43 packet headers",
)
require(
    "JH_BTSTACK_CYW43_MAX_HCI_PROCESS_LOOP_COUNT 8u" in transport,
    "HCI receive drain is no longer bounded",
)

probe = (
    ROOT / "src/hal/impl/shared/bluetooth/jh_bluetooth_stage1_probe.c"
).read_text(encoding="utf-8")
require(
    "hci_subevent_le_connection_complete_get_status(packet)" in probe,
    "Stage 1 reports failed LE connection events as successful connections",
)

manifest = json.loads(
    (
        ROOT
        / "tests/hardware/bluetooth_stage1/.vscode/jaszczurhal.project.json"
    ).read_text(encoding="utf-8")
)
require(
    manifest["example"]["targets"] == ["stm32g474", "rp2040"],
    "hardware probe does not cover both Stage 1 targets",
)
require(
    manifest["example"]["boards"]
    == {"stm32g474": "nucleo-g474re-pim730", "rp2040": "picow"},
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
