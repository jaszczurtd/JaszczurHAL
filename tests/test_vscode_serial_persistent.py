#!/usr/bin/env python3
"""Unit checks for persistent-monitor USB identity following."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from types import ModuleType, SimpleNamespace
from unittest.mock import patch


ROOT = Path(sys.argv[1]).resolve()
RUNTIME = ROOT / "vscode" / "linux" / "runtime" / "serial_persistent.py"

serial_module = ModuleType("serial")
serial_module.Serial = object
serial_module.SerialException = Exception
serial_tools_module = ModuleType("serial.tools")
serial_list_ports_module = ModuleType("serial.tools.list_ports")
serial_list_ports_module.comports = lambda: []
serial_tools_module.list_ports = serial_list_ports_module
serial_module.tools = serial_tools_module

with patch.dict(
    sys.modules,
    {
        "serial": serial_module,
        "serial.tools": serial_tools_module,
        "serial.tools.list_ports": serial_list_ports_module,
    },
):
    spec = importlib.util.spec_from_file_location(
        "jh_vscode_serial_persistent_test",
        RUNTIME,
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {RUNTIME}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)


def pico_port(device: str, product: str = "Router Reset - JaszczurHAL CDC"):
    return SimpleNamespace(
        device=device,
        vid=0x2E8A,
        pid=0x000A,
        description=product,
        manufacturer="Jaszczur",
        product=product,
        interface="JaszczurHAL CDC",
        hwid=f"USB VID:PID=2E8A:000A LOCATION={device}",
    )


old_port = "/dev/serial/by-id/usb-Jaszczur_Router_Reset-old-if00"
replacement = pico_port("/dev/ttyACM0")
tokens = ["routerreset", "jaszczurrouterreset"]

with patch.object(module.os.path, "exists", return_value=False), patch.object(
    module,
    "list_serial_ports",
    return_value=[replacement],
):
    port, reason = module.find_port(
        "pico",
        old_port,
        tokens,
        follow_identity=True,
    )

assert port == replacement.device
assert reason.startswith("verified-identity:")

with patch.object(module.os.path, "exists", return_value=False), patch.object(
    module,
    "list_serial_ports",
    return_value=[replacement],
):
    port, reason = module.find_port(
        "pico",
        old_port,
        tokens,
        follow_identity=False,
    )

assert port is None
assert reason == f"preferred-missing:{old_port}"

second_match = pico_port("/dev/ttyACM1")
with patch.object(module.os.path, "exists", return_value=False), patch.object(
    module,
    "list_serial_ports",
    return_value=[replacement, second_match],
):
    port, reason = module.find_port(
        "pico",
        old_port,
        tokens,
        follow_identity=True,
    )

assert port is None
assert reason == "identity-ambiguous"

unrelated = pico_port("/dev/ttyACM2", "Different Device")
with patch.object(module.os.path, "exists", return_value=False), patch.object(
    module,
    "list_serial_ports",
    return_value=[unrelated],
):
    port, reason = module.find_port(
        "pico",
        old_port,
        tokens,
        follow_identity=True,
    )

assert port is None
assert reason == f"preferred-missing:{old_port}"

with patch.object(
    module.os.path,
    "exists",
    side_effect=lambda path: path == old_port,
), patch.object(
    module,
    "list_serial_ports",
) as preferred_scan:
    port, reason = module.find_port(
        "pico",
        old_port,
        tokens,
        follow_identity=True,
    )

assert port == old_port
assert reason == f"preferred:{old_port}"
preferred_scan.assert_not_called()
