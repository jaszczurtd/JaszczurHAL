#!/usr/bin/env python3
"""Unit checks for persistent-monitor USB identity following."""

from __future__ import annotations

from pathlib import Path
import sys
from types import ModuleType, SimpleNamespace
from unittest.mock import patch


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT))

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
    from vscode.runtime.monitor import core as module
    from vscode.linux.runtime import serial_persistent as compatibility
    from vscode.runtime.platform_api import SerialPortRecord
    from vscode.runtime.serial_identity import SerialIdentityExpectation


assert compatibility.find_port is module.find_port


ordered_ports = [
    SerialPortRecord(device="COM2", platform="windows"),
    SerialPortRecord(device="COM10", platform="windows"),
]
order_adapter = SimpleNamespace(
    list_serial_ports=lambda: ordered_ports,
    is_serial_candidate=lambda device: device.startswith("COM"),
)
with patch.object(module, "get_platform_adapter", return_value=order_adapter):
    assert [port.device for port in module.list_serial_ports()] == ["COM2", "COM10"]


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

with patch.object(module, "serial_port_exists", return_value=False), patch.object(
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

with patch.object(module, "serial_port_exists", return_value=False), patch.object(
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
with patch.object(module, "serial_port_exists", return_value=False), patch.object(
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

# With no configured port, following an identity must not open the first Pico.
for ports, wanted, wanted_reason in (
    ([unrelated, replacement], replacement.device, "verified-identity:"),
    ([unrelated], None, "identity-not-found"),
    ([replacement, second_match], None, "identity-ambiguous"),
    ([], None, "identity-not-found"),
):
    with patch.object(module, "list_serial_ports", return_value=ports):
        port, reason = module.find_port("pico", "", tokens, follow_identity=True)
    assert port == wanted, (ports, port)
    assert reason.startswith(wanted_reason), reason

# Project identities also select USB-UART bridges and native non-RP CDC devices.
for vid, pid in ((0x10C4, 0xEA60), (0x0483, 0x5740), (0x303A, 0x1001)):
    device = SerialPortRecord(device="COM12", vid=vid, pid=pid)
    expected_device = SerialIdentityExpectation(vid=vid, pid=pid)
    with patch.object(module, "list_serial_ports", return_value=[unrelated, device]):
        port, reason = module.find_port("pico", follow_identity=True,
                                        expected_identity=expected_device)
    assert port == device.device, (vid, pid, reason)

with patch.object(module, "serial_port_exists", return_value=False), patch.object(
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
    module,
    "serial_port_exists",
    side_effect=lambda path: path == old_port,
), patch.object(
    module,
    "list_serial_ports",
) as preferred_scan, patch.object(
    module, "get_platform_adapter",
    return_value=SimpleNamespace(serial_port_record=lambda port: replacement),
):
    port, reason = module.find_port(
        "pico",
        old_port,
        tokens,
        follow_identity=True,
    )

assert port == old_port
assert reason == f"preferred:{old_port}"
preferred_scan.assert_not_called()

# A tty name can survive while its device identity changes after reconnection.
for ports, wanted in (([unrelated, replacement], replacement.device),
                      ([unrelated], None)):
    with patch.object(module, "serial_port_exists", return_value=True), patch.object(
        module, "get_platform_adapter",
        return_value=SimpleNamespace(serial_port_record=lambda port: unrelated),
    ), patch.object(module, "list_serial_ports", return_value=ports):
        port, reason = module.find_port(
            "pico", unrelated.device, tokens, follow_identity=True,
        )
    assert port == wanted, (ports, port)

# Explicitly pinned monitoring keeps its requested port without following USB.
with patch.object(module, "serial_port_exists", return_value=True), patch.object(
    module, "list_serial_ports",
) as pinned_scan:
    port, reason = module.find_port("pico", unrelated.device, tokens,
                                    follow_identity=False)
assert port == unrelated.device
pinned_scan.assert_not_called()

ecu = SerialPortRecord(device="COM10", vid=0x2E8A, pid=0x0009,
                       manufacturer="Jaszczur", product="Fiesta ECU")
adjustometer = SerialPortRecord(device="COM2", vid=0x2E8A, pid=0x000A,
                               manufacturer="Jaszczur", product="Fiesta Adjustometer")
expected = SerialIdentityExpectation(vid=0x2E8A, pid=0x0009,
                                     manufacturer="Jaszczur", product="Fiesta ECU")
for ports, wanted in (([adjustometer, ecu], ecu.device),
                      ([adjustometer], None)):
    with patch.object(module, "list_serial_ports", return_value=ports):
        port, reason = module.find_port("pico", follow_identity=True,
                                        expected_identity=expected)
    assert port == wanted, (ports, port)
