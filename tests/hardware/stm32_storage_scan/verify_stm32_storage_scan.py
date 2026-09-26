#!/usr/bin/env python3
"""Read the NUCLEO-G474RE fixture's report over the ST-LINK virtual COM port.

The fixture drives itself after upload (odd boot: checks, watchdog reset;
even boot: persistence check and a report every two seconds). The verifier
waits for the report of an even boot and judges every field."""

import argparse
import json
import re
import sys
import time

import serial

REPORT = re.compile(
    rb"JHSTM32REPORT boot=(\d+) verdict=0x([0-9a-fA-F]{8}) persist=(\d) "
    rb"wdg=(\d) keys=(\d+) cap=(\d+) gen=(\d+) scan=(\d) blocks=(\d+) "
    rb"period_ns=(\d+)"
)
MASK_RUNS = 10
KV_WRITES = 4


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout
    seen: list[str] = []
    result = None
    with serial.Serial(args.port, baudrate=115200, timeout=0.2) as port:
        port.reset_input_buffer()
        buffer = bytearray()
        while time.monotonic() < deadline and result is None:
            buffer.extend(port.read(4096))
            while b"\n" in buffer:
                raw, _, buffer = buffer.partition(b"\n")
                line = raw.strip(b"\r")
                if b"JHSTM32" in line:
                    seen.append(line.decode("utf-8", "replace"))
                match = REPORT.search(line)
                if match is None:
                    continue
                values = [int(match.group(1)), int(match.group(2), 16)] + [
                    int(match.group(i)) for i in range(3, 11)
                ]
                boot, verdict, persist, wdg, keys, cap, gen, scan, blocks, period = values
                if boot % 2 == 1:
                    continue  # the checks are still running; wait for the reset
                result = {
                    "boot": boot,
                    "verdict": f"0x{verdict:08x}",
                    "maskMatches": (verdict >> 8) & 0xFF,
                    "kvWrites": (verdict >> 16) & 0xFF,
                    "commitMaxMs": (verdict >> 24) & 0xFF,
                    "persist": persist,
                    "watchdogReset": wdg,
                    "keys": keys,
                    "keyCapacity": cap,
                    "generation": gen,
                    "scan": scan,
                    "blocks": blocks,
                    "framePeriodNs": period,
                }
                failures = []
                if verdict & 0x3F != 0x3F:
                    failures.append("verdict bits")
                if result["maskMatches"] != MASK_RUNS:
                    failures.append("masked interrupt runs")
                if result["kvWrites"] != KV_WRITES:
                    failures.append("kv writes during scan")
                if persist != 1 or wdg != 1:
                    failures.append("persistence across the watchdog reset")
                if keys < 4 or cap < 32 or gen < 2:
                    failures.append("kv stats")
                if scan != 1 or blocks == 0:
                    failures.append("scan after reset")
                result["failures"] = failures
                result["status"] = "pass" if not failures else "fail"
                break
    if result is None:
        print(json.dumps({"status": "timeout", "seen": seen[-20:]}, indent=2))
        return 2
    result["log"] = seen[-40:]
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
