#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import re
import time

import serial


WRITE_PATTERN = re.compile(
    rb"^JHSD1-WRITE target=([^ ]+) board=([^ ]+) runtime=([^ ]+) "
    rb"eeprom=(-?\d+) spi=(-?\d+) init=(-?\d+) append0=(-?\d+) "
    rb"append1=(-?\d+) append2=(-?\d+) close=(-?\d+) before=(-?\d+) "
    rb"after=(-?\d+) file=([^ ]+) bytes=(\d+) checksum=([0-9a-f]{8})\n$"
)
VERIFY_PATTERN = re.compile(
    rb"^JHSD1-VERIFY target=([^ ]+) board=([^ ]+) runtime=([^ ]+) "
    rb"mount=(\d+) open=(\d+) seek=(\d+) close=(\d+) counter=(-?\d+) "
    rb"file=([^ ]+) size=(\d+) bytes=(\d+) expected=(\d+) "
    rb"checksum=([0-9a-f]{8}) match=(\d+)\n$"
)


def bounded_log_number(number: int) -> int:
    return 0 if number < 0 else number % 100000


def content_token(file_number: int, record: int) -> int:
    return (
        file_number * 2654435761
        ^ record * 0x9E3779B9
        ^ 0x4A485344
    ) & 0xFFFFFFFF


def expected_content(log_number: int) -> bytes:
    file_number = bounded_log_number(log_number)
    return "".join(
        f"JHSD1 file={file_number:05d} record={record} "
        f"token={content_token(file_number, record):08x}\n"
        for record in range(3)
    ).encode()


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path,
        baudrate=115200,
        timeout=0.1,
        write_timeout=5.0,
        exclusive=True,
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def read_prefixed_line(
    port: serial.Serial, prefix: bytes, timeout_s: float
) -> bytes:
    deadline = time.monotonic() + timeout_s
    line = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(1)
        if not chunk:
            continue
        line.extend(chunk)
        if not line.endswith(b"\n"):
            continue
        completed = bytes(line)
        if completed.startswith(prefix):
            return completed
        line.clear()
    raise TimeoutError(f"response {prefix!r} not received")


def command(
    port: serial.Serial, value: bytes, prefix: bytes, timeout_s: float
) -> bytes:
    port.reset_input_buffer()
    port.write(value)
    port.flush()
    return read_prefixed_line(port, prefix, timeout_s)


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


def parse_write(line: bytes) -> dict[str, int | str]:
    match = WRITE_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid write response: {line!r}")
    keys = (
        "target",
        "board",
        "runtime",
        "eeprom",
        "spi",
        "init",
        "append0",
        "append1",
        "append2",
        "close",
        "before",
        "after",
        "file",
        "bytes",
        "checksum",
    )
    values: list[int | str] = [
        match.group(1).decode(),
        match.group(2).decode(),
        match.group(3).decode(),
        *(int(value) for value in match.groups()[3:12]),
        match.group(13).decode(),
        int(match.group(14)),
        int(match.group(15), 16),
    ]
    return dict(zip(keys, values))


def parse_verify(line: bytes) -> dict[str, int | str]:
    match = VERIFY_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid verify response: {line!r}")
    keys = (
        "target",
        "board",
        "runtime",
        "mount",
        "open",
        "seek",
        "close",
        "counter",
        "file",
        "size",
        "bytes",
        "expected",
        "checksum",
        "match",
    )
    values: list[int | str] = [
        match.group(1).decode(),
        match.group(2).decode(),
        match.group(3).decode(),
        *(int(value) for value in match.groups()[3:8]),
        match.group(9).decode(),
        *(int(value) for value in match.groups()[9:12]),
        int(match.group(13), 16),
        int(match.group(14)),
    ]
    return dict(zip(keys, values))


def validate_identity(
    status: dict[str, int | str], target: str, board: str, runtime: str
) -> None:
    expected = {"target": target, "board": board, "runtime": runtime}
    actual = {key: status[key] for key in expected}
    if actual != expected:
        raise RuntimeError(
            f"firmware identity mismatch: expected={expected}, actual={actual}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument(
        "--target",
        required=True,
        choices=("rp2040", "rp2350-arm", "rp2350-riscv"),
    )
    parser.add_argument("--board", required=True, choices=("pico", "pico2"))
    parser.add_argument(
        "--runtime", required=True, choices=("baremetal", "freertos")
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    expected_board = "pico" if args.target == "rp2040" else "pico2"
    if args.board != expected_board:
        parser.error(f"{args.target} requires --board {expected_board}")

    with open_port(args.port) as port:
        written = parse_write(
            command(port, b"W", b"JHSD1-WRITE ", args.timeout)
        )
        validate_identity(
            written, args.target, args.board, args.runtime
        )
        for key in (
            "eeprom",
            "spi",
            "init",
            "append0",
            "append1",
            "append2",
            "close",
        ):
            if written[key] != 1:
                raise RuntimeError(f"{key} failed: {written}")
        if written["after"] != written["before"] + 1:
            raise RuntimeError(f"EEPROM counter did not advance: {written}")

        expected = expected_content(int(written["before"]))
        expected_name = (
            f"log{bounded_log_number(int(written['before'])):05d}.txt"
        )
        if written["file"] != expected_name:
            raise RuntimeError(f"unexpected log filename: {written}")
        if written["bytes"] != len(expected):
            raise RuntimeError(f"unexpected logged byte count: {written}")
        if written["checksum"] != fnv1a(expected):
            raise RuntimeError(f"unexpected expected-content checksum: {written}")

        port.write(b"R")
        port.flush()

    time.sleep(0.5)
    with wait_for_reconnect(args.port, args.timeout) as port:
        verified = parse_verify(
            command(port, b"V", b"JHSD1-VERIFY ", args.timeout)
        )

    validate_identity(verified, args.target, args.board, args.runtime)
    for key in ("mount", "open", "seek", "close", "match"):
        if verified[key] != 1:
            raise RuntimeError(f"{key} failed after reset: {verified}")
    if verified["counter"] != written["after"]:
        raise RuntimeError(
            f"EEPROM counter did not persist: write={written}, verify={verified}"
        )
    if verified["file"] != written["file"]:
        raise RuntimeError(
            f"verified the wrong log file: write={written}, verify={verified}"
        )
    if (
        verified["bytes"] != len(expected)
        or verified["expected"] != len(expected)
        or verified["checksum"] != fnv1a(expected)
    ):
        raise RuntimeError(f"persisted content mismatch: {verified}")
    if verified["size"] < len(expected):
        raise RuntimeError(f"persisted log is truncated: {verified}")

    print(
        json.dumps(
            {
                "port": args.port,
                "target": args.target,
                "board": args.board,
                "runtime": args.runtime,
                "write": written,
                "afterReset": verified,
                "status": "pass",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
