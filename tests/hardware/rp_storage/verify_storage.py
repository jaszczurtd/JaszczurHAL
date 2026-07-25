#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import re
import time

import serial


STATUS_PATTERN = re.compile(
    rb"^JHSTORAGE1 eeprom=(-?\d+) commit=(-?\d+) count=(\d+) "
    rb"littlefs=(-?\d+) startup_format=(\d+) total=(\d+) used=(\d+)\n$"
)
FORMAT_PATTERN = re.compile(
    rb"^JHSTORAGE-FORMAT format=(-?\d+) mount=(-?\d+) "
    rb"total=(\d+) used=(\d+)\n$"
)


def read_line(port: serial.Serial, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while not received.endswith(b"\n"):
        chunk = port.read(1)
        if chunk:
            received.extend(chunk)
        elif time.monotonic() >= deadline:
            raise TimeoutError(f"incomplete response: {received!r}")
    return bytes(received)


def command(port: serial.Serial, value: bytes, timeout_s: float) -> bytes:
    port.reset_input_buffer()
    port.write(value)
    port.flush()
    return read_line(port, timeout_s)


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path, baudrate=115200, timeout=0.1, write_timeout=5.0, exclusive=True
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def parse(pattern: re.Pattern[bytes], line: bytes, keys: tuple[str, ...]) -> dict:
    match = pattern.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid response: {line!r}")
    return {key: int(value) for key, value in zip(keys, match.groups())}


def wait_for_reconnect(path: str, timeout_s: float) -> serial.Serial:
    deadline = time.monotonic() + timeout_s
    candidate = Path(path)
    while time.monotonic() < deadline:
        if candidate.exists():
            try:
                return open_port(path)
            except (OSError, serial.SerialException):
                pass
        time.sleep(0.1)
    raise TimeoutError(f"device did not reconnect at {path}")


def validate_status(status: dict) -> None:
    if status["eeprom"] != 1 or status["commit"] != 1:
        raise RuntimeError(f"EEPROM operation failed: {status}")
    if status["littlefs"] != 1 or status["total"] != 65536:
        raise RuntimeError(f"LittleFS operation failed: {status}")
    if status["used"] > status["total"]:
        raise RuntimeError(f"invalid LittleFS usage: {status}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    with open_port(args.port) as port:
        before = parse(
            STATUS_PATTERN,
            command(port, b"S", args.timeout),
            (
                "eeprom",
                "commit",
                "count",
                "littlefs",
                "startupFormat",
                "total",
                "used",
            ),
        )
        validate_status(before)
        formatted = parse(
            FORMAT_PATTERN,
            command(port, b"F", args.timeout),
            ("format", "mount", "total", "used"),
        )
        if formatted["format"] != 1 or formatted["mount"] != 1:
            raise RuntimeError(f"explicit LittleFS format failed: {formatted}")
        port.write(b"R")
        port.flush()

    time.sleep(0.5)
    with wait_for_reconnect(args.port, args.timeout) as port:
        after = parse(
            STATUS_PATTERN,
            command(port, b"S", args.timeout),
            (
                "eeprom",
                "commit",
                "count",
                "littlefs",
                "startupFormat",
                "total",
                "used",
            ),
        )
        validate_status(after)

    if after["count"] != before["count"] + 1:
        raise RuntimeError(
            f"EEPROM did not persist across reset: before={before}, after={after}"
        )
    if after["startupFormat"] != 0:
        raise RuntimeError(f"LittleFS did not mount after reset: {after}")

    print(
        json.dumps(
            {
                "port": args.port,
                "before": before,
                "format": formatted,
                "afterReset": after,
                "status": "pass",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
