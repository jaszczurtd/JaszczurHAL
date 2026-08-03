"""Linux host adapter for the JaszczurHAL runtime."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import replace
import fcntl
import glob
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
from typing import Any, Callable, Iterator

from vscode.runtime.platform_api import SerialPortRecord


def _read_optional_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return ""


def _iter_lsblk_devices(devices: list[dict[str, Any]]) -> list[dict[str, Any]]:
    flat: list[dict[str, Any]] = []
    for device in devices:
        flat.append(device)
        children = device.get("children")
        if isinstance(children, list):
            flat.extend(_iter_lsblk_devices(children))
    return flat


def _serial_identity_metadata(port: Path) -> dict[str, str]:
    try:
        resolved = port.resolve(strict=True)
    except OSError:
        resolved = port
    sys_tty = Path("/sys/class/tty") / resolved.name / "device"
    try:
        device = sys_tty.resolve(strict=True)
    except OSError:
        return {}

    metadata: dict[str, str] = {}
    current = device
    sys_root = Path("/sys")
    for _ in range(8):
        for name in (
            "manufacturer",
            "product",
            "interface",
            "serial",
            "idVendor",
            "idProduct",
        ):
            value = _read_optional_text(current / name)
            if value and name not in metadata:
                metadata[name] = value
                if name == "idVendor" and "location" not in metadata:
                    metadata["location"] = current.name
        if current == sys_root or current.parent == current:
            break
        current = current.parent
    return metadata


def _usb_id(metadata: dict[str, str], key: str) -> int | None:
    try:
        return int(metadata.get(key, ""), 16)
    except ValueError:
        return None


class LinuxPlatformAdapter:
    """Linux implementations of serial, process, volume, and lock operations."""

    @property
    def platform_name(self) -> str:
        return "linux"

    def list_serial_ports(self) -> list[SerialPortRecord]:
        port_info_by_device: dict[str, Any] = {}
        try:
            from serial.tools import list_ports

            for port_info in list_ports.comports():
                port_info_by_device[str(port_info.device)] = port_info
        except ImportError:
            pass

        records: list[SerialPortRecord] = []
        for path in self.serial_candidate_paths():
            device = str(path)
            aliases = tuple(link.name for link in self._serial_by_id_links(device))
            metadata = _serial_identity_metadata(path)
            platform_identity = " ".join(metadata.values())
            port_info = port_info_by_device.get(device)
            if port_info is None:
                records.append(
                    SerialPortRecord(
                        device=device,
                        vid=_usb_id(metadata, "idVendor"),
                        pid=_usb_id(metadata, "idProduct"),
                        serial_number=metadata.get("serial", ""),
                        manufacturer=metadata.get("manufacturer", ""),
                        product=metadata.get("product", ""),
                        interface=metadata.get("interface", ""),
                        location=metadata.get("location", ""),
                        aliases=aliases,
                        platform_identity=platform_identity,
                        platform="linux",
                    )
                )
            else:
                record = SerialPortRecord.from_port_info(
                    port_info,
                    device=device,
                    aliases=aliases,
                    platform_identity=platform_identity,
                    platform="linux",
                )
                records.append(
                    replace(
                        record,
                        manufacturer=record.manufacturer
                        or metadata.get("manufacturer", ""),
                        product=record.product or metadata.get("product", ""),
                        interface=record.interface or metadata.get("interface", ""),
                        serial_number=record.serial_number
                        or metadata.get("serial", ""),
                        location=record.location or metadata.get("location", ""),
                        vid=record.vid if record.vid is not None else _usb_id(metadata, "idVendor"),
                        pid=record.pid if record.pid is not None else _usb_id(metadata, "idProduct"),
                    )
                )
        return records

    def serial_port_record(self, port: str) -> SerialPortRecord | None:
        resolved = self.resolve_serial_port(port)
        for record in self.list_serial_ports():
            if self.resolve_serial_port(record.device) == resolved:
                return record
            if port in record.aliases:
                return record
        return None

    def serial_candidate_paths(self) -> list[Path]:
        candidates: set[Path] = set()
        for pattern in ("ttyACM*", "ttyUSB*"):
            for path in sorted(Path("/dev").glob(pattern)):
                if path.exists():
                    candidates.add(path)

        by_id_dir = Path("/dev/serial/by-id")
        if by_id_dir.is_dir():
            for link in by_id_dir.iterdir():
                try:
                    resolved = link.resolve(strict=True)
                except OSError:
                    continue
                if resolved.name.startswith(("ttyACM", "ttyUSB")):
                    candidates.add(resolved)
        return sorted(candidates, key=lambda path: path.name)

    def serial_port_exists(self, port: str) -> bool:
        return bool(port) and Path(port).expanduser().exists()

    def resolve_serial_port(self, port: str) -> str:
        if not port:
            return port
        try:
            resolved = Path(port).resolve(strict=True)
        except OSError:
            return port
        if resolved.parent == Path("/dev") and resolved.name.startswith("tty"):
            return str(resolved)
        return port

    def _serial_by_id_links(self, port: str) -> list[Path]:
        try:
            resolved_port = Path(port).resolve()
        except OSError:
            return []

        by_id_dir = Path("/dev/serial/by-id")
        if not by_id_dir.is_dir():
            return []

        matches = []
        for link in sorted(by_id_dir.iterdir()):
            try:
                if link.resolve() == resolved_port:
                    matches.append(link)
            except OSError:
                continue
        return matches

    def serial_fallback_candidates(self) -> list[str]:
        return sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))

    def is_serial_candidate(self, port: str) -> bool:
        return port.startswith("/dev/ttyACM") or port.startswith("/dev/ttyUSB")

    def process_cmdline(self, pid: int) -> str:
        try:
            raw = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").strip()
            return raw.decode("utf-8", errors="replace")
        except Exception:
            return ""

    def process_start_identity(self, pid: int) -> str:
        try:
            stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
            close_paren = stat.rfind(")")
            fields = stat[close_paren + 2 :].split()
            start_ticks = fields[19]
            boot_id = _read_optional_text(Path("/proc/sys/kernel/random/boot_id"))
            return f"linux:{boot_id}:{start_ticks}"
        except (OSError, IndexError, ValueError):
            return ""

    def port_owner_pids(self, port: str) -> list[int]:
        pids: set[int] = set()
        for command in (["fuser", port], ["lsof", "-t", "--", port]):
            try:
                result = subprocess.run(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    text=True,
                    check=False,
                )
            except Exception:
                continue
            for item in result.stdout.split():
                try:
                    pids.add(int(item))
                except ValueError:
                    pass
        return sorted(pids)

    def request_monitor_release(self, pid: int) -> None:
        release_signal = signal.SIGUSR1 if hasattr(signal, "SIGUSR1") else signal.SIGTERM
        os.kill(pid, release_signal)

    def terminate_process(self, pid: int) -> None:
        os.kill(pid, signal.SIGTERM)

    def configure_serial_fd(self, fd: int) -> None:
        try:
            buf = bytearray(60)
            fcntl.ioctl(fd, 0x5401, buf)
            cflag = struct.unpack_from("I", buf, 8)[0] & ~0x0400
            struct.pack_into("I", buf, 8, cflag)
            fcntl.ioctl(fd, 0x5402, buf)
        except Exception:
            pass

    def install_monitor_signal_handlers(
        self,
        interrupt_handler: Callable[[int, Any], None],
        release_handler: Callable[[int, Any], None],
    ) -> None:
        signal.signal(signal.SIGINT, interrupt_handler)
        if hasattr(signal, "SIGUSR1"):
            signal.signal(signal.SIGUSR1, release_handler)

    def find_bootsel_mounts(self, labels: set[str]) -> list[Path]:
        user = os.environ.get("USER", "")
        roots = []
        if user:
            roots.extend([Path("/media") / user, Path("/run/media") / user])
        mounts: list[Path] = []
        for root in roots:
            if not root.is_dir():
                continue
            for child in root.iterdir():
                if child.is_dir() and child.name in labels:
                    mounts.append(child)
        return sorted(mounts)

    def find_bootsel_blocks(self, labels: set[str]) -> list[dict[str, Any]]:
        command = ["lsblk", "--json", "-o", "PATH,LABEL,FSTYPE,MOUNTPOINTS"]
        try:
            result = subprocess.run(command, check=False, capture_output=True, text=True)
        except OSError:
            return []
        if result.returncode != 0:
            return []
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            return []

        matches: list[dict[str, Any]] = []
        for device in _iter_lsblk_devices(payload.get("blockdevices") or []):
            if device.get("label") not in labels:
                continue
            if device.get("fstype") not in {None, "vfat", "fat", "msdos"}:
                continue
            if device.get("path"):
                matches.append(device)
        return matches

    def mount_bootsel_block(
        self,
        block: dict[str, Any],
        labels: set[str],
        mountpoint: Callable[[dict[str, Any]], Path | None],
    ) -> Path | None:
        existing = mountpoint(block)
        if existing:
            return existing

        device = str(block.get("path") or "")
        if not device or shutil.which("udisksctl") is None:
            return None
        result = subprocess.run(
            ["udisksctl", "mount", "-b", device],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            message = (result.stderr or result.stdout).strip()
            if message:
                print(f"warning: could not mount {device}: {message}", file=sys.stderr)
            return None

        for candidate in self.find_bootsel_blocks(labels):
            if candidate.get("path") == device:
                return mountpoint(candidate)
        mounts = self.find_bootsel_mounts(labels)
        return mounts[0] if len(mounts) == 1 else None

    def durable_copy(self, source: Path, destination: Path) -> None:
        shutil.copy2(source, destination)
        os.sync()

    @contextmanager
    def build_lock(self, lock_path: Path) -> Iterator[None]:
        with lock_path.open("w", encoding="utf-8") as lock_file:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)

    def temporary_directory(self) -> Path:
        return Path(tempfile.gettempdir())

    def persistent_monitor_path(self) -> Path:
        return Path(__file__).resolve().parent / "serial_persistent.py"


PLATFORM = LinuxPlatformAdapter()
