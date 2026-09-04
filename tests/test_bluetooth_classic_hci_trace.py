#!/usr/bin/env python3
"""Validate the private, privacy-preserving Classic HCI trace fixture."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys

from source_assertions import source_has_fragment


ROOT = Path(sys.argv[1]).resolve()
FIXTURE = ROOT / "tests" / "hardware" / "bluetooth_classic_hci_trace"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


manifest = json.loads(
    (FIXTURE / ".vscode" / "jaszczurhal.project.json").read_text(
        encoding="utf-8"
    )
)
require(
    manifest["example"]["targets"] == ["rp2040", "rp2350-arm"]
    and manifest["example"]["boards"]
    == {"rp2040": "picow", "rp2350-arm": "pico2w"},
    "HCI trace fixture must compare Pico W and Pico 2 W",
)
require(
    manifest["cmake"]["cache"]["JH_EXTRA_DEFINES"]
    == "HAL_ENABLE_BLUETOOTH_CLASSIC=1",
    "HCI trace fixture must use only the public Classic profile",
)

backend = (
    ROOT / "src" / "hal" / "bluetooth" / "jh_bluetooth_classic_btstack_backend.c"
).read_text(encoding="utf-8")
require(
    source_has_fragment(backend, "hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);"),
    "Classic backend no longer requests inquiry RSSI and EIR data",
)

trace = (FIXTURE / "app.c").read_text(encoding="utf-8")
for expected in (
    "hci_dump_init(&s_hciDump);",
    "hci_dump_enable_packet_log(true);",
    'strcmp(s_command, "SCAN30")',
    "jh_btstack_cyw43_transport_snapshot(&transport);",
    "jh_rp2040_cyw43_gspi_get_clock(&clock);",
    "inquiryAddressByte(record, index)",
):
    require(
        source_has_fragment(trace, expected),
        f"HCI trace fixture is missing {expected}",
    )
require(
    re.search(
        r"case TRACE_HCI_EVENT_EXTENDED_INQUIRY_RESULT:\s*"
        r"/\*.*?\*/\s*return .*?17u;",
        trace,
        re.DOTALL,
    )
    is not None,
    "Extended Inquiry Result must redact its EIR body after RSSI",
)
require(
    not source_has_fragment(trace.lower(), "case 0xfc01u"),
    "vendor Write BD_ADDR command must not expose its payload",
)
require(
    re.search(r"default:\s*return 3u;", trace) is not None,
    "unknown HCI commands must expose only the opcode header",
)

parity = (ROOT / "scripts" / "build_rp_native_parity_fixtures.sh").read_text(
    encoding="utf-8"
)
for expected in (
    '"bluetooth_classic_hci_trace"',
    '"rp2040 picow"',
    '"rp2350-arm pico2w"',
):
    require(expected in parity, f"RP parity build is missing {expected}")
