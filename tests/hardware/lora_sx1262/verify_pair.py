#!/usr/bin/env python3
"""Serial-log verifier for the two-device SX1262 raw LoRa hardware gate."""

from __future__ import annotations

import argparse
import json
import re
import time

import serial


PING_TX = re.compile(r"TX ping sequence=(\d+)")
PING_RX = re.compile(r"RX 'JHLORA1 PING (\d+) .*RSSI=(-?\d+) dBm SNR=(-?\d+) dB")
REPLY_TX = re.compile(r"TX reply sequence=(\d+)")
REPLY_RX = re.compile(r"RX 'JHLORA1 PONG (\d+) .*RSSI=(-?\d+) dBm SNR=(-?\d+) dB")
SLEEP_WAKE = re.compile(
    r"Sleep/wake sequence=(\d+) sleep=(HAL_[A-Z]+) wake=(HAL_[A-Z]+)"
)
REINITIALIZE = re.compile(r"Reinitialize sequence=(\d+) status=(HAL_[A-Z]+)")
ASYNC_READY = re.compile(r"Async DIO1 event loop enabled")
ASYNC_DIAGNOSTICS = re.compile(
    r"Async diagnostics sequence=(\d+) irq=(\d+) callbacks=(\d+) cancelled=(\d+)"
)


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(path, 115200, timeout=0.02, exclusive=True)
    port.dtr = True
    port.reset_input_buffer()
    return port


def decoded_line(port: serial.Serial) -> str:
    return port.readline().decode("utf-8", errors="replace").strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--initiator-port", required=True)
    parser.add_argument("--responder-port", required=True)
    parser.add_argument("--duration", type=float, default=75.0)
    parser.add_argument("--minimum-packets", type=int, default=5)
    args = parser.parse_args()
    if args.initiator_port == args.responder_port:
        parser.error("initiator and responder ports must be different")

    ping_tx: set[int] = set()
    ping_rx: set[int] = set()
    reply_tx: set[int] = set()
    reply_rx: set[int] = set()
    rssi_values: list[int] = []
    snr_values: list[int] = []
    sleep_wake_ok = False
    reinitialize_ok = False
    async_ready: set[str] = set()
    async_diagnostics_ok = False

    with open_port(args.initiator_port) as initiator, open_port(
        args.responder_port
    ) as responder:
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            for role, port in (("initiator", initiator), ("responder", responder)):
                line = decoded_line(port)
                if not line:
                    continue
                print(f"[{role}] {line}")
                match = PING_TX.search(line)
                if role == "initiator" and match:
                    ping_tx.add(int(match.group(1)))
                    async_ready.add(role)
                match = PING_RX.search(line)
                if role == "responder" and match:
                    ping_rx.add(int(match.group(1)))
                    rssi_values.append(int(match.group(2)))
                    snr_values.append(int(match.group(3)))
                match = REPLY_TX.search(line)
                if role == "responder" and match:
                    reply_tx.add(int(match.group(1)))
                    async_ready.add(role)
                match = REPLY_RX.search(line)
                if role == "initiator" and match:
                    reply_rx.add(int(match.group(1)))
                    rssi_values.append(int(match.group(2)))
                    snr_values.append(int(match.group(3)))
                match = SLEEP_WAKE.search(line)
                if role == "responder" and match:
                    sleep_wake_ok = (
                        match.group(2) == "HAL_OK" and match.group(3) == "HAL_OK"
                    )
                match = REINITIALIZE.search(line)
                if role == "responder" and match:
                    reinitialize_ok = match.group(2) == "HAL_OK"
                if ASYNC_READY.search(line):
                    async_ready.add(role)
                match = ASYNC_DIAGNOSTICS.search(line)
                if role == "responder" and match:
                    async_diagnostics_ok = (
                        int(match.group(2)) > 0
                        and int(match.group(3)) > 0
                        and int(match.group(4)) > 0
                    )

    matched = ping_tx & ping_rx & reply_tx & reply_rx
    failures: list[str] = []
    if len(matched) < args.minimum_packets:
        failures.append(
            f"only {len(matched)} complete exchanges; expected {args.minimum_packets}"
        )
    if not rssi_values or not snr_values:
        failures.append("no RSSI/SNR packet metadata observed")
    if not sleep_wake_ok:
        failures.append("HAL_OK sleep/wake probe was not observed")
    if not reinitialize_ok:
        failures.append("HAL_OK radio reinitialization was not observed")
    if async_ready != {"initiator", "responder"}:
        failures.append("asynchronous DIO1 event loop was not enabled on both radios")
    if not async_diagnostics_ok:
        failures.append("IRQ/callback/cancel diagnostics were not observed")

    result = {
        "status": "fail" if failures else "pass",
        "initiatorPort": args.initiator_port,
        "responderPort": args.responder_port,
        "completeSequences": sorted(matched),
        "completeExchanges": len(matched),
        "rssiDbm": {
            "min": min(rssi_values) if rssi_values else None,
            "max": max(rssi_values) if rssi_values else None,
        },
        "snrDb": {
            "min": min(snr_values) if snr_values else None,
            "max": max(snr_values) if snr_values else None,
        },
        "sleepWake": sleep_wake_ok,
        "reinitialize": reinitialize_ok,
        "asyncReady": sorted(async_ready),
        "asyncDiagnostics": async_diagnostics_ok,
        "failures": failures,
    }
    print(json.dumps(result, indent=2))
    if failures:
        raise RuntimeError("; ".join(failures))
    print("JHLORA1 HOST PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
