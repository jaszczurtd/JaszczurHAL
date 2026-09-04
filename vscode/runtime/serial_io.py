"""Portable pyserial adapter shared by host-side hardware verification."""

from __future__ import annotations

from typing import Any

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
