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


RESULT_PATTERN = re.compile(
    rb"^JHKV3 target=(rp2040|rp2350-arm) "
    rb"invalidate=(-?\d+)/(\d+) body=(-?\d+)/(\d+) "
    rb"verify=(-?\d+)/(\d+) publish=(-?\d+)/(\d+) "
    rb"deferred=(-?\d+)/(\d+)/(\d+) "
    rb"readthrough=(-?\d+)/(-?\d+)/(\d+)/(-?\d+)/(\d+)/"
    rb"(-?\d+)/(-?\d+)/(\d+)/(-?\d+)/(\d+)/(\d+)/"
    rb"(-?\d+)/(-?\d+)/(\d+)/(-?\d+)/(\d+)/(\d+)\n$"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--target", required=True, choices=("rp2040", "rp2350-arm"))
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    with serial.Serial(
        args.port,
        baudrate=115200,
        timeout=0.1,
        write_timeout=5.0,
        exclusive=True,
    ) as port:
        port.dtr = True
        time.sleep(0.3)
        port.reset_input_buffer()
        port.write(b"T")
        port.flush()
        line = read_line(port, time.monotonic() + args.timeout)

    match = RESULT_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid KV probe response: {line!r}")
    values = match.groups()
    target = values[0].decode("ascii")
    numeric = tuple(int(value) for value in values[1:])
    expected = (
        -4,
        100,
        -4,
        100,
        -4,
        100,
        -4,
        200,
        1,
        11,
        22,
        1,
        -2,
        0,
        -2,
        0,
        1,
        1,
        20,
        1,
        4,
        1,
        1,
        1,
        20,
        1,
        4,
        1,
    )
    if target != args.target or numeric != expected:
        raise RuntimeError(
            f"KV recovery mismatch: target={target!r}, values={numeric!r}, "
            f"expected target={args.target!r}, values={expected!r}"
        )

    print(json.dumps({"port": args.port, "target": target, "status": "pass"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
