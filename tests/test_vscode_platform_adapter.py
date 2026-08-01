#!/usr/bin/env python3
"""Unit checks for the shared runtime platform boundary."""

from __future__ import annotations

from contextlib import contextmanager, redirect_stderr
import io
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from typing import Any, Callable, Iterator
from unittest.mock import patch


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT))

from vscode.runtime import exit_codes
from vscode.runtime import jh_vscode as runtime
from vscode.runtime.platform_api import (
    UnsupportedPlatformAdapter,
    get_platform_adapter,
    set_platform_adapter,
)
from vscode.linux.runtime import jh_vscode as linux_compatibility


assert linux_compatibility.SUPPORTED_ACTIONS == runtime.SUPPORTED_ACTIONS
assert runtime.EXIT_MONITOR == exit_codes.EXIT_MONITOR
assert runtime.EXIT_UNSUPPORTED == exit_codes.EXIT_UNSUPPORTED


# Regular packages keep `vscode` from resolving as a namespace package merged
# with a same-named directory from another sys.path entry.
for package in (
    "vscode",
    "vscode/linux",
    "vscode/linux/runtime",
    "vscode/runtime",
    "vscode/runtime/monitor",
):
    assert (ROOT / package / "__init__.py").is_file(), f"missing package marker in {package}"


import_probe = """
import builtins
import sys

real_import = builtins.__import__

def guarded_import(name, *args, **kwargs):
    if name == "fcntl":
        raise ImportError("fcntl intentionally unavailable")
    return real_import(name, *args, **kwargs)

builtins.__import__ = guarded_import
import vscode.runtime.jh_vscode
assert "vscode.linux.runtime.adapter" not in sys.modules
"""
subprocess.run(
    [sys.executable, "-c", import_probe],
    cwd=ROOT,
    check=True,
)


# The monitor core stays importable without pyserial and reports the missing
# dependency through the monitor exit code instead of exiting during import.
monitor_probe = """
import sys

class MissingPySerial:
    def find_spec(self, name, path=None, target=None):
        if name == "serial" or name.startswith("serial."):
            raise ImportError("pyserial intentionally unavailable")
        return None

sys.meta_path.insert(0, MissingPySerial())
from vscode.runtime.monitor import core
from vscode.runtime.exit_codes import EXIT_MONITOR

assert core.serial is None
assert core.list_ports is None
assert core.run() == EXIT_MONITOR
"""
monitor_result = subprocess.run(
    [sys.executable, "-c", monitor_probe],
    cwd=ROOT,
    check=False,
    capture_output=True,
    text=True,
)
assert monitor_result.returncode == 0, monitor_result.stderr
assert "pyserial is not installed" in monitor_result.stderr


# Unsupported host operations surface as the documented exit code, never as a
# traceback.
set_platform_adapter(UnsupportedPlatformAdapter())
try:
    unsupported_error = io.StringIO()
    with redirect_stderr(unsupported_error):
        assert runtime.main(["list-ports"]) == runtime.EXIT_UNSUPPORTED
    assert "serial_candidate_paths" in unsupported_error.getvalue()
finally:
    set_platform_adapter(None)


