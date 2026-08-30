#!/usr/bin/env python3
"""Run the private Pico 2 W + Zero 2 Classic HID hardware gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time
from typing import Callable

import serial


FIXTURE_PATH = Path(__file__).with_name("zero2_android_dinput.json")
DEFAULT_RESULT_PATH = Path(__file__).with_name("zero2_pico2w_c6_result.json")
SNAPSHOT_PREFIX = "JHBT5-SNAPSHOT "
ACK_PREFIX = "JHBT5-ACK "
ALL_CONTROLS_MASK = 0x0FFF
DISCONNECT_TIMEOUT_S = 60.0
RECONNECT_CYCLES = 5
RECONNECT_SETTLE_MS = 3_000
RECONNECT_TIMEOUT_S = 180.0
STABILITY_DURATION_MS = 30 * 60 * 1000
HAL_OK = 1

Snapshot = dict[str, object]


class Probe:
    def __init__(self, port: serial.Serial) -> None:
        self.port = port
        self.last_snapshot: Snapshot | None = None

    def _event(self, timeout_s: float) -> tuple[str, Snapshot]:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            snapshot_at = line.find(SNAPSHOT_PREFIX)
            if snapshot_at >= 0:
                payload = json.loads(line[snapshot_at + len(SNAPSHOT_PREFIX) :])
                self.last_snapshot = payload
                return "snapshot", payload
            ack_at = line.find(ACK_PREFIX)
            if ack_at >= 0:
                return "ack", json.loads(line[ack_at + len(ACK_PREFIX) :])
            if "JHBT5-ERROR" in line:
                raise RuntimeError(f"firmware error: {line}")
        raise TimeoutError("timed out waiting for firmware output")

    def snapshot(self, timeout_s: float = 3.0) -> Snapshot:
        while True:
            kind, payload = self._event(timeout_s)
            if kind == "snapshot":
                return payload

    def command(self, name: str, timeout_s: float = 5.0) -> None:
        self.port.write(f"{name}\n".encode("ascii"))
        self.port.flush()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            kind, payload = self._event(max(0.1, deadline - time.monotonic()))
            if kind != "ack" or payload.get("command") != name:
                continue
            if payload.get("status") != "HAL_OK":
                raise RuntimeError(f"{name} failed: {payload}")
            return
        raise TimeoutError(f"no acknowledgement for {name}")


def wait_for(
    probe: Probe,
    description: str,
    predicate: Callable[[Snapshot], bool],
    timeout_s: float,
    *,
    authorize_pairing: bool = False,
) -> Snapshot:
    deadline = time.monotonic() + timeout_s
    last: Snapshot | None = None
    while time.monotonic() < deadline:
        last = probe.snapshot(min(3.0, max(0.1, deadline - time.monotonic())))
        validate_live_health(last)
        if authorize_pairing and bool(last["pairingPending"]):
            probe.command("AUTHORIZE")
            continue
        if predicate(last):
            return last
    raise TimeoutError(f"timed out waiting for {description}; last={last}")


def validate_live_health(snapshot: Snapshot) -> None:
    if int(snapshot["lastStatus"]) < 0:
        raise RuntimeError(f"HAL service failed: {snapshot['lastStatus']}")
    if int(snapshot["transportStatus"]) != HAL_OK:
        raise RuntimeError(
            f"HCI transport is unhealthy: {snapshot['transportStatus']}"
        )
    for name, values in dict(snapshot["pools"]).items():
        current, high_water, capacity, failures = map(int, values)
        if current > capacity or high_water > capacity or failures != 0:
            raise RuntimeError(f"BTstack pool {name} is unhealthy: {values}")
    parser = dict(snapshot["parser"])
    expected_limits = {
        "descriptorLimit": 256,
        "queueCapacity": 16,
        "reportLimit": 32,
    }
    for name, expected in expected_limits.items():
        if int(parser[name]) != expected:
            raise RuntimeError(
                f"gamepad parser {name} changed: {parser[name]} != {expected}"
            )
    for name in (
        "descriptorsRejected",
        "droppedSnapshots",
        "reportsRejected",
        "truncatedReports",
        "unknownReportIds",
    ):
        if int(parser[name]) != 0:
            raise RuntimeError(f"gamepad parser {name} is nonzero: {parser[name]}")
    if int(parser["queueHighWater"]) > int(parser["queueCapacity"]):
        raise RuntimeError(f"gamepad parser queue is unhealthy: {parser}")


def counter(snapshot: Snapshot, name: str) -> int:
    return int(snapshot[name])


def health_counter(snapshot: Snapshot, name: str) -> int:
    transport = dict(snapshot["transport"])
    return int(transport[name]) if name in transport else counter(snapshot, name)


def connected_and_characterized(snapshot: Snapshot) -> bool:
    parser = dict(snapshot["parser"])
    return (
        bool(snapshot["connected"])
        and bool(snapshot["descriptorAvailable"])
        and bool(snapshot["descriptorMatchesCapture"])
        and int(snapshot["protocol"]) in (1, 2)
        and int(parser["descriptorsAccepted"]) == 1
    )


def disconnect_and_reconnect(
    probe: Probe, before: Snapshot, cycle: int
) -> Snapshot:
    disconnections = counter(before, "disconnections")
    connections = counter(before, "connections")
    probe.command("DISCONNECT")
    disconnected = wait_for(
        probe,
        f"disconnect in cycle {cycle}",
        lambda value: counter(value, "disconnections") > disconnections,
        DISCONNECT_TIMEOUT_S,
    )
    if (
        connected_and_characterized(disconnected)
        and counter(disconnected, "connections") > connections
    ):
        reconnected = disconnected
    else:
        print(
            f"Reconnect cycle {cycle}/{RECONNECT_CYCLES}: let the Zero 2 "
            "retry while its LED flashes. If the LED turns off, immediately "
            "press Start once to power it on normally; do not enter pairing "
            "mode.",
            flush=True,
        )
        reconnected = wait_for(
            probe,
            f"known-pad reconnect in cycle {cycle}",
            lambda value: connected_and_characterized(value)
            and counter(value, "connections") > connections,
            RECONNECT_TIMEOUT_S,
        )

    connected_ms = counter(reconnected, "connectedMs")
    return wait_for(
        probe,
        f"settled reconnect in cycle {cycle}",
        lambda value: connected_and_characterized(value)
        and counter(value, "connectedMs") >= connected_ms + RECONNECT_SETTLE_MS,
        20.0,
    )


def wait_for_stability(probe: Probe, start: Snapshot) -> Snapshot:
    connected_at = counter(start, "connectedMs")
    failure_counters = {
        name: health_counter(start, name)
        for name in (
            "authenticationFailures",
            "connectionFailures",
            "disconnections",
            "invalidReports",
            "drainBudgetHits",
        )
    }
    report_count = counter(start, "reports")
    deadline = time.monotonic() + STABILITY_DURATION_MS / 1000.0 + 60.0
    next_progress = time.monotonic()
    last = start
    while counter(last, "connectedMs") - connected_at < STABILITY_DURATION_MS:
        if time.monotonic() >= deadline:
            raise TimeoutError("30-minute connected interval was not completed")
        last = probe.snapshot(3.0)
        validate_live_health(last)
        if not connected_and_characterized(last):
            raise RuntimeError("gamepad disconnected during the stability interval")
        for name, expected in failure_counters.items():
            if health_counter(last, name) != expected:
                raise RuntimeError(
                    f"{name} changed during the stability interval: "
                    f"{expected} -> {last[name]}"
                )
        if time.monotonic() >= next_progress:
            elapsed = counter(last, "connectedMs") - connected_at
            print(
                f"stability {elapsed // 60000:02d}:"
                f"{(elapsed // 1000) % 60:02d}/30:00, "
                f"reports={last['reports']}",
                flush=True,
            )
            next_progress = time.monotonic() + 60.0
    if counter(last, "reports") <= report_count:
        raise RuntimeError("no input report arrived during the stability interval")
    return last


def pairing_method(value: int) -> str:
    methods = {
        1: "ssp-just-works",
        2: "legacy-pin-0000",
        3: "unsupported-passkey",
    }
    return methods.get(value, "none")


def protocol_name(value: int) -> str:
    return {1: "report", 2: "boot-fallback"}.get(value, "unknown")


def pool_report(snapshot: Snapshot) -> dict[str, dict[str, int]]:
    result: dict[str, dict[str, int]] = {}
    for name, values in dict(snapshot["pools"]).items():
        current, high_water, capacity, failures = map(int, values)
        result[name] = {
            "allocationFailures": failures,
            "capacity": capacity,
            "current": current,
            "highWater": high_water,
        }
    return result


def selected_counters(snapshot: Snapshot) -> dict[str, int]:
    names = (
        "acceptedIncomingConnections",
        "authenticationFailures",
        "authenticationSuccesses",
        "connectionFailures",
        "connections",
        "disconnections",
        "drainBudgetHits",
        "hidEvents",
        "identityRejections",
        "inquiryCycles",
        "inquiryResults",
        "invalidReports",
        "linkKeysStored",
        "pairingAuthorizations",
        "pairingRequests",
        "peripheralCandidates",
        "reconnectAttempts",
        "reconnectSuccesses",
        "rejectedIncomingConnections",
        "releaseAllEvents",
        "reportBytes",
        "reports",
        "rxAcl",
        "rxEvents",
        "rx",
        "sdpHidMatches",
        "pnpIdentityMatches",
        "txAcl",
        "txCommands",
        "tx",
    )
    return {name: health_counter(snapshot, name) for name in names}


def parser_report(snapshot: Snapshot) -> dict[str, int]:
    parser = dict(snapshot["parser"])
    return {name: int(value) for name, value in parser.items()}


def run_gate(probe: Probe, fixture: Snapshot) -> Snapshot:
    started_at = time.monotonic()
    ready = wait_for(
        probe,
        "controller/profile readiness",
        lambda value: bool(value["started"])
        and bool(value["controllerReady"])
        and bool(value["profileReady"]),
        20.0,
    )
    if ready["target"] != "rp2350-arm" or ready["board"] != "pico2w":
        raise RuntimeError(
            f"wrong hardware image: {ready['target']}:{ready['board']}"
        )

    probe.command("DISCOVER")
    discovery_at = time.monotonic()
    print(
        "Put the Zero 2 into Android D-input pairing now: B+Start, then hold "
        "Select until the pairing LED flashes.",
        flush=True,
    )
    connected = wait_for(
        probe,
        "the characterized Zero 2 connection",
        connected_and_characterized,
        120.0,
        authorize_pairing=True,
    )
    discovery_ms = round((time.monotonic() - discovery_at) * 1000)
    if discovery_ms > 121_000:
        raise RuntimeError(f"discovery exceeded its 120-second window: {discovery_ms}")
    if counter(connected, "pairingAuthorizations") < 1:
        raise RuntimeError("no explicit pairing authorization was recorded")
    if pairing_method(int(connected["pairingMethod"])) == "unsupported-passkey":
        raise RuntimeError("the gamepad requested unsupported passkey entry")

    expected_descriptor = dict(
        dict(fixture["reportDescriptor"])["btstackExtraction"]
    )
    expected_hash = int(expected_descriptor["fnv1a32"], 0)
    if (
        int(connected["descriptorLength"]) != int(expected_descriptor["length"])
        or int(connected["descriptorHash"]) != expected_hash
    ):
        raise RuntimeError("live HID descriptor differs from the C1 fixture")

    controls_at = time.monotonic()
    print(
        "Press A, B, X, Y, L, R, Select, Start and every D-pad direction.",
        flush=True,
    )
    connected = wait_for(
        probe,
        "all twelve characterized controls",
        lambda value: int(value["seenControlsMask"]) == ALL_CONTROLS_MASK,
        180.0,
    )
    controls_ms = round((time.monotonic() - controls_at) * 1000)

    print("Hold any gamepad control until the first reconnect starts.", flush=True)
    active = wait_for(
        probe,
        "an active control before disconnect",
        lambda value: int(value["activeControlsMask"]) != 0,
        120.0,
    )
    releases = counter(active, "releaseAllEvents")
    connections_before_reconnects = counter(active, "connections")
    reconnect_at = time.monotonic()
    connected = disconnect_and_reconnect(probe, active, 1)
    if counter(connected, "releaseAllEvents") != releases + 1:
        raise RuntimeError("disconnect did not synthesize release of active controls")
    print("Release the held control; running four more reconnects.", flush=True)
    for cycle in range(2, RECONNECT_CYCLES + 1):
        connected = disconnect_and_reconnect(probe, connected, cycle)
    reconnect_ms = round((time.monotonic() - reconnect_at) * 1000)
    if (
        counter(connected, "connections")
        < connections_before_reconnects + RECONNECT_CYCLES
    ):
        raise RuntimeError("not all requested reconnects succeeded")

    power_at = time.monotonic()
    disconnections = counter(connected, "disconnections")
    print("Switch the Zero 2 off now.", flush=True)
    wait_for(
        probe,
        "gamepad power-off disconnect",
        lambda value: not bool(value["connected"])
        and counter(value, "disconnections") > disconnections,
        120.0,
    )
    print("Switch the Zero 2 on normally; do not enter pairing mode.", flush=True)
    connected = wait_for(
        probe,
        "known-pad reconnect after power cycle",
        connected_and_characterized,
        120.0,
    )
    power_cycle_ms = round((time.monotonic() - power_at) * 1000)

    print(
        "Starting the 30-minute connected interval. Exercise the controls "
        "periodically and leave the pad powered on.",
        flush=True,
    )
    final = wait_for_stability(probe, connected)
    final["hostTimings"] = {
        "controlCaptureMs": controls_ms,
        "discoveryMs": discovery_ms,
        "powerCycleMs": power_cycle_ms,
        "reconnectCyclesMs": reconnect_ms,
        "scenarioMs": round((time.monotonic() - started_at) * 1000),
        "stabilityMs": STABILITY_DURATION_MS,
    }
    final["hostVerifierResumed"] = False
    return final


def resume_stability_gate(probe: Probe, fixture: Snapshot) -> Snapshot:
    started_at = time.monotonic()
    connected = wait_for(
        probe,
        "the uninterrupted characterized connection",
        connected_and_characterized,
        20.0,
    )
    if connected["target"] != "rp2350-arm" or connected["board"] != "pico2w":
        raise RuntimeError(
            f"wrong hardware image: {connected['target']}:{connected['board']}"
        )

    expected_descriptor = dict(
        dict(fixture["reportDescriptor"])["btstackExtraction"]
    )
    if (
        int(connected["descriptorLength"]) != int(expected_descriptor["length"])
        or int(connected["descriptorHash"])
        != int(expected_descriptor["fnv1a32"], 0)
    ):
        raise RuntimeError("live HID descriptor differs from the C1 fixture")

    minimum_counters = {
        "acceptedIncomingConnections": RECONNECT_CYCLES + 1,
        "connections": RECONNECT_CYCLES + 2,
        "disconnections": RECONNECT_CYCLES + 1,
        "pairingAuthorizations": 1,
        "releaseAllEvents": 1,
    }
    for name, minimum in minimum_counters.items():
        if counter(connected, name) < minimum:
            raise RuntimeError(
                f"cannot resume: {name} is {connected[name]}, expected at least "
                f"{minimum}"
            )
    if int(connected["seenControlsMask"]) != ALL_CONTROLS_MASK:
        raise RuntimeError("cannot resume: not all twelve controls were captured")
    for name in (
        "authenticationFailures",
        "invalidReports",
        "drainBudgetHits",
    ):
        if health_counter(connected, name) != 0:
            raise RuntimeError(f"cannot resume after nonzero {name}")

    print(
        "Resuming the uninterrupted C6 scenario at the 30-minute connected "
        "interval. Exercise the controls periodically and leave the pad "
        "powered on.",
        flush=True,
    )
    final = wait_for_stability(probe, connected)
    final["hostTimings"] = {
        "preStabilityConnectedMs": counter(connected, "connectedMs"),
        "resumeScenarioMs": round((time.monotonic() - started_at) * 1000),
        "stabilityMs": STABILITY_DURATION_MS,
    }
    final["hostVerifierResumed"] = True
    return final


def build_report(snapshot: Snapshot) -> Snapshot:
    timings = dict(snapshot.pop("hostTimings"))
    host_verifier_resumed = bool(snapshot.pop("hostVerifierResumed"))
    return {
        "schemaVersion": 1,
        "capture": FIXTURE_PATH.name,
        "firmware": {
            "btstack": snapshot["btstackVersion"],
            "gamepad": "unavailable",
            "picoSdk": snapshot["picoSdkVersion"],
        },
        "hardware": {
            "board": snapshot["board"],
            "target": snapshot["target"],
        },
        "link": {
            "allControlsMask": f"0x{int(snapshot['seenControlsMask']):04x}",
            "connectedMs": counter(snapshot, "connectedMs"),
            "descriptorFNV1a32": f"0x{int(snapshot['descriptorHash']):08x}",
            "descriptorLength": int(snapshot["descriptorLength"]),
            "descriptorLengthHighWater": int(
                snapshot["descriptorLengthHighWater"]
            ),
            "pairingMethod": pairing_method(int(snapshot["pairingMethod"])),
            "protocol": protocol_name(int(snapshot["protocol"])),
            "reportLengthHighWater": int(snapshot["reportLengthHighWater"]),
        },
        "scenario": {
            "hostVerifierResumed": host_verifier_resumed,
            "powerCycleReconnect": True,
            "reconnectCycles": RECONNECT_CYCLES,
            "releaseActiveInputOnDisconnect": True,
            "stabilityRequiredMs": STABILITY_DURATION_MS,
            "timings": timings,
        },
        "resources": pool_report(snapshot),
        "parser": parser_report(snapshot),
        "counters": selected_counters(snapshot),
        "result": "pass",
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Pico USB CDC device")
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_RESULT_PATH,
        help="sanitized deterministic result path",
    )
    parser.add_argument(
        "--resume-stability",
        action="store_true",
        help=(
            "resume only the 30-minute interval after strict validation of "
            "the uninterrupted firmware counters"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    descriptor = dict(fixture["reportDescriptor"])
    if "fnv1a32" not in descriptor:
        raise RuntimeError("the C1 fixture does not contain its descriptor hash")

    with serial.Serial(
        args.port,
        baudrate=115200,
        timeout=0.2,
        write_timeout=5.0,
        exclusive=True,
    ) as port:
        port.dtr = True
        port.rts = False
        port.reset_input_buffer()
        probe = Probe(port)
        final = (
            resume_stability_gate(probe, fixture)
            if args.resume_stability
            else run_gate(probe, fixture)
        )

    report = build_report(final)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, TimeoutError, serial.SerialException) as exc:
        print(f"C6 FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
