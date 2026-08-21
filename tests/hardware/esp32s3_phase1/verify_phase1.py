#!/usr/bin/env python3
"""Verify the ESP32-S3 Phase 1 board contract over USB Serial/JTAG."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import time

import serial


ROOT = Path(__file__).resolve().parents[3]
REPORT_PATTERN = re.compile(
    r"JH_ESP32_PHASE1 phase=([^ ]+) sequence=(\d+) "
    r"target=([^ ]+) board=([^ ]+) model_match=(\d+) "
    r"cores=(\d+) expected_cores=(\d+) flash=(\d+) expected_flash=(\d+) "
    r"psram_initialized=(\d+) psram=(\d+) expected_psram=(\d+) "
    r"status=(PASS|FAIL)"
)


def load_expected_contract(target: str, board: str) -> dict[str, int | str]:
    target_path = ROOT / "boards" / "targets" / f"{target}.json"
    board_path = ROOT / "boards" / "profiles" / f"{board}.json"
    target_desc = json.loads(target_path.read_text(encoding="utf-8"))
    board_desc = json.loads(board_path.read_text(encoding="utf-8"))
    if target not in board_desc["compatibleTargets"]:
        raise RuntimeError(f"board {board!r} is not compatible with {target!r}")
    return {
        "target": target_desc["id"],
        "board": board_desc["id"],
        "cores": target_desc["architecture"]["cores"],
        "flash": board_desc["memory"]["flash"]["expectedBytes"],
        "psram": board_desc["memory"]["psram"]["sizeBytes"],
    }


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        port=None,
        baudrate=115200,
        timeout=0.1,
        write_timeout=5.0,
        exclusive=True,
    )
    port.dtr = False
    port.rts = False
    port.port = path
    port.open()
    return port


def parse_report(line: bytes) -> dict[str, int | str] | None:
    match = REPORT_PATTERN.search(line.decode("utf-8", errors="replace"))
    if match is None:
        return None
    keys = (
        "phase",
        "sequence",
        "target",
        "board",
        "model_match",
        "cores",
        "expected_cores",
        "flash",
        "expected_flash",
        "psram_initialized",
        "psram",
        "expected_psram",
        "status",
    )
    report: dict[str, int | str] = dict(zip(keys, match.groups()))
    for key in ("sequence", *keys[4:-1]):
        report[key] = int(report[key])
    return report


def validate_report(
    report: dict[str, int | str], expected: dict[str, int | str]
) -> None:
    wanted = {
        "phase": "task0",
        "sequence": report["sequence"],
        "target": expected["target"],
        "board": expected["board"],
        "model_match": 1,
        "cores": expected["cores"],
        "expected_cores": expected["cores"],
        "flash": expected["flash"],
        "expected_flash": expected["flash"],
        "psram_initialized": 1,
        "psram": expected["psram"],
        "expected_psram": expected["psram"],
        "status": "PASS",
    }
    if not isinstance(report["sequence"], int) or report["sequence"] < 1:
        raise RuntimeError(f"invalid task0 heartbeat sequence: {report}")
    if report != wanted:
        raise RuntimeError(
            f"hardware contract mismatch: expected={wanted}, actual={report}"
        )


def is_task0_heartbeat(report: dict[str, int | str]) -> bool:
    """Return true only for a heartbeat emitted by the created task0."""
    return (
        report.get("phase") == "task0"
        and isinstance(report.get("sequence"), int)
        and int(report["sequence"]) >= 1
    )


def wait_for_report(path: str, timeout_s: float) -> dict[str, int | str]:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        port: serial.Serial | None = None
        try:
            if not Path(path).exists():
                time.sleep(0.1)
                continue
            port = open_port(path)
            line = bytearray()
            while time.monotonic() < deadline:
                chunk = port.read(1)
                if not chunk:
                    continue
                line.extend(chunk)
                if not line.endswith(b"\n"):
                    continue
                report = parse_report(bytes(line))
                if report is not None and is_task0_heartbeat(report):
                    return report
                line.clear()
        except (OSError, serial.SerialException) as exc:
            last_error = exc
            time.sleep(0.1)
        finally:
            if port is not None:
                port.close()
    suffix = f"; last serial error: {last_error}" if last_error else ""
    raise TimeoutError(f"Phase 1 report not received from {path}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--target", default="esp32s3")
    parser.add_argument("--board", default="waveshare-esp32-s3-zero")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    expected = load_expected_contract(args.target, args.board)
    report = wait_for_report(args.port, args.timeout)
    validate_report(report, expected)
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