class FakePlatformAdapter:
    def __init__(self, root: Path):
        self.root = root
        self.mixed_port = Path("C:\\JH builds/moduł testowy/COM17")
        self.lock_paths: list[Path] = []
        self.copy_calls: list[tuple[Path, Path]] = []

    def serial_candidate_paths(self) -> list[Path]:
        return [self.mixed_port]

    def serial_port_exists(self, port: str) -> bool:
        return port in {str(self.mixed_port), "COM17"}

    def resolve_serial_port(self, port: str) -> str:
        return "COM17" if port else port

    def serial_by_id_links(self, port: str) -> list[Path]:
        return [Path("usb-Jaszczur_Moduł-if00")] if port else []

    def serial_identity_text(self, port: Path) -> str:
        return f"Jaszczur Moduł {port.name}"

    def verified_identity_ports(
        self,
        expected_tokens: list[str],
        normalize: Callable[[str], str],
    ) -> list[tuple[Path, Path | None]]:
        identity = normalize("Jaszczur Moduł")
        if any(token in identity for token in expected_tokens):
            return [(self.mixed_port, Path("usb-Jaszczur_Moduł-if00"))]
        return []

    def serial_fallback_candidates(self) -> list[str]:
        return [str(self.mixed_port)]

    def is_serial_candidate(self, port: str) -> bool:
        return port.upper().startswith("COM")

    def process_cmdline(self, pid: int) -> str:
        return f"python monitor --pid {pid}"

    def port_owner_pids(self, port: str) -> list[int]:
        return [17] if port else []

    def request_monitor_release(self, pid: int) -> None:
        del pid

    def terminate_process(self, pid: int) -> None:
        del pid

    def configure_serial_fd(self, fd: int) -> None:
        del fd

    def install_monitor_signal_handlers(
        self,
        interrupt_handler: Callable[[int, Any], None],
        release_handler: Callable[[int, Any], None],
    ) -> None:
        del interrupt_handler, release_handler

    def find_bootsel_mounts(self, labels: set[str]) -> list[Path]:
        assert "RPI-RP2" in labels
        return [self.root / "BOOT SEL modułu"]

    def find_bootsel_blocks(self, labels: set[str]) -> list[dict[str, Any]]:
        assert "RPI-RP2" in labels
        return [{"path": "volume://moduł", "mountpoints": [None]}]

    def mount_bootsel_block(
        self,
        block: dict[str, Any],
        labels: set[str],
        mountpoint: Callable[[dict[str, Any]], Path | None],
    ) -> Path | None:
        assert block["path"] == "volume://moduł"
        assert "RPI-RP2" in labels
        assert mountpoint(block) is None
        return self.root / "BOOT SEL modułu"

    def durable_copy(self, source: Path, destination: Path) -> None:
        self.copy_calls.append((source, destination))

    @contextmanager
    def build_lock(self, lock_path: Path) -> Iterator[None]:
        self.lock_paths.append(lock_path)
        yield

    def temporary_directory(self) -> Path:
        return self.root / "temp dir modułu"

    def persistent_monitor_path(self) -> Path:
        return self.root / "runtime dir" / "serial_persistent.py"


with TemporaryDirectory(prefix="jh platform modułu ") as temp_dir:
    root = Path(temp_dir)
    fake = FakePlatformAdapter(root)
    set_platform_adapter(fake)
    try:
        assert get_platform_adapter() is fake
        assert runtime.serial_candidate_paths() == [fake.mixed_port]
        assert runtime.upload_port_path_exists(str(fake.mixed_port))
        assert runtime.resolve_upload_port_for_tool(str(fake.mixed_port)) == "COM17"
        assert runtime.by_id_links_for_port("COM17")[0].name == "usb-Jaszczur_Moduł-if00"
        assert "Moduł" in runtime.tty_usb_identity_text(fake.mixed_port)
        assert runtime.process_cmdline(42).endswith("42")
        assert runtime.port_owner_pids("COM17") == [17]

        config = {
            "identity": {
                "enabled": True,
                "usbManufacturer": "Jaszczur",
                "usbProduct": "Moduł",
            }
        }
        assert runtime.verified_identity_ports(config) == [
            (fake.mixed_port, Path("usb-Jaszczur_Moduł-if00"))
        ]

        mounts = runtime.find_bootsel_mounts()
        blocks = runtime.find_bootsel_blocks()
        assert mounts == [root / "BOOT SEL modułu"]
        assert runtime.bootsel_mountpoint(blocks[0]) is None
        assert runtime.mount_bootsel_block(blocks[0]) == mounts[0]

        build_dir = root / "build dir modułu"
        with runtime.build_lock({"buildDir": str(build_dir)}, root):
            pass
        assert fake.lock_paths == [build_dir / ".jh-build.lock"]
        assert runtime.temporary_directory().name == "temp dir modułu"

        uf2 = root / "firmware modułu.uf2"
        with patch.object(
            runtime,
            "find_single_bootsel_mount",
            return_value=(mounts[0], [str(mounts[0])]),
        ), patch.object(runtime, "print_memory_map_overview"), patch.object(
            runtime.time,
            "sleep",
        ), patch(
            "builtins.print",
        ):
            assert runtime.upload_uf2_artifact(uf2, {}, root) == 0
        assert fake.copy_calls == [(uf2, mounts[0] / uf2.name)]
    finally:
        set_platform_adapter(None)
