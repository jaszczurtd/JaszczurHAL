"""Versioned persistent-monitor ownership and cooperative release channel."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
import json
import os
from pathlib import Path
import time
from typing import Any

from vscode.runtime.platform_api import PlatformAdapter


MONITOR_OWNERSHIP_VERSION = 1
RELEASE_UPLOAD = "upload"
RELEASE_REPLACE = "replace"


class MonitorOwnershipConflict(RuntimeError):
    """Raised when another live monitor owns the same project port."""


@dataclass(frozen=True)
class MonitorOwnership:
    version: int
    port: str
    pid: int
    process_start: str
    project: str
    created_ns: int
    marker_path: Path
    release_path: Path

    def payload(self) -> dict[str, Any]:
        value = asdict(self)
        value.pop("marker_path")
        value.pop("release_path")
        value["processStart"] = value.pop("process_start")
        value["createdNs"] = value.pop("created_ns")
        return value


def _project_text(project_dir: Path | None) -> str:
    return str(project_dir.resolve()) if project_dir is not None else ""


def _ownership_paths(
    adapter: PlatformAdapter,
    project_dir: Path | None,
    port: str,
) -> tuple[Path, Path]:
    project = os.path.normcase(_project_text(project_dir))
    normalized_port = adapter.resolve_serial_port(port).casefold()
    project_key = hashlib.sha256(project.encode("utf-8")).hexdigest()[:16]
    port_key = hashlib.sha256(normalized_port.encode("utf-8")).hexdigest()[:16]
    root = adapter.temporary_directory() / "jaszczurhal-monitor" / project_key
    marker = root / f"{port_key}.json"
    return marker, marker.with_suffix(".release.json")


def _unlink(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass
    except OSError:
        pass


def _read_json(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def _parse_ownership(
    marker_path: Path,
    release_path: Path,
    value: dict[str, Any] | None,
) -> MonitorOwnership | None:
    if value is None:
        return None
    try:
        ownership = MonitorOwnership(
            version=int(value["version"]),
            port=str(value["port"]),
            pid=int(value["pid"]),
            process_start=str(value["processStart"]),
            project=str(value["project"]),
            created_ns=int(value["createdNs"]),
            marker_path=marker_path,
            release_path=release_path,
        )
    except (KeyError, TypeError, ValueError):
        return None
    if ownership.version != MONITOR_OWNERSHIP_VERSION or ownership.pid <= 0:
        return None
    if not ownership.process_start:
        return None
    return ownership


def load_monitor_ownership(
    adapter: PlatformAdapter,
    project_dir: Path | None,
    port: str,
    *,
    cleanup_stale: bool = True,
) -> MonitorOwnership | None:
    marker_path, release_path = _ownership_paths(adapter, project_dir, port)
    if not marker_path.exists():
        return None
    ownership = _parse_ownership(
        marker_path,
        release_path,
        _read_json(marker_path),
    )
    expected_project = _project_text(project_dir)
    expected_port = adapter.resolve_serial_port(port).casefold()
    valid = bool(
        ownership is not None
        and os.path.normcase(ownership.project) == os.path.normcase(expected_project)
        and adapter.resolve_serial_port(ownership.port).casefold() == expected_port
        and adapter.process_start_identity(ownership.pid) == ownership.process_start
    )
    if valid:
        return ownership
    if cleanup_stale:
        _unlink(marker_path)
        _unlink(release_path)
    return None


def register_monitor_ownership(
    adapter: PlatformAdapter,
    project_dir: Path | None,
    port: str,
    *,
    pid: int | None = None,
) -> MonitorOwnership:
    owner_pid = int(pid if pid is not None else os.getpid())
    process_start = adapter.process_start_identity(owner_pid)
    if not process_start:
        raise RuntimeError(f"cannot determine process start identity for PID {owner_pid}")
    marker_path, release_path = _ownership_paths(adapter, project_dir, port)
    marker_path.parent.mkdir(parents=True, exist_ok=True)

    existing = load_monitor_ownership(adapter, project_dir, port)
    if existing is not None:
        raise MonitorOwnershipConflict(
            f"port {port} already belongs to monitor PID {existing.pid}"
        )

    ownership = MonitorOwnership(
        version=MONITOR_OWNERSHIP_VERSION,
        port=adapter.resolve_serial_port(port),
        pid=owner_pid,
        process_start=process_start,
        project=_project_text(project_dir),
        created_ns=time.time_ns(),
        marker_path=marker_path,
        release_path=release_path,
    )
    _unlink(release_path)
    encoded = json.dumps(ownership.payload(), sort_keys=True) + "\n"
    try:
        descriptor = os.open(marker_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as exc:
        other = load_monitor_ownership(adapter, project_dir, port)
        detail = f" PID {other.pid}" if other is not None else ""
        raise MonitorOwnershipConflict(f"port {port} ownership raced with monitor{detail}") from exc
    with os.fdopen(descriptor, "w", encoding="utf-8") as marker_file:
        marker_file.write(encoded)
        marker_file.flush()
        os.fsync(marker_file.fileno())
    return ownership


def unregister_monitor_ownership(ownership: MonitorOwnership) -> None:
    current = _parse_ownership(
        ownership.marker_path,
        ownership.release_path,
        _read_json(ownership.marker_path),
    )
    if current is not None and (
        current.pid == ownership.pid
        and current.process_start == ownership.process_start
    ):
        _unlink(ownership.marker_path)
        _unlink(ownership.release_path)


def request_monitor_release(
    adapter: PlatformAdapter,
    ownership: MonitorOwnership,
    action: str,
) -> None:
    if action not in {RELEASE_UPLOAD, RELEASE_REPLACE}:
        raise ValueError(f"unsupported monitor release action: {action}")
    if adapter.process_start_identity(ownership.pid) != ownership.process_start:
        raise ProcessLookupError(ownership.pid)
    payload = {
        "version": MONITOR_OWNERSHIP_VERSION,
        "port": ownership.port,
        "pid": ownership.pid,
        "processStart": ownership.process_start,
        "action": action,
        "requestedNs": time.time_ns(),
    }
    temporary = ownership.release_path.with_name(
        f"{ownership.release_path.name}.{os.getpid()}.tmp"
    )
    try:
        temporary.write_text(
            json.dumps(payload, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(ownership.release_path)
    finally:
        _unlink(temporary)
    adapter.request_monitor_release(ownership.pid)


def monitor_release_action(ownership: MonitorOwnership) -> str | None:
    value = _read_json(ownership.release_path)
    if value is None:
        return None
    try:
        valid = (
            int(value.get("version")) == MONITOR_OWNERSHIP_VERSION
            and int(value.get("pid")) == ownership.pid
            and str(value.get("processStart")) == ownership.process_start
            and str(value.get("port")).casefold() == ownership.port.casefold()
        )
    except (TypeError, ValueError):
        valid = False
    action = str(value.get("action")) if valid else ""
    return action if action in {RELEASE_UPLOAD, RELEASE_REPLACE} else None
