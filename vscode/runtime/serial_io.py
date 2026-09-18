"""Portable pyserial adapter shared by host-side hardware verification."""

from __future__ import annotations

import time
from typing import Any, Callable, Optional

try:
    import serial as _serial
except ImportError:  # pragma: no cover - exercised by dependency diagnostics
    _serial = None

try:
    import termios as _termios
except ImportError:  # pragma: no cover - unavailable on native Windows
    _termios = None


def serial_error_types() -> tuple[type[BaseException], ...]:
    errors: list[type[BaseException]] = [OSError]
    if _serial is not None:
        errors.append(_serial.SerialException)
    if _termios is not None and not any(
        issubclass(_termios.error, error) for error in errors
    ):
        errors.append(_termios.error)
    return tuple(errors)


def open_serial_port(
    path: str,
    *,
    baudrate: int = 115200,
    timeout: float = 0.1,
    write_timeout: float = 5.0,
    exclusive: bool = True,
) -> Any:
    """Open a serial port with the strongest portable ownership contract."""
    if _serial is None:
        raise RuntimeError("pyserial is required for serial hardware verification")
    options: dict[str, Any] = {
        "baudrate": baudrate,
        "timeout": timeout,
        "write_timeout": write_timeout,
    }
    if exclusive:
        options["exclusive"] = True
    return _serial.Serial(path, **options)


def read_line(
    port: Any,
    deadline: float,
    *,
    limit: int = 4096,
    clock: Optional[Callable[[], float]] = None,
) -> bytes:
    """Read one newline-terminated line before an absolute monotonic deadline.

    The deadline is checked before every read, so a device that keeps sending
    bytes without a newline still times out. A line reaching ``limit`` bytes
    without a newline is rejected instead of growing without bound.
    """
    now = clock if clock is not None else time.monotonic
    line = bytearray()
    while True:
        if now() >= deadline:
            raise TimeoutError(f"incomplete line: {bytes(line[-80:])!r}")
        chunk = port.read(1)
        if not chunk:
            continue
        line.extend(chunk)
        if line.endswith(b"\n"):
            return bytes(line)
        if len(line) >= limit:
            raise ValueError(f"line exceeds {limit} bytes without a newline")
