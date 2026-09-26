#!/usr/bin/env python3
"""Verify the ESP32-S3 ADC scan probe over the native USB Serial/JTAG port."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

import serial

REPORT_PREFIX = "JH_ESP32_ADC_SCAN "
INTEGER_FIELDS = {
    "sequence", "start", "period_ns", "blocks", "blocks_ok", "fresh", "lo",
    "hi", "lo_read", "hi_read", "unscanned", "unscanned_ok", "flash",
    "flash_writes", "flash_max_us", "flash_lost", "flash_fail", "restart",
    "restart_step", "oneshot",
}
FLAG_FIELDS = {"start", "blocks_ok", "fresh", "unscanned_ok", "flash", "restart"}
EXPECTED_PERIOD_NS = 24000
MIN_BLOCKS = 20
FLASH_WRITES = 4


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                         write_timeout=5.0, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = path
    port.open()
    return port


def parse_report(line: bytes) -> dict | None:
    text = line.decode("utf-8", errors="replace").strip()
    position = text.find(REPORT_PREFIX)
    if position < 0:
        return None
    report: dict = {}
    for token in text[position + len(REPORT_PREFIX):].split():
        key, separator, value = token.partition("=")
        if not separator or not key or not value:
            return None
        report[key] = int(value) if key in INTEGER_FIELDS else value
    return report


def validate(report: dict) -> None:
    required = INTEGER_FIELDS | {"target", "board", "status"}
    if set(report) != required:
        raise RuntimeError(f"report fields differ: {sorted(report)}")
    if report["target"] != "esp32s3" or report["board"] != "waveshare-esp32-s3-zero":
        raise RuntimeError(f"target/board mismatch: {report}")
    if any(report[field] != 1 for field in FLAG_FIELDS):
        raise RuntimeError(f"a scan check failed: {report}")
    if report["period_ns"] != EXPECTED_PERIOD_NS:
        raise RuntimeError(f"frame period differs from 12 us x 2 pins: {report}")
    if report["blocks"] < MIN_BLOCKS:
        raise RuntimeError(f"too few blocks in the window: {report}")
    if not (0 <= report["lo"] < 600 and 3400 < report["hi"] <= 4095):
        raise RuntimeError(f"pull-down/pull-up levels not distinguishable: {report}")
    if not (0 <= report["lo_read"] < 600 and 3400 < report["hi_read"] <= 4095):
        raise RuntimeError(f"hal_adc_read() did not follow the scan: {report}")
    if report["unscanned"] != 0:
        raise RuntimeError(f"a pin outside the scan did not read 0: {report}")
    if report["flash_writes"] != FLASH_WRITES:
        raise RuntimeError(f"flash writes during the scan failed: {report}")
    if report["status"] != "PASS":
        raise RuntimeError(f"firmware did not report PASS: {report}")


def wait_for_report(path: str, timeout_s: float) -> dict:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    last_report: dict | None = None
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
                    if len(line) > 2048:
                        line.clear()
                    continue
                report = parse_report(bytes(line))
                line.clear()
                if report is None:
                    continue
                last_report = report
                try:
                    validate(report)
                except RuntimeError as exc:
                    last_error = exc
                    continue
                return report
        except (OSError, ValueError, serial.SerialException) as exc:
            last_error = exc
            time.sleep(0.2)
        finally:
            if port is not None:
                port.close()
    raise RuntimeError(
        f"no passing report within {timeout_s:.0f} s; last report {last_report}; "
        f"last error {last_error}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    report = wait_for_report(args.port, args.timeout)
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
