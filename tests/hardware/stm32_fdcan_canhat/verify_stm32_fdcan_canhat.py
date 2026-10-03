#!/usr/bin/env python3
"""Read the CAN-FD HAT fixture's report over the ST-LINK virtual COM port.

The fixture runs its checks once after reset and then repeats the report
every two seconds: a begin line with the totals, one line per check
(``JHCANHAT <name> <PASS|FAIL|SKIP> <detail>``) and an end line. The ST-LINK
may still hold reports of the image that ran before an upload and hands them
over in one burst, so the verifier first waits for QUIET_S of silence, then
takes the next complete report and fails on any FAIL line. The shared-bus
check is skipped when CN5-CN7 are not wired together; ``--require-bus``
turns that skip into a failure for runs where the bus is wired."""

import argparse
import json
import re
import sys
import time

import serial

BEGIN = re.compile(rb"JHCANHAT begin count=(\d+) pass=(\d+) fail=(\d+) skip=(\d+)")
CHECK = re.compile(rb"JHCANHAT (\S+) (PASS|FAIL|SKIP) ?(.*)")
END = b"JHCANHAT end"
QUIET_S = 0.5  # the live image reports every 2 s; a stale burst has no gap


def judge(count: int, checks: list[dict], require_bus: bool) -> list[str]:
    failures = []
    if len(checks) != count:
        failures.append(f"report has {len(checks)} checks, begin line says {count}")
    failures += [f"{c['name']}: {c['detail']}" for c in checks if c["result"] == "FAIL"]
    bus = [c for c in checks if c["name"] == "shared_bus"]
    if require_bus and (not bus or bus[0]["result"] != "PASS"):
        failures.append("shared_bus did not pass and --require-bus is set")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--require-bus", action="store_true")
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout
    seen: list[str] = []
    result = None
    count = None
    checks: list[dict] = []
    with serial.Serial(args.port, baudrate=115200, timeout=0.1) as port:
        port.reset_input_buffer()
        last_byte = time.monotonic()
        while time.monotonic() < deadline:
            if port.read(4096):
                last_byte = time.monotonic()
            elif time.monotonic() - last_byte >= QUIET_S:
                break
        buffer = bytearray()
        while time.monotonic() < deadline and result is None:
            buffer.extend(port.read(4096))
            while b"\n" in buffer and result is None:
                raw, _, buffer = buffer.partition(b"\n")
                line = raw.strip(b"\r")
                if b"JHCANHAT" not in line:
                    continue
                seen.append(line.decode("utf-8", "replace"))
                begin = BEGIN.search(line)
                if begin is not None:
                    count = int(begin.group(1))
                    checks = []
                    continue
                if count is None:
                    continue  # joined in the middle of a report
                if line.endswith(END):
                    failures = judge(count, checks, args.require_bus)
                    result = {
                        "status": "pass" if not failures else "fail",
                        "pass": sum(c["result"] == "PASS" for c in checks),
                        "fail": sum(c["result"] == "FAIL" for c in checks),
                        "skip": sum(c["result"] == "SKIP" for c in checks),
                        "failures": failures,
                        "checks": checks,
                    }
                    continue
                match = CHECK.search(line)
                if match is not None:
                    checks.append(
                        {
                            "name": match.group(1).decode(),
                            "result": match.group(2).decode(),
                            "detail": match.group(3).decode("utf-8", "replace").strip(),
                        }
                    )
    if result is None:
        print(json.dumps({"status": "timeout", "seen": seen[-20:]}, indent=2))
        return 2
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
