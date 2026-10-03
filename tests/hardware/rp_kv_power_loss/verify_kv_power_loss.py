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
    rb"^JHKV4 target=(rp2040|rp2350-arm) "
    rb"invalidate=(-?\d+)/(\d+) body=(-?\d+)/(\d+) "
    rb"verify=(-?\d+)/(\d+) publish=(-?\d+)/(\d+) "
    rb"torn=(-?\d+)/(\d+) programmed=(-?\d+)/(\d+) "
    rb"deferred=(-?\d+)/(\d+)/(\d+) "
    rb"readthrough=(-?\d+)/(-?\d+)/(\d+)/(-?\d+)/(\d+)/"
    rb"(-?\d+)/(-?\d+)/(\d+)/(-?\d+)/(\d+)/(\d+)/"
    rb"(-?\d+)/(-?\d+)/(\d+)/(-?\d+)/(\d+)/(\d+) "
    rb"timing=(-?\d+)/(\d+)/(\d+)/(\d+)\n$"
)

# A log commit programs a page or two; a compaction into the prepared bank
# programs the pages holding data. Both stay far below one sector erase.
APPEND_LIMIT_US = 10_000
COMPACT_LIMIT_US = 20_000


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
    timing = numeric[-4:]
    numeric = numeric[:-4]
    expected = (
        -4,
        100,
        -4,
        100,
        -4,
        100,
        -4,
        200,
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
    timed, append_us, prepare_us, compact_us = timing
    if timed != 1 or append_us > APPEND_LIMIT_US or compact_us > COMPACT_LIMIT_US:
        raise RuntimeError(
            f"KV timing out of bounds: status={timed}, append={append_us} us "
            f"(limit {APPEND_LIMIT_US}), compaction={compact_us} us "
            f"(limit {COMPACT_LIMIT_US}), prepare={prepare_us} us"
        )

    print(
        json.dumps(
            {
                "port": args.port,
                "target": target,
                "append_max_us": append_us,
                "prepare_us": prepare_us,
                "compact_us": compact_us,
                "status": "pass",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
