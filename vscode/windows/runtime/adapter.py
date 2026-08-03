"""Native Windows adapter for serial, BOOTSEL, and process ownership."""

from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
import re
import signal
import sys
import tempfile
from typing import Any, Callable, Iterator

from vscode.runtime.platform_api import (
    PlatformOperationUnsupported,
    SerialPortRecord,
)


_COM_PORT = re.compile(r"^(?:\\\\\.\\)?COM([1-9][0-9]*)$", re.IGNORECASE)
_WINDOWS_BOOTSEL_FILESYSTEMS = {"FAT", "FAT32"}
_COPY_CHUNK_SIZE = 1024 * 1024


def normalize_com_port(port: str) -> str:
    """Return the stable pyserial spelling for COM ports, including COM10+."""

    value = str(port or "").strip().replace("/", "\\")
    match = _COM_PORT.fullmatch(value)
    return f"COM{int(match.group(1))}" if match else value


class WindowsPlatformAdapter:
    """Windows COM, process, locking, and runtime-path operations."""

    def __init__(
        self,
        port_enumerator: Callable[[], list[Any]] | None = None,
        volume_enumerator: Callable[[], list[dict[str, Any]]] | None = None,
    ):
        self._port_enumerator = port_enumerator
        self._volume_enumerator = volume_enumerator

    @property
    def platform_name(self) -> str:
        return "windows"

    def _port_infos(self) -> list[Any]:
        if self._port_enumerator is not None:
            return list(self._port_enumerator())
        try:
            from serial.tools import list_ports
        except ImportError:
            return []
        return list(list_ports.comports())

    def list_serial_ports(self) -> list[SerialPortRecord]:
        records = [
            SerialPortRecord.from_port_info(
                port_info,
                device=normalize_com_port(str(port_info.device)),
                platform="windows",
            )
            for port_info in self._port_infos()
            if self.is_serial_candidate(str(port_info.device))
        ]
        return sorted(records, key=lambda record: int(record.device[3:]))

    def serial_port_record(self, port: str) -> SerialPortRecord | None:
        normalized = normalize_com_port(port).casefold()
        return next(
            (
                record
                for record in self.list_serial_ports()
                if record.device.casefold() == normalized
            ),
            None,
        )

    def serial_candidate_paths(self) -> list[Path]:
        return [Path(record.device) for record in self.list_serial_ports()]

    def serial_port_exists(self, port: str) -> bool:
        return bool(port) and self.serial_port_record(port) is not None

    def resolve_serial_port(self, port: str) -> str:
        return normalize_com_port(port)

    def serial_fallback_candidates(self) -> list[str]:
        return [record.device for record in self.list_serial_ports()]

    def is_serial_candidate(self, port: str) -> bool:
        return bool(_COM_PORT.fullmatch(str(port or "").strip().replace("/", "\\")))

    @staticmethod
    def _kernel32():
        if sys.platform != "win32":
            raise PlatformOperationUnsupported("Windows process APIs require a Windows host")
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL
        kernel32.QueryFullProcessImageNameW.argtypes = [
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.LPWSTR,
            ctypes.POINTER(wintypes.DWORD),
        ]
        kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
        kernel32.GetProcessTimes.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME),
        ]
        kernel32.GetProcessTimes.restype = wintypes.BOOL
        kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        kernel32.TerminateProcess.restype = wintypes.BOOL
        return kernel32

    def process_cmdline(self, pid: int) -> str:
        if pid <= 0:
            return ""
        try:
            import ctypes
            from ctypes import wintypes

            kernel32 = self._kernel32()
            handle = kernel32.OpenProcess(0x1000, False, pid)
            if not handle:
                return ""
            try:
                capacity = wintypes.DWORD(32768)
                buffer = ctypes.create_unicode_buffer(capacity.value)
                if not kernel32.QueryFullProcessImageNameW(
                    handle, 0, buffer, ctypes.byref(capacity)
                ):
                    return ""
                return buffer.value
            finally:
                kernel32.CloseHandle(handle)
        except (OSError, ValueError):
            return ""

    def process_start_identity(self, pid: int) -> str:
        if pid <= 0:
            return ""
        try:
            import ctypes
            from ctypes import wintypes

            kernel32 = self._kernel32()
            handle = kernel32.OpenProcess(0x1000, False, pid)
            if not handle:
                return ""
            try:
                creation = wintypes.FILETIME()
                exit_time = wintypes.FILETIME()
                kernel_time = wintypes.FILETIME()
                user_time = wintypes.FILETIME()
                if not kernel32.GetProcessTimes(
                    handle,
                    ctypes.byref(creation),
                    ctypes.byref(exit_time),
                    ctypes.byref(kernel_time),
                    ctypes.byref(user_time),
                ):
                    return ""
                timestamp = (creation.dwHighDateTime << 32) | creation.dwLowDateTime
                return f"windows:{timestamp}"
            finally:
                kernel32.CloseHandle(handle)
        except (OSError, ValueError):
            return ""

    def port_owner_pids(self, port: str) -> list[int]:
        del port
        return []

    def request_monitor_release(self, pid: int) -> None:
        del pid

    def terminate_process(self, pid: int) -> None:
        import ctypes

        kernel32 = self._kernel32()
        handle = kernel32.OpenProcess(0x0001 | 0x1000, False, pid)
        if not handle:
            error = ctypes.get_last_error()
            if error == 5:
                raise PermissionError(pid)
            raise ProcessLookupError(pid)
        try:
            if not kernel32.TerminateProcess(handle, 1):
                error = ctypes.get_last_error()
                if error == 5:
                    raise PermissionError(pid)
                raise OSError(error, "TerminateProcess failed")
        finally:
            kernel32.CloseHandle(handle)

    def configure_serial_fd(self, fd: int) -> None:
        del fd

    def install_monitor_signal_handlers(
        self,
        interrupt_handler: Callable[[int, Any], None],
        release_handler: Callable[[int, Any], None],
    ) -> None:
        del release_handler
        signal.signal(signal.SIGINT, interrupt_handler)

    @staticmethod
    def _volume_kernel32():
        if sys.platform != "win32":
            raise PlatformOperationUnsupported("Windows volume APIs require a Windows host")
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetLogicalDriveStringsW.argtypes = [wintypes.DWORD, wintypes.LPWSTR]
        kernel32.GetLogicalDriveStringsW.restype = wintypes.DWORD
        kernel32.GetVolumeInformationW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.LPWSTR,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(wintypes.DWORD),
            wintypes.LPWSTR,
            wintypes.DWORD,
        ]
        kernel32.GetVolumeInformationW.restype = wintypes.BOOL
        kernel32.GetVolumeNameForVolumeMountPointW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.LPWSTR,
            wintypes.DWORD,
        ]
        kernel32.GetVolumeNameForVolumeMountPointW.restype = wintypes.BOOL
        return kernel32

    def _winapi_volumes(self) -> list[dict[str, Any]]:
        import ctypes
        from ctypes import wintypes

        kernel32 = self._volume_kernel32()
        required = kernel32.GetLogicalDriveStringsW(0, None)
        if required == 0:
            raise ctypes.WinError(ctypes.get_last_error())
        drive_buffer = ctypes.create_unicode_buffer(required + 1)
        written = kernel32.GetLogicalDriveStringsW(required, drive_buffer)
        if written == 0 or written > required:
            raise ctypes.WinError(ctypes.get_last_error())

        records: list[dict[str, Any]] = []
        for drive_root in drive_buffer[:written].split("\0"):
            if not drive_root:
                continue
            label_buffer = ctypes.create_unicode_buffer(261)
            filesystem_buffer = ctypes.create_unicode_buffer(261)
            serial_number = wintypes.DWORD()
            maximum_component_length = wintypes.DWORD()
            filesystem_flags = wintypes.DWORD()
            if not kernel32.GetVolumeInformationW(
                drive_root,
                label_buffer,
                len(label_buffer),
                ctypes.byref(serial_number),
                ctypes.byref(maximum_component_length),
                ctypes.byref(filesystem_flags),
                filesystem_buffer,
                len(filesystem_buffer),
            ):
                continue

            guid_buffer = ctypes.create_unicode_buffer(51)
            if not kernel32.GetVolumeNameForVolumeMountPointW(
                drive_root,
                guid_buffer,
                len(guid_buffer),
            ):
                continue
            records.append(
                {
                    "path": guid_buffer.value,
                    "volumeGuid": guid_buffer.value,
                    "label": label_buffer.value,
                    "fstype": filesystem_buffer.value,
                    "mountpoints": [drive_root],
                }
            )
        return records

    def _boot_volume_records(self, labels: set[str]) -> list[dict[str, Any]]:
        records = (
            self._volume_enumerator()
            if self._volume_enumerator is not None
            else self._winapi_volumes()
        )
        matches = []
        for record in records:
            label = str(record.get("label") or "")
            filesystem = str(record.get("fstype") or "").upper()
            volume_guid = str(record.get("volumeGuid") or record.get("path") or "")
            mountpoints = record.get("mountpoints")
            if (
                label not in labels
                or filesystem not in _WINDOWS_BOOTSEL_FILESYSTEMS
                or not volume_guid
                or not isinstance(mountpoints, list)
                or not any(mountpoints)
            ):
                continue
            matches.append(
                {
                    **record,
                    "path": volume_guid,
                    "volumeGuid": volume_guid,
                    "label": label,
                    "fstype": filesystem,
                    "mountpoints": [str(path) for path in mountpoints if path],
                }
            )
        return sorted(matches, key=lambda record: str(record["path"]).casefold())

    def find_bootsel_mounts(self, labels: set[str]) -> list[Path]:
        mounts = {
            Path(str(mountpoint))
            for record in self._boot_volume_records(labels)
            for mountpoint in record["mountpoints"]
        }
        return sorted(mounts, key=lambda path: str(path).casefold())

    def find_bootsel_blocks(self, labels: set[str]) -> list[dict[str, Any]]:
        return self._boot_volume_records(labels)

    def mount_bootsel_block(
        self,
        block: dict[str, Any],
        labels: set[str],
        mountpoint: Callable[[dict[str, Any]], Path | None],
    ) -> Path | None:
        existing = mountpoint(block)
        if existing is None:
            return None
        expected_guid = str(block.get("volumeGuid") or block.get("path") or "")
        for candidate in self._boot_volume_records(labels):
            candidate_guid = str(candidate.get("volumeGuid") or candidate.get("path") or "")
            if candidate_guid.casefold() != expected_guid.casefold():
                continue
            current = mountpoint(candidate)
            return current if current == existing else None
        return None

    def durable_copy(self, source: Path, destination: Path) -> None:
        try:
            expected_size = source.stat().st_size
            copied_size = 0
            with source.open("rb") as source_file, destination.open("wb") as destination_file:
                while True:
                    chunk = source_file.read(_COPY_CHUNK_SIZE)
                    if not chunk:
                        break
                    written = destination_file.write(chunk)
                    if written != len(chunk):
                        raise OSError(
                            f"short UF2 write: expected {len(chunk)}, wrote {written}"
                        )
                    copied_size += written
                if copied_size != expected_size:
                    raise OSError(
                        f"UF2 source changed during copy: expected {expected_size}, "
                        f"read {copied_size}"
                    )
                destination_file.flush()
                os.fsync(destination_file.fileno())
        except OSError:
            try:
                destination.unlink(missing_ok=True)
            except OSError:
                pass
            raise

    @contextmanager
    def build_lock(self, lock_path: Path) -> Iterator[None]:
        if sys.platform != "win32":
            raise PlatformOperationUnsupported("Windows build locks require a Windows host")
        import msvcrt

        lock_path.parent.mkdir(parents=True, exist_ok=True)
        with lock_path.open("a+b") as lock_file:
            if lock_file.tell() == 0:
                lock_file.write(b"\0")
                lock_file.flush()
            lock_file.seek(0)
            msvcrt.locking(lock_file.fileno(), msvcrt.LK_LOCK, 1)
            try:
                yield
            finally:
                lock_file.seek(0)
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_UNLCK, 1)

    def temporary_directory(self) -> Path:
        return Path(tempfile.gettempdir())

    def persistent_monitor_path(self) -> Path:
        return Path(__file__).resolve().parent / "serial_persistent.py"


PLATFORM = WindowsPlatformAdapter()
