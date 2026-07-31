"""Platform boundary for the shared JaszczurHAL host runtime."""

from __future__ import annotations

from contextlib import AbstractContextManager
import importlib
from pathlib import Path
import sys
from typing import Any, Callable, cast, Protocol


class PlatformOperationUnsupported(RuntimeError):
    """Raised when the active host has no adapter for an operation."""


class PlatformAdapter(Protocol):
    """Host operations required by the shared runtime and monitor."""

    def serial_candidate_paths(self) -> list[Path]: ...

    def serial_port_exists(self, port: str) -> bool: ...

    def resolve_serial_port(self, port: str) -> str: ...

    def serial_by_id_links(self, port: str) -> list[Path]: ...

    def serial_identity_text(self, port: Path) -> str: ...

    def verified_identity_ports(
        self,
        expected_tokens: list[str],
        normalize: Callable[[str], str],
    ) -> list[tuple[Path, Path | None]]: ...

    def serial_fallback_candidates(self) -> list[str]: ...

    def is_serial_candidate(self, port: str) -> bool: ...

    def process_cmdline(self, pid: int) -> str: ...

    def port_owner_pids(self, port: str) -> list[int]: ...

    def request_monitor_release(self, pid: int) -> None: ...

    def terminate_process(self, pid: int) -> None: ...

    def configure_serial_fd(self, fd: int) -> None: ...

    def install_monitor_signal_handlers(
        self,
        interrupt_handler: Callable[[int, Any], None],
        release_handler: Callable[[int, Any], None],
    ) -> None: ...

    def find_bootsel_mounts(self, labels: set[str]) -> list[Path]: ...

    def find_bootsel_blocks(self, labels: set[str]) -> list[dict[str, Any]]: ...

    def mount_bootsel_block(
        self,
        block: dict[str, Any],
        labels: set[str],
        mountpoint: Callable[[dict[str, Any]], Path | None],
    ) -> Path | None: ...

    def durable_copy(self, source: Path, destination: Path) -> None: ...

    def build_lock(self, lock_path: Path) -> AbstractContextManager[None]: ...

    def temporary_directory(self) -> Path: ...

    def persistent_monitor_path(self) -> Path: ...


class UnsupportedPlatformAdapter:
    """Import-safe placeholder until a host adapter is implemented."""

    def __getattr__(self, operation: str):
        raise PlatformOperationUnsupported(
            f"platform operation '{operation}' is unavailable on {sys.platform}"
        )


_adapter: PlatformAdapter | None = None


def set_platform_adapter(adapter: PlatformAdapter | None) -> None:
    """Override the active adapter for deterministic tests."""

    global _adapter
    _adapter = adapter


def get_platform_adapter() -> PlatformAdapter:
    """Load the host adapter lazily so the shared core stays import-safe."""

    global _adapter
    if _adapter is None:
        if sys.platform.startswith("linux"):
            module = importlib.import_module("vscode.linux.runtime.adapter")
            _adapter = module.PLATFORM
        else:
            _adapter = cast(PlatformAdapter, UnsupportedPlatformAdapter())
    return _adapter
