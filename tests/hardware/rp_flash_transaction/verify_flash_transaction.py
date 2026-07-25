#!/usr/bin/env python3

import argparse
import json
import re
import time

import serial


STATUS_PATTERN = re.compile(
    rb"^JHFLASH1 task0=(\d+) task1=(\d+) core0=(\d+) core1=(\d+)\n$"
)
RESULT_PATTERN = re.compile(
    rb"^JHFLASH-RESULT usb=(-?\d+) raw=(-?\d+) noop=(-?\d+) "
    rb"core1=(-?\d+) dma=(-?\d+) xip=(-?\d+) recursive=(-?\d+) "
    rb"flash=(-?\d+) interrupt=(-?\d+) recovery=(-?\d+) count=(\d+)\n$"
)


def read_line(port: serial.Serial, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while not received.endswith(b"\n"):
        chunk = port.read(1)
        if chunk:
            received.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise TimeoutError(f"incomplete response: {received!r}")
    return bytes(received)


def command(port: serial.Serial, value: bytes, timeout_s: float) -> bytes:
    port.reset_input_buffer()
    port.write(value)
    port.flush()
    return read_line(port, timeout_s)


def parse_status(line: bytes) -> dict[str, int]:
    match = STATUS_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid status response: {line!r}")
    keys = ("task0", "task1", "core0", "core1")
    status = {key: int(value) for key, value in zip(keys, match.groups())}
    if status["core0"] != 0 or status["core1"] != 1:
        raise RuntimeError(f"invalid core affinity: {status}")
    if status["task0"] == 0 or status["task1"] == 0:
        raise RuntimeError(f"application tasks are not running: {status}")
    return status


def parse_result(line: bytes) -> dict[str, int]:
    match = RESULT_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid transaction response: {line!r}")
    keys = (
        "usb",
        "raw",
        "noop",
        "core1",
        "dma",
        "xip",
        "recursive",
        "flash",
        "interrupt",
        "recovery",
        "count",
    )
    result = {key: int(value) for key, value in zip(keys, match.groups())}
    expected = {
        "usb": 1,
        "raw": 1,
        "noop": 1,
        "core1": 1,
        "dma": -2,
        "xip": -1,
        "recursive": -20,
        "flash": 1,
        "interrupt": -14,
        "recovery": 1,
        "count": 1,
    }
    if result != expected:
        raise RuntimeError(
            f"transaction result mismatch: expected {expected}, got {result}"
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    with serial.Serial(
        args.port,
        baudrate=115200,
        timeout=0.1,
        write_timeout=5.0,
        exclusive=True,
    ) as port:
        port.dtr = True
        time.sleep(0.2)
        before = parse_status(command(port, b"S", args.timeout))
        result = parse_result(command(port, b"T", args.timeout))
        after = parse_status(command(port, b"S", args.timeout))

    if after["task0"] <= before["task0"]:
        raise RuntimeError("core-0 task did not resume after flash operations")
    if after["task1"] <= before["task1"]:
        raise RuntimeError("core-1 task did not resume after flash operations")

    print(
        json.dumps(
            {
                "port": args.port,
                "statusBefore": before,
                "transactions": result,
                "statusAfter": after,
                "status": "pass",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
