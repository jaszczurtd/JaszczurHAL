#!/usr/bin/env python3
"""Verify the ESP32-S3 Phase 2 HAL smoke contract over USB Serial/JTAG."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

import serial


ROOT = Path(__file__).resolve().parents[3]
REPORT_PREFIX = "JH_ESP32_PHASE2 "
INTEGER_FIELDS = {
    "sequence",
    "core0",
    "core1",
    "task1",
    "system",
    "sync",
    "gpio",
    "irq",
    "irq_isr",
    "adc",
    "adc_low",
    "adc_high",
    "uart",
    "i2c",
    "i2c_found",
    "spi",
    "timer",
    "timer_count",
    "timer_isr",
    "serial_rx",
    "unsupported",
    "heap",
    "temp_centi",
}
BOOLEAN_FIELDS = {
    "system",
    "sync",
    "gpio",
    "irq_isr",
    "adc",
    "uart",
    "i2c",
    "spi",
    "timer",
    "timer_isr",
    "serial_rx",
    "unsupported",
}


def load_expected_contract(target: str, board: str) -> dict[str, int | str]:
    target_desc = json.loads(
        (ROOT / "boards" / "targets" / f"{target}.json").read_text(
            encoding="utf-8"
        )
    )
    board_desc = json.loads(
        (ROOT / "boards" / "profiles" / f"{board}.json").read_text(
            encoding="utf-8"
        )
    )
    if target not in board_desc["compatibleTargets"]:
        raise RuntimeError(f"board {board!r} is not compatible with {target!r}")
    cores = int(target_desc["architecture"]["cores"])
    return {
        "target": target_desc["id"],
        "board": board_desc["id"],
        "task0_core": 0,
        "task1_core": 1 if cores > 1 else 0,
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
    text = line.decode("utf-8", errors="replace").strip()
    position = text.find(REPORT_PREFIX)
    if position < 0:
        return None

    report: dict[str, int | str] = {}
    for token in text[position + len(REPORT_PREFIX) :].split():
        key, separator, value = token.partition("=")
        if not separator or not key or not value:
            return None
        report[key] = int(value) if key in INTEGER_FIELDS else value
    return report


def validate_report(
    report: dict[str, int | str], expected: dict[str, int | str]
) -> None:
    required = {
        "sequence",
        "target",
        "board",
        "core0",
        "core1",
        "task1",
        "irq",
        "adc_low",
        "adc_high",
        "i2c_found",
        "timer_count",
        "heap",
        "temp_centi",
        "status",
        *BOOLEAN_FIELDS,
    }
    if set(report) != required:
        raise RuntimeError(
            f"Phase 2 report fields differ: expected={sorted(required)}, "
            f"actual={sorted(report)}"
        )
    if report["target"] != expected["target"] or report["board"] != expected["board"]:
        raise RuntimeError(f"target/board mismatch: {report}")
    if report["core0"] != expected["task0_core"] or report["core1"] != expected["task1_core"]:
        raise RuntimeError(f"FreeRTOS task affinity mismatch: {report}")
    if any(report[field] != 1 for field in BOOLEAN_FIELDS):
        raise RuntimeError(f"one or more HAL subsystem checks failed: {report}")
    if report["status"] != "PASS":
        raise RuntimeError(f"firmware did not report PASS: {report}")
    if report["sequence"] < 1 or report["task1"] < 1:
        raise RuntimeError(f"both application tasks must execute: {report}")
    if report["irq"] < 2 or report["timer_count"] < 3:
        raise RuntimeError(f"IRQ/timer callbacks did not repeat: {report}")
    if report["adc_low"] < 0 or report["adc_high"] > 4095:
        raise RuntimeError(f"ADC values outside 12-bit range: {report}")
    if report["adc_high"] <= report["adc_low"] + 256:
        raise RuntimeError(f"ADC pull-up/pull-down levels not distinguishable: {report}")
    if report["i2c_found"] < 0 or report["heap"] <= 0:
        raise RuntimeError(f"invalid I2C count or heap value: {report}")
    if not -4000 < report["temp_centi"] < 12500:
        raise RuntimeError(f"chip temperature outside plausible range: {report}")


def wait_for_report(path: str, timeout_s: float) -> dict[str, int | str]:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    last_report: dict[str, int | str] | None = None
    while time.monotonic() < deadline:
        port: serial.Serial | None = None
        try:
            if not Path(path).exists():
                time.sleep(0.1)
                continue
            port = open_port(path)
            next_ping = 0.0
            line = bytearray()
            while time.monotonic() < deadline:
                now = time.monotonic()
                if now >= next_ping:
                    port.write(b"PING\r")
                    next_ping = now + 0.5
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
                if report.get("status") != "PASS":
                    continue
                return report
        except (OSError, ValueError, serial.SerialException) as exc:
            last_error = exc
            time.sleep(0.1)
        finally:
            if port is not None:
                port.close()
    details = []
    if last_report is not None:
        details.append(f"last report: {last_report}")
    if last_error is not None:
        details.append(f"last serial error: {last_error}")
    suffix = f"; {'; '.join(details)}" if details else ""
    raise TimeoutError(f"Phase 2 PASS report not received from {path}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--target", default="esp32s3")
    parser.add_argument("--board", default="waveshare-esp32-s3-zero")
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    expected = load_expected_contract(args.target, args.board)
    report = wait_for_report(args.port, args.timeout)
    validate_report(report, expected)
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
