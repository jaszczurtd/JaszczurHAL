#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import threading
import time

import serial


STATUS_COMMAND = b"\xa5"
STATUS_PATTERN = re.compile(
    rb"^JHRTOS1 core0=(\d+) core1=(\d+) task0=(\d+) task1=(\d+) "
    rb"heap=(\d+) scheduler=(\d+)\n$"
)


def deterministic_payload(length: int, salt: int) -> bytes:
    return bytes(((index * 73 + salt * 29) & 0xFF) for index in range(length))


def read_exact(port: serial.Serial, length: int, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while len(received) < length:
        chunk = port.read(length - len(received))
        if chunk:
            received.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"received {len(received)} of {length} expected bytes"
            )
    return bytes(received)


def read_line(port: serial.Serial, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while not received.endswith(b"\n"):
        chunk = port.read(1)
        if chunk:
            received.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise TimeoutError(f"incomplete status response: {received!r}")
    return bytes(received)


def query_status(port: serial.Serial, timeout_s: float) -> dict[str, int]:
    port.reset_input_buffer()
    port.write(STATUS_COMMAND)
    port.flush()
    line = read_line(port, timeout_s)
    match = STATUS_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid status response: {line!r}")
    keys = ("core0", "core1", "task0", "task1", "heap", "scheduler")
    return {key: int(value) for key, value in zip(keys, match.groups())}


def exchange(
    port: serial.Serial, payload: bytes, timeout_s: float, pause_before_read_s: float
) -> float:
    port.reset_input_buffer()
    port.reset_output_buffer()
    started = time.monotonic()
    writer_result = {"written": 0, "error": None}

    def write_payload() -> None:
        try:
            writer_result["written"] = port.write(payload)
            port.flush()
        except Exception as exc:
            writer_result["error"] = exc

    writer = threading.Thread(target=write_payload, name="freertos-cdc-writer")
    writer.start()
    if pause_before_read_s > 0.0:
        time.sleep(pause_before_read_s)
    echoed = read_exact(port, len(payload), timeout_s)
    writer.join(timeout_s)
    if writer.is_alive():
        raise TimeoutError("CDC writer did not finish")
    if writer_result["error"] is not None:
        raise writer_result["error"]
    if writer_result["written"] != len(payload):
        raise RuntimeError(
            f"wrote {writer_result['written']} of {len(payload)} bytes"
        )
    if echoed != payload:
        mismatch = next(
            (
                index
                for index, (expected, actual) in enumerate(zip(payload, echoed))
                if expected != actual
            ),
            min(len(payload), len(echoed)),
        )
        raise RuntimeError(f"echo mismatch at byte {mismatch}")
    return time.monotonic() - started


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path,
        baudrate=115200,
        timeout=0.1,
        write_timeout=10.0,
        exclusive=True,
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def validate_status(status: dict[str, int]) -> None:
    if status["core0"] != 0 or status["core1"] != 1:
        raise RuntimeError(f"invalid SMP affinity: {status}")
    if status["task0"] == 0 or status["task1"] == 0:
        raise RuntimeError(f"application task did not run: {status}")
    if status["heap"] == 0:
        raise RuntimeError(f"FreeRTOS heap is exhausted: {status}")
    if status["scheduler"] != 2:
        raise RuntimeError(f"FreeRTOS scheduler is not running: {status}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--bytes", type=int, default=65536)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--backpressure-pause", type=float, default=0.75)
    args = parser.parse_args()

    with open_port(args.port) as port:
        status_before = query_status(port, args.timeout)
        validate_status(status_before)

        payload = deterministic_payload(args.bytes, 7)
        elapsed = exchange(
            port, payload, args.timeout, args.backpressure_pause
        )

        status_after = query_status(port, args.timeout)
        validate_status(status_after)

    if status_after["task0"] <= status_before["task0"]:
        raise RuntimeError("app_task0 did not advance during USB transfer")
    if status_after["task1"] <= status_before["task1"]:
        raise RuntimeError("app_task1 did not advance during USB transfer")

    result = {
        "port": args.port,
        "statusBefore": status_before,
        "usb": {
            "bytes": len(payload),
            "elapsedSeconds": round(elapsed, 3),
            "bytesPerSecond": round(len(payload) / elapsed),
            "sha256": hashlib.sha256(payload).hexdigest(),
        },
        "statusAfter": status_after,
        "status": "pass",
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
