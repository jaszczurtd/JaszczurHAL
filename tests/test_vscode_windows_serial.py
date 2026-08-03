#!/usr/bin/env python3
"""Unit checks for Windows COM identity and monitor ownership safety."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT))

from vscode.runtime import jh_vscode as runtime
from vscode.runtime.monitor import core as monitor
from vscode.runtime.monitor.ownership import (
    MONITOR_OWNERSHIP_VERSION,
    RELEASE_UPLOAD,
    MonitorOwnershipConflict,
    load_monitor_ownership,
    monitor_release_action,
    register_monitor_ownership,
    request_monitor_release,
    unregister_monitor_ownership,
)
from vscode.runtime.platform_api import set_platform_adapter
from vscode.runtime.serial_identity import (
    IDENTITY_MISMATCH,
    IDENTITY_MISSING_METADATA,
    SerialIdentityExpectation,
    match_serial_identity,
)
from vscode.windows.runtime.adapter import (
    WindowsPlatformAdapter,
    normalize_com_port,
)


def port_info(
    device: str,
    *,
    serial_number: str | None,
    manufacturer: str | None = "Jaszczur",
    product: str | None = "Fiesta ECU - JaszczurHAL CDC",
    interface: str | None = "JaszczurHAL CDC",
    location: str | None = "1-2.3",
):
    return SimpleNamespace(
        device=device,
        vid=0x2E8A,
        pid=0x000A,
        serial_number=serial_number,
        manufacturer=manufacturer,
        product=product,
        interface=interface,
        location=location,
        hwid=(
            f"USB VID:PID=2E8A:000A SER={serial_number or ''} "
            f"LOCATION={location or ''}"
        ),
        description=product or "USB Serial Device",
    )


infos = [
    port_info("COM3", serial_number="FIESTA-A"),
    port_info("com11", serial_number="FIESTA-B"),
    port_info(
        r"\\.\COM20",
        serial_number=None,
        manufacturer=None,
        product=None,
        interface=None,
        location=None,
    ),
]
adapter = WindowsPlatformAdapter(lambda: infos, lambda: [])

assert normalize_com_port("com11") == "COM11"
assert normalize_com_port(r"\\.\COM20") == "COM20"
assert normalize_com_port("//./COM31") == "COM31"
assert adapter.is_serial_candidate("COM10")
assert not adapter.is_serial_candidate("COM0")
assert [record.device for record in adapter.list_serial_ports()] == [
    "COM3",
    "COM11",
    "COM20",
]

if sys.platform == "win32":
    assert adapter.process_start_identity(os.getpid()).startswith("windows:")
    assert "python" in adapter.process_cmdline(os.getpid()).lower()
    with TemporaryDirectory(prefix="jh windows lock ") as lock_directory:
        lock_path = Path(lock_directory) / "build.lock"
        with adapter.build_lock(lock_path):
            assert lock_path.is_file()
        crash_probe = """
import os
from pathlib import Path
import sys
sys.path.insert(0, sys.argv[1])
from vscode.windows.runtime.adapter import PLATFORM
with PLATFORM.build_lock(Path(sys.argv[2])):
    os._exit(0)
