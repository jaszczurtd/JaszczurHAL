#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from vscode.runtime.serial_io import read_line  # noqa: E402


STATUS_PATTERN = re.compile(
    rb"^JHFLASH1 task0=(\d+) task1=(\d+) core0=(\d+) core1=(\d+) "
    rb"reset=(-?\d+) fault=(\d) pc=([0-9a-f]{8}) lr=([0-9a-f]{8})\n$"
)
RESULT_PATTERN = re.compile(
    rb"^JHFLASH-RESULT usb=(-?\d+) raw=(-?\d+) noop=(-?\d+) "
    rb"core1=(-?\d+) dma_ram=(-?\d+) dma_xip=(-?\d+) xip=(-?\d+) "
    rb"recursive=(-?\d+) flash=(-?\d+) interrupt=(-?\d+) recovery=(-?\d+) "
    rb"count=(\d+)\n$"
)
LOAD_LINE_PATTERN = re.compile(rb"^JHLOAD i=(\d+) core=(\d) status=(-?\d+)\n$")
LOAD_RESULT_PATTERN = re.compile(
    rb"^JHLOAD-RESULT init=(-?\d+) dma=(-?\d+) scan=(-?\d+) kv0=(-?\d+) "
    rb"kv1=(-?\d+) fail=(\d+) rb=(-?\d+) writes=(\d+) blocks=(\d+) "
    rb"guard=(\d)\n$"
)
LOAD_WRITES = 16


def command(port: serial.Serial, value: bytes, timeout_s: float) -> bytes:
    port.reset_input_buffer()
    port.write(value)
    port.flush()
    return read_line(port, time.monotonic() + timeout_s)


def parse_status(line: bytes) -> dict[str, int]:
    match = STATUS_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid status response: {line!r}")
    keys = ("task0", "task1", "core0", "core1", "reset", "fault")
    status = {key: int(value) for key, value in zip(keys, match.groups()[:6])}
    status["pc"] = match.group(7).decode()
    status["lr"] = match.group(8).decode()
    if status["core0"] != 0 or status["core1"] != 1:
        raise RuntimeError(f"invalid core affinity: {status}")
    if status["task0"] == 0 or status["task1"] == 0:
        raise RuntimeError(f"application tasks are not running: {status}")
    if status["fault"] != 0:
        raise RuntimeError(
            f"previous boot ended in a fault: pc=0x{status['pc']} "
            f"lr=0x{status['lr']} reset={status['reset']}"
        )
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
        "dma_ram",
        "dma_xip",
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
        "dma_ram": 1,
        "dma_xip": -2,
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


def run_load(port: serial.Serial, timeout_s: float) -> dict[str, int]:
    """Send the load command and keep both CDC directions busy until the
    firmware reports the result; every JHLOAD line must arrive in order."""
    port.reset_input_buffer()
    port.write(b"L")
    port.flush()
    deadline = time.monotonic() + timeout_s
    junk = bytes(range(64))
    seen = 0
    pending = bytearray()
    lines: list[bytes] = []
    while True:
        if not lines:
            port.write(junk)
            time.sleep(0.02)
            pending.extend(port.read(port.in_waiting or 1))
            while b"\n" in pending:
                raw, _, rest = pending.partition(b"\n")
                lines.append(bytes(raw) + b"\n")
                pending = bytearray(rest)
        line = lines.pop(0) if lines else b""
        if line:
            progress = LOAD_LINE_PATTERN.fullmatch(line)
            if progress is not None:
                index = int(progress.group(1))
                if index != seen:
                    raise RuntimeError(f"load line out of order: {line!r}")
                seen += 1
                continue
            result = LOAD_RESULT_PATTERN.fullmatch(line)
            if result is not None:
                keys = ("init", "dma", "scan", "kv0", "kv1", "fail", "rb",
                        "writes", "blocks", "guard")
                values = {k: int(v) for k, v in zip(keys, result.groups())}
                values["lines"] = seen
                expected = {
                    "init": 1,
                    "dma": 1,
                    "scan": 1,
                    "kv0": 1,
                    "kv1": 1,
                    "fail": 0,
                    "rb": 1,
                    "writes": LOAD_WRITES,
                    "lines": LOAD_WRITES,
                    "guard": 1,
                }
                blocks = values.pop("blocks")
                if values != expected or blocks == 0:
                    raise RuntimeError(
                        f"load result mismatch: expected {expected}, got "
                        f"{values} blocks={blocks}"
                    )
                values["blocks"] = blocks
                return values
            raise RuntimeError(f"unexpected line during load: {line!r}")
        if time.monotonic() >= deadline:
            raise RuntimeError(
                f"load probe timed out after {seen} of {LOAD_WRITES} writes"
            )


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
        middle = parse_status(command(port, b"S", args.timeout))
        try:
            load = run_load(port, args.timeout * 3)
        except serial.SerialException as error:
            raise RuntimeError(
                "device dropped off USB during the KV load probe; reconnect "
                "and query S: a fault record names the crash site"
            ) from error
        after = parse_status(command(port, b"S", args.timeout))

    if middle["task0"] <= before["task0"] or after["task0"] <= middle["task0"]:
        raise RuntimeError("core-0 task did not resume after flash operations")
    if middle["task1"] <= before["task1"] or after["task1"] <= middle["task1"]:
        raise RuntimeError("core-1 task did not resume after flash operations")

    print(
        json.dumps(
            {
                "port": args.port,
                "statusBefore": before,
                "transactions": result,
                "load": load,
                "statusAfter": after,
                "status": "pass",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
