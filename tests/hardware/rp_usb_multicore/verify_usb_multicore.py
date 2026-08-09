#!/usr/bin/env python3

import argparse
import json
import re
import time

import serial


RECORD_PATTERN = re.compile(
    rb"^JHUSB2 core=(\d) seq=(\d{6}) token=([0-9a-f]{8})\n$"
)
RESULT_PATTERN = re.compile(
    rb"^JHUSB2-RESULT target=([^ ]+) board=([^ ]+) runtime=([^ ]+) "
    rb"requested=(\d+) core0=(\d+) core1=(\d+) status0=(-?\d+) "
    rb"status1=(-?\d+) observed0=(\d+) observed1=(\d+)\n$"
)


def record_token(producer: int, sequence: int) -> int:
    return (
        sequence * 2654435761
        ^ producer * 0x9E3779B9
        ^ 0x4A485532
    ) & 0xFFFFFFFF


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


def read_line(port: serial.Serial, deadline: float) -> bytes:
    line = bytearray()
    while not line.endswith(b"\n"):
        if time.monotonic() >= deadline:
            raise TimeoutError(f"incomplete multicore USB line: {line!r}")
        chunk = port.read(1)
        if chunk:
            line.extend(chunk)
    return bytes(line)


def validate_identity(
    result: dict[str, int | str], target: str, board: str, runtime: str
) -> None:
    expected = {"target": target, "board": board, "runtime": runtime}
    actual = {key: result[key] for key in expected}
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
    parser.add_argument("--records", type=int, default=4096)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    expected_board = "pico" if args.target == "rp2040" else "pico2"
    if args.board != expected_board:
        parser.error(f"{args.target} requires --board {expected_board}")
    if args.records <= 0:
        parser.error("--records must be positive")

    seen = [set(), set()]
    started = time.monotonic()
    deadline = started + args.timeout
    result: dict[str, int | str] | None = None

    with open_port(args.port) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        port.write(b"G")
        port.flush()

        while result is None:
            line = read_line(port, deadline)
            match = RECORD_PATTERN.fullmatch(line)
            if match is not None:
                producer = int(match.group(1))
                sequence = int(match.group(2))
                token = int(match.group(3), 16)
                if producer not in (0, 1):
                    raise RuntimeError(f"invalid producer in record: {line!r}")
                if sequence >= args.records:
                    raise RuntimeError(f"out-of-range sequence: {line!r}")
                if sequence in seen[producer]:
                    raise RuntimeError(f"duplicate record: {line!r}")
                expected_token = record_token(producer, sequence)
                if token != expected_token:
                    raise RuntimeError(
                        f"record token mismatch: expected={expected_token:08x}, "
                        f"line={line!r}"
                    )
                seen[producer].add(sequence)
                continue

            match = RESULT_PATTERN.fullmatch(line)
            if match is None:
                raise RuntimeError(
                    f"malformed or interleaved multicore USB record: {line!r}"
                )
            keys = (
                "target",
                "board",
                "runtime",
                "requested",
                "core0",
                "core1",
                "status0",
                "status1",
                "observed0",
                "observed1",
            )
            values: list[int | str] = [
                match.group(1).decode(),
                match.group(2).decode(),
                match.group(3).decode(),
                *(int(value) for value in match.groups()[3:]),
            ]
            result = dict(zip(keys, values))

    validate_identity(result, args.target, args.board, args.runtime)
    if result["requested"] != args.records:
        raise RuntimeError(f"firmware record count mismatch: {result}")
    if result["status0"] != 1 or result["status1"] != 1:
        raise RuntimeError(f"producer failed: {result}")
    if result["core0"] != args.records or result["core1"] != args.records:
        raise RuntimeError(f"incomplete firmware counters: {result}")
    if result["observed0"] != 0 or result["observed1"] != 1:
        raise RuntimeError(f"invalid producer affinity: {result}")

    expected_sequences = set(range(args.records))
    for producer in (0, 1):
        if seen[producer] != expected_sequences:
            missing = sorted(expected_sequences - seen[producer])[:8]
            raise RuntimeError(
                f"producer {producer} missing records: {missing}"
            )

    elapsed = time.monotonic() - started
    output = {
        "port": args.port,
        "target": args.target,
        "board": args.board,
        "runtime": args.runtime,
        "recordsPerCore": args.records,
        "totalRecords": args.records * 2,
        "elapsedSeconds": round(elapsed, 3),
        "recordsPerSecond": round((args.records * 2) / elapsed),
        "firmware": result,
        "status": "pass",
    }
    print(json.dumps(output, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