"""
        subprocess.run(
            [sys.executable, "-c", crash_probe, str(ROOT), str(lock_path)],
            check=True,
        )
        with adapter.build_lock(lock_path):
            pass
        native_project = Path(lock_directory) / "native ownership"
        native_project.mkdir()
        native_owner = register_monitor_ownership(
            adapter,
            native_project,
            "COM27",
        )
        assert load_monitor_ownership(adapter, native_project, "com27") == native_owner
        request_monitor_release(adapter, native_owner, RELEASE_UPLOAD)
        assert monitor_release_action(native_owner) == RELEASE_UPLOAD
        unregister_monitor_ownership(native_owner)

first = adapter.serial_port_record("com3")
assert first is not None
assert first.vid == 0x2E8A
assert first.pid == 0x000A
assert first.serial_number == "FIESTA-A"
assert first.manufacturer == "Jaszczur"
assert first.product.startswith("Fiesta ECU")
assert first.interface == "JaszczurHAL CDC"
assert first.location == "1-2.3"
assert "SER=FIESTA-A" in first.hwid

project_identity = {
    "usbManufacturer": "Jaszczur",
    "usbProduct": "Fiesta ECU",
    "usbVid": "0x2e8a",
    "usbPid": "000a",
    "byIdHint": "Jaszczur_Fiesta_ECU",
}
expectation = SerialIdentityExpectation.from_config(project_identity)
assert match_serial_identity(first, expectation).verified
try:
    SerialIdentityExpectation.from_config({"usbVid": "not-a-usb-id"})
except ValueError as exc:
    assert "usbVid" in str(exc)
else:
    raise AssertionError("invalid USB identifier was silently ignored")
second = adapter.serial_port_record("COM11")
assert second is not None
assert match_serial_identity(second, expectation).verified

serial_expectation = SerialIdentityExpectation.from_config(
    {**project_identity, "usbSerialNumber": "FIESTA-A"}
)
assert match_serial_identity(first, serial_expectation).verified
second_match = match_serial_identity(second, serial_expectation)
assert second_match.status == IDENTITY_MISMATCH
assert second_match.mismatched_fields == ("serialNumber",)

incomplete = adapter.serial_port_record("COM20")
assert incomplete is not None
incomplete_match = match_serial_identity(incomplete, serial_expectation)
assert incomplete_match.status == IDENTITY_MISSING_METADATA
assert {"manufacturer", "product", "serialNumber"}.issubset(
    incomplete_match.missing_fields
)

pnp_info = port_info(
    "COM4",
    serial_number="PICO-USB-SERIAL",
    manufacturer="Microsoft",
    product=None,
    interface=None,
)
pnp_adapter = WindowsPlatformAdapter(
    lambda: [pnp_info],
    lambda: [],
    lambda _port_info: "Fiesta Adjustometer",
)
pnp_record = pnp_adapter.serial_port_record("COM4")
assert pnp_record is not None
assert pnp_record.manufacturer == ""
assert pnp_record.product == "Fiesta Adjustometer"
assert pnp_record.platform_identity == "Fiesta Adjustometer"
pnp_expectation = SerialIdentityExpectation.from_config(
    {
        "usbManufacturer": "Jaszczur",
        "usbProduct": "Fiesta Adjustometer",
        "usbVid": "0x2e8a",
        "usbPid": "0x000a",
    }
)
pnp_match = match_serial_identity(pnp_record, pnp_expectation)
assert pnp_match.verified
assert pnp_match.missing_fields == ("manufacturer",)
assert not match_serial_identity(
    replace(pnp_record, product="Fiesta Adjustometer Clone"),
    pnp_expectation,
).verified
weak_pnp_match = match_serial_identity(
    pnp_record,
    SerialIdentityExpectation.from_config(
        {
            "usbManufacturer": "Jaszczur",
            "usbProduct": "Fiesta Adjustometer",
        }
    ),
)
assert weak_pnp_match.status == IDENTITY_MISSING_METADATA

config = {
    "identity": {
        "enabled": True,
        **project_identity,
        "usbSerialNumber": "FIESTA-A",
    }
}
ambiguous_config = {"identity": {"enabled": True, **project_identity}}
set_platform_adapter(adapter)
try:
    assert runtime.verify_upload_port(config, "com3") == 0
    with redirect_stderr(io.StringIO()):
        assert runtime.verify_upload_port(config, "COM11") == runtime.EXIT_UNSAFE_DEVICE
        assert runtime.verify_upload_port(config, "COM99") == runtime.EXIT_UNSAFE_DEVICE
    assert runtime.verify_upload_port(
        config,
        "COM11",
        allow_unverified=True,
    ) == 0

    selected, status = runtime.select_verified_identity_port(config)
    assert (selected, status) == ("COM3", 0)
    with redirect_stderr(io.StringIO()):
        selected, status = runtime.select_verified_identity_port(ambiguous_config)
    assert selected is None
    assert status == runtime.EXIT_UNSAFE_DEVICE
    missing_config = {
        "identity": {
            "enabled": True,
            **project_identity,
            "usbSerialNumber": "NOT-CONNECTED",
        }
    }
    assert runtime.select_verified_identity_port(missing_config) == (None, 0)

    records = runtime.serial_port_records(config)
    assert records[0]["serialNumber"] == "FIESTA-A"
    assert records[0]["verifiedForProject"]
    assert records[1]["identityStatus"] == IDENTITY_MISMATCH

    requirements = io.StringIO()
    with redirect_stderr(requirements):
        runtime.print_identity_upload_requirements(config)
    assert "COM metadata" in requirements.getvalue()
    assert "/dev/serial" not in requirements.getvalue()

    list_output = io.StringIO()
    with redirect_stdout(list_output):
        assert (
            runtime.command_list_ports(
                SimpleNamespace(
                    project=None,
                    target=None,
                    board=None,
                    port=None,
                    json=True,
                )
            )
            == 0
        )
    listed = json.loads(list_output.getvalue())
    assert listed["bootselSupported"] is True
    assert listed["bootsel"] == []
    assert listed["bootselRecords"] == []
    assert [record["port"] for record in listed["serial"]] == [
        "COM3",
        "COM11",
        "COM20",
    ]
finally:
    set_platform_adapter(None)


with TemporaryDirectory(prefix="jh local port ") as temporary:
    project = Path(temporary)
    vscode_dir = project / ".vscode"
    vscode_dir.mkdir()
    (vscode_dir / "settings.json").write_text(
        json.dumps({"jaszczurhal.uploadPort": "COM3"}),
        encoding="utf-8",
    )
    (vscode_dir / "jaszczurhal.local.json").write_text(
        json.dumps({"uploadPort": "COM11"}),
        encoding="utf-8",
    )
    assert monitor.get_preferred_port("", project) == "COM11"
    assert monitor.get_preferred_port("COM20", project) == "COM20"


class OwnershipAdapter(WindowsPlatformAdapter):
    def __init__(self, temporary_root: Path):
        super().__init__(lambda: [])
        self.temporary_root = temporary_root
        self.processes: dict[int, str] = {}
        self.owners: list[int] = []
        self.release_requests: list[int] = []
        self.terminated: list[int] = []
        self.reuse_on_release: tuple[int, str] | None = None

    def temporary_directory(self) -> Path:
        return self.temporary_root

    def process_start_identity(self, pid: int) -> str:
        return self.processes.get(pid, "")

    def process_cmdline(self, pid: int) -> str:
        return f"managed monitor {pid}" if pid in self.processes else "foreign"

    def port_owner_pids(self, port: str) -> list[int]:
        del port
        return list(self.owners)

    def request_monitor_release(self, pid: int) -> None:
        self.release_requests.append(pid)
        if self.reuse_on_release is not None and self.reuse_on_release[0] == pid:
            self.processes[pid] = self.reuse_on_release[1]

    def terminate_process(self, pid: int) -> None:
        self.terminated.append(pid)
        self.processes.pop(pid, None)
        self.owners = [owner for owner in self.owners if owner != pid]


with TemporaryDirectory(prefix="jh ownership ") as temporary:
    temporary_root = Path(temporary)
    project = temporary_root / "project with spaces"
    project.mkdir()
    ownership_adapter = OwnershipAdapter(temporary_root)

    ownership_adapter.processes[101] = "start-101"
    owner = register_monitor_ownership(
        ownership_adapter,
        project,
        "COM17",
        pid=101,
    )
    payload = json.loads(owner.marker_path.read_text(encoding="utf-8"))
    assert payload["version"] == MONITOR_OWNERSHIP_VERSION
    assert payload["port"] == "COM17"
    assert payload["pid"] == 101
    assert payload["processStart"] == "start-101"
    assert load_monitor_ownership(ownership_adapter, project, "com17") == owner
    ownership_adapter.processes[104] = "parallel-start"
    try:
        register_monitor_ownership(ownership_adapter, project, "COM17", pid=104)
    except MonitorOwnershipConflict:
        pass
    else:
        raise AssertionError("two monitors registered the same port")
    request_monitor_release(ownership_adapter, owner, RELEASE_UPLOAD)
    assert monitor_release_action(owner) == RELEASE_UPLOAD
    unregister_monitor_ownership(owner)
    assert not owner.marker_path.exists()
    assert not owner.release_path.exists()

    ownership_adapter.processes[102] = "start-before-reuse"
    reused = register_monitor_ownership(
        ownership_adapter,
        project,
        "COM18",
        pid=102,
    )
    ownership_adapter.processes[102] = "start-after-reuse"
    assert load_monitor_ownership(ownership_adapter, project, "COM18") is None
    assert not reused.marker_path.exists()

    ownership_adapter.processes[103] = "start-crash"
    crashed = register_monitor_ownership(
        ownership_adapter,
        project,
        "COM19",
        pid=103,
    )
    ownership_adapter.processes.pop(103)
    assert load_monitor_ownership(ownership_adapter, project, "COM19") is None
    assert not crashed.marker_path.exists()

    set_platform_adapter(ownership_adapter)
    try:
        ownership_adapter.processes[os.getpid()] = "live-monitor-start"

        ownership_adapter.processes[304] = "busy-monitor-start"
        busy_owner = register_monitor_ownership(
            ownership_adapter,
            project,
            "COM25",
            pid=304,
        )
        ownership_adapter.owners = []
        busy_lines = monitor.format_port_owners("COM25", project)
        assert busy_lines == [
            "  PID 304: managed monitor 304 [JaszczurHAL monitor marker]"
        ]
        assert monitor.is_lock_error(
            OSError(13, "could not open port COM25: PermissionError(13, 'Access is denied.', None, 5)")
        )
        unregister_monitor_ownership(busy_owner)
        assert "PID unavailable" in monitor.format_port_owners("COM25", project)[0]

        class CooperativeSerial:
            def __init__(self):
                self.closed = False

            def readline(self):
                live_owner = load_monitor_ownership(
                    ownership_adapter,
                    project,
                    "COM20",
                )
                assert live_owner is not None
                request_monitor_release(
                    ownership_adapter,
                    live_owner,
                    RELEASE_UPLOAD,
                )
                return b""

            def close(self):
                self.closed = True

        cooperative_serial = CooperativeSerial()
        with patch.object(
            monitor,
            "open_serial",
            return_value=cooperative_serial,
        ):
            assert (
                monitor.monitor("COM20", 115200, "wait", project)
                == "released-for-upload"
            )
        assert cooperative_serial.closed
        assert load_monitor_ownership(
            ownership_adapter,
            project,
            "COM20",
        ) is None

        ownership_adapter.processes[202] = "foreign-start"
        ownership_adapter.owners = [202]
        foreign_error = io.StringIO()
        with redirect_stderr(foreign_error):
            assert (
                runtime.release_port_for_upload("COM21", project)
                == runtime.EXIT_UNSAFE_DEVICE
            )
        assert "not owned by this project monitor" in foreign_error.getvalue()
        assert ownership_adapter.terminated == []

        ownership_adapter.processes[301] = "cooperative-start"
        ownership_adapter.owners = [301]
        register_monitor_ownership(
            ownership_adapter,
            project,
            "COM22",
            pid=301,
        )
        real_request = request_monitor_release

        def cooperative_request(adapter_value, ownership_value, action):
            real_request(adapter_value, ownership_value, action)
            unregister_monitor_ownership(ownership_value)
            ownership_adapter.owners = []

        with patch.object(runtime, "request_monitor_release", cooperative_request):
            assert runtime.release_port_for_upload("COM22", project) == 0
        assert ownership_adapter.terminated == []
        runtime.end_upload_release(project)

        ownership_adapter.processes[302] = "timeout-start"
        ownership_adapter.owners = [302]
        register_monitor_ownership(
            ownership_adapter,
            project,
            "COM23",
            pid=302,
        )
        with patch.object(
            runtime.time,
            "monotonic",
            side_effect=[0.0, 4.0, 4.0, 4.5],
        ), patch.object(runtime.time, "sleep"):
            assert runtime.release_port_for_upload("COM23", project) == 0
        assert ownership_adapter.terminated == [302]
        runtime.end_upload_release(project)

        ownership_adapter.processes[303] = "reuse-before"
        ownership_adapter.owners = [303]
        register_monitor_ownership(
            ownership_adapter,
            project,
            "COM24",
            pid=303,
        )
        ownership_adapter.reuse_on_release = (303, "reuse-after")
        reuse_error = io.StringIO()
        with patch.object(
            runtime.time,
            "monotonic",
            side_effect=[0.0, 4.0],
        ), redirect_stderr(reuse_error):
            assert (
                runtime.release_port_for_upload("COM24", project)
                == runtime.EXIT_UNSAFE_DEVICE
            )
        assert 303 not in ownership_adapter.terminated
        runtime.end_upload_release(project)
    finally:
        set_platform_adapter(None)
