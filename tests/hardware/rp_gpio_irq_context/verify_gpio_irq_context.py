#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import re
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from vscode.runtime.serial_io import read_line  # noqa: E402


RESULT_PATTERN = re.compile(
    rb"^JHGPIOIRQ1 checks=(\d+) failed=0x([0-9a-f]+) attach=(-?\d+) "
    rb"owner=(-?\d+) owner_core=(\d+) detached=(-?\d+) "
    rb"a=(\d+)/(\d+) b=(\d+)/(\d+) swap=(\d+)/(\d+) plain=(\d+)\n$"
)

KEYS = (
    "checks",
    "failed",
    "attach",
    "owner",
    "owner_core",
    "detached",
    "a_hits",
    "a_pin",
    "b_hits",
    "b_pin",
    "swap_hits",
    "swap_pin",
    "plain_hits",
)

EXPECTED_CHECKS = 16
PIN_CTX_A = 2
PIN_CTX_B = 3


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path, baudrate=115200, timeout=0.1, write_timeout=5.0, exclusive=True
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def run_probe(port: serial.Serial, timeout_s: float) -> dict:
    port.reset_input_buffer()
    port.write(b"T")
    port.flush()
    line = read_line(port, time.monotonic() + timeout_s)
    match = RESULT_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid response: {line!r}")
    values = [int(group, 16 if index == 1 else 10)
              for index, group in enumerate(match.groups())]
    return dict(zip(KEYS, values))


def evaluate(result: dict) -> list[str]:
    """Judge the run from the firmware's own verdict.

    The statuses in the line are diagnostics: the firmware compares them
    against the hal_status_t enum it was built with, so repeating those values
    here would only add a second place to keep them correct.
    """
    problems = []
    if result["checks"] != EXPECTED_CHECKS:
        problems.append(
            f"ran {result['checks']} checks, expected {EXPECTED_CHECKS}"
        )
    if result["failed"] != 0:
        problems.append(f"failed checks bitmask 0x{result['failed']:08x}")
    if result["a_pin"] != PIN_CTX_A or result["b_pin"] != PIN_CTX_B:
        problems.append(
            f"handler saw pins {result['a_pin']}/{result['b_pin']}, "
            f"expected {PIN_CTX_A}/{PIN_CTX_B}"
        )
    for name in ("a_hits", "b_hits", "swap_hits", "plain_hits"):
        if result[name] == 0:
            problems.append(f"{name} stayed at zero: no edge reached the handler")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify context-aware GPIO interrupts on RP hardware"
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    with open_port(args.port) as port:
        result = run_probe(port, args.timeout)

    problems = evaluate(result)
    if args.json is not None:
        args.json.write_text(
            json.dumps({"result": result, "problems": problems}, indent=2)
        )

    print(" ".join(f"{key}={value}" for key, value in result.items()))
    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    print("PASS" if not problems else "FAIL")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
