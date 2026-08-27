#!/usr/bin/env python3
"""Verify fragmented command-router round trips between two LoRa devices."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import re
import time
from typing import Iterable


REQUEST = re.compile(
    r"JHCMD1 REQUEST id=(\d+) len=(\d+) crc=([0-9A-Fa-f]{8}) fragments=(\d+)"
)
HANDLE = re.compile(
    r"JHCMD1 HANDLE id=(\d+) len=(\d+) crc=([0-9A-Fa-f]{8}) "
    r"fragments=(\d+) status=(HAL_[A-Z0-9_]+) call=(\d+) "
    r"source=([A-Z0-9_]+) peer=0x([0-9A-Fa-f]{4,16}) "
    r"session=0x([0-9A-Fa-f]{1,16}) security=0x([0-9A-Fa-f]{8}) "
    r"rssi=(-?\d+) snr=(-?\d+)"
)
RESPONSE = re.compile(
    r"JHCMD1 RESPONSE id=(\d+) len=(\d+) crc=([0-9A-Fa-f]{8}) "
    r"fragments=(\d+) status=(HAL_[A-Z0-9_]+) match=([01]) "
    r"source=0x([0-9A-Fa-f]{4}) session=0x([0-9A-Fa-f]{1,8}) "
    r"security=0x([0-9A-Fa-f]{8}) rssi=(-?\d+) snr=(-?\d+)"
)
READY = re.compile(
    r"JHCMD1 READY role=(initiator|responder) "
    r"local=0x([0-9A-Fa-f]{4}) peer=0x([0-9A-Fa-f]{4}) "
    r"payload=(\d+) sources=(LORA_LINK\|BLE_STREAM) "
    r"session=0x([0-9A-Fa-f]{1,8})"
)
FAULT = re.compile(r"JHCMD1 (ERROR|TIMEOUT)\b.*")

EXPECTED_PAYLOAD_LENGTH = 500
EXPECTED_FRAGMENT_COUNT = 3


@dataclass(frozen=True)
class RequestRecord:
    length: int
    crc: str
    fragments: int


@dataclass(frozen=True)
class HandlerRecord:
    length: int
    crc: str
    fragments: int
    status: str
    call: int
    source: str
    peer: int
    session: int
    security: int
    rssi: int
    snr: int


@dataclass(frozen=True)
class ResponseRecord:
    length: int
    crc: str
    fragments: int
    status: str
    match: int
    source: int
    session: int
    security: int
    rssi: int
    snr: int


class Observations:
    def __init__(self) -> None:
        self.requests: dict[int, RequestRecord] = {}
        self.handlers: dict[int, HandlerRecord] = {}
        self.responses: dict[int, ResponseRecord] = {}
        self.record_failures: list[str] = []
        self.faults: list[str] = []
        self.handler_call_sequence: list[int] = []
        self.ready_streams: set[str] = set()
        self.ready_sessions: dict[str, int] = {}

    def _failure(self, message: str) -> None:
        if message not in self.record_failures:
            self.record_failures.append(message)

    def _insert(self, kind: str, request_id: int, record: object) -> None:
        records = {
            "request": self.requests,
            "handler": self.handlers,
            "response": self.responses,
        }[kind]
        if request_id in records:
            qualifier = "duplicate" if records[request_id] == record else "conflicting"
            self._failure(f"{qualifier} {kind} record for id {request_id}")
            return
        records[request_id] = record

    def consume(self, role: str, line: str) -> None:
        match = FAULT.search(line)
        if match:
            fault = f"{role}: {match.group(0)}"
            if fault not in self.faults:
                self.faults.append(fault)
            return

        match = READY.search(line)
        if match:
            declared_role = match.group(1)
            local = int(match.group(2), 16)
            peer = int(match.group(3), 16)
            payload = int(match.group(4))
            session = int(match.group(6), 16)
            expected_addresses = {
                "initiator": (0x1001, 0x1002),
                "responder": (0x1002, 0x1001),
            }
            valid = True
            if declared_role != role:
                self._failure(
                    f"{role} stream declares READY role {declared_role}"
                )
                valid = False
            if (local, peer) != expected_addresses[declared_role]:
                self._failure(
                    f"{declared_role} READY addresses are "
                    f"0x{local:04X}->0x{peer:04X}"
                )
                valid = False
            if payload != EXPECTED_PAYLOAD_LENGTH:
                self._failure(
                    f"{declared_role} READY payload is {payload}, expected "
                    f"{EXPECTED_PAYLOAD_LENGTH}"
                )
                valid = False
            if session == 0:
                self._failure(f"{declared_role} READY session is zero")
                valid = False
            previous_session = self.ready_sessions.get(declared_role)
            if previous_session is not None and previous_session != session:
                self._failure(f"{declared_role} READY session changed")
                valid = False
            if valid:
                self.ready_streams.add(role)
                self.ready_sessions[declared_role] = session
            return

        match = REQUEST.search(line)
        if role == "initiator" and match:
            self._insert(
                "request",
                int(match.group(1)),
                RequestRecord(
                    length=int(match.group(2)),
                    crc=match.group(3).upper(),
                    fragments=int(match.group(4)),
                ),
            )
            return

        match = HANDLE.search(line)
        if role == "responder" and match:
            handler_call = int(match.group(6))
            self.handler_call_sequence.append(handler_call)
            self._insert(
                "handler",
                int(match.group(1)),
                HandlerRecord(
                    length=int(match.group(2)),
                    crc=match.group(3).upper(),
                    fragments=int(match.group(4)),
                    status=match.group(5),
                    call=handler_call,
                    source=match.group(7),
                    peer=int(match.group(8), 16),
                    session=int(match.group(9), 16),
                    security=int(match.group(10), 16),
                    rssi=int(match.group(11)),
                    snr=int(match.group(12)),
                ),
            )
            return

        match = RESPONSE.search(line)
        if role == "initiator" and match:
            self._insert(
                "response",
                int(match.group(1)),
                ResponseRecord(
                    length=int(match.group(2)),
                    crc=match.group(3).upper(),
                    fragments=int(match.group(4)),
                    status=match.group(5),
                    match=int(match.group(6)),
                    source=int(match.group(7), 16),
                    session=int(match.group(8), 16),
                    security=int(match.group(9), 16),
                    rssi=int(match.group(10)),
                    snr=int(match.group(11)),
                ),
            )


def open_port(path: str):
    try:
        import serial
    except ImportError as error:
        raise RuntimeError(
            "pyserial is required when serial ports are selected"
        ) from error

    port = serial.Serial(path, 115200, timeout=0.02, exclusive=True)
    port.dtr = True
    port.reset_input_buffer()
    return port


def decoded_line(port: object) -> str:
    return port.readline().decode("utf-8", errors="replace").strip()


def consume_lines(
    observations: Observations, role: str, lines: Iterable[str]
) -> None:
    for raw_line in lines:
        line = raw_line.strip()
        if not line:
            continue
        print(f"[{role}] {line}")
        observations.consume(role, line)


def capture_serial(
    observations: Observations,
    initiator_path: str,
    responder_path: str,
    duration: float,
) -> None:
    with open_port(initiator_path) as initiator, open_port(
        responder_path
    ) as responder:
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            for role, port in (
                ("initiator", initiator),
                ("responder", responder),
            ):
                line = decoded_line(port)
                if line:
                    print(f"[{role}] {line}")
                    observations.consume(role, line)


def capture_logs(
    observations: Observations, initiator_path: Path, responder_path: Path
) -> None:
    with initiator_path.open(encoding="utf-8", errors="replace") as initiator:
        consume_lines(observations, "initiator", initiator)
    with responder_path.open(encoding="utf-8", errors="replace") as responder:
        consume_lines(observations, "responder", responder)


def validate_transaction(
    request_id: int,
    request: RequestRecord,
    handler: HandlerRecord,
    response: ResponseRecord,
    ready_sessions: dict[str, int],
) -> list[str]:
    failures: list[str] = []
    if request_id == 0:
        failures.append("request id is zero")
    lengths = (request.length, handler.length, response.length)
    if any(length != EXPECTED_PAYLOAD_LENGTH for length in lengths):
        failures.append(
            f"lengths {lengths} do not all equal {EXPECTED_PAYLOAD_LENGTH}"
        )
    crcs = (request.crc, handler.crc, response.crc)
    if len(set(crcs)) != 1:
        failures.append(f"CRC values differ: {crcs}")
    fragments = (request.fragments, handler.fragments, response.fragments)
    if any(count != EXPECTED_FRAGMENT_COUNT for count in fragments):
        failures.append(
            f"fragment counts {fragments} do not all equal "
            f"{EXPECTED_FRAGMENT_COUNT}"
        )
    if handler.status != "HAL_OK":
        failures.append(f"handler status is {handler.status}")
    if handler.source != "LORA_LINK":
        failures.append(f"handler source is {handler.source}")
    if handler.peer != 0x1001:
        failures.append(f"handler peer is 0x{handler.peer:04X}")
    if handler.session == 0:
        failures.append("handler session is zero")
    elif (
        "initiator" in ready_sessions
        and handler.session != ready_sessions["initiator"]
    ):
        failures.append("handler session differs from initiator READY")
    if handler.security != 0:
        failures.append(f"handler security flags are 0x{handler.security:08X}")
    if not -164 <= handler.rssi < 0:
        failures.append(f"handler RSSI is outside the LoRa range: {handler.rssi}")
    if not -32 <= handler.snr <= 31:
        failures.append(f"handler SNR is outside the LoRa range: {handler.snr}")
    if response.status != "HAL_OK":
        failures.append(f"response status is {response.status}")
    if response.match != 1:
        failures.append("firmware response integrity check failed")
    if response.source != 0x1002:
        failures.append(f"response source is 0x{response.source:04X}")
    if response.session == 0:
        failures.append("response session is zero")
    elif (
        "responder" in ready_sessions
        and response.session != ready_sessions["responder"]
    ):
        failures.append("response session differs from responder READY")
    if response.security != 0:
        failures.append(
            f"response security flags are 0x{response.security:08X}"
        )
    if not -164 <= response.rssi < 0:
        failures.append(
            f"response RSSI is outside the LoRa range: {response.rssi}"
        )
    if not -32 <= response.snr <= 31:
        failures.append(f"response SNR is outside the LoRa range: {response.snr}")
    return failures


def evaluate(
    observations: Observations, minimum_transactions: int
) -> tuple[dict[str, object], list[str]]:
    failures = list(observations.record_failures)
    failures.extend(f"firmware reported {fault}" for fault in observations.faults)
    if observations.ready_streams != {"initiator", "responder"}:
        failures.append("valid READY records for both declared roles were not observed")
    handler_calls = observations.handler_call_sequence
    if any(
        later <= earlier
        for earlier, later in zip(handler_calls, handler_calls[1:])
    ):
        failures.append("handler call identifiers are not strictly increasing")

    matched_ids = sorted(
        set(observations.requests)
        & set(observations.handlers)
        & set(observations.responses)
    )
    valid_ids: list[int] = []
    invalid: dict[str, list[str]] = {}
    transactions: dict[str, object] = {}
    for request_id in matched_ids:
        request = observations.requests[request_id]
        handler = observations.handlers[request_id]
        response = observations.responses[request_id]
        transaction_failures = validate_transaction(
            request_id, request, handler, response, observations.ready_sessions
        )
        transactions[str(request_id)] = {
            "request": asdict(request),
            "handler": asdict(handler),
            "response": asdict(response),
        }
        if transaction_failures:
            invalid[str(request_id)] = transaction_failures
        else:
            valid_ids.append(request_id)

    if len(valid_ids) < minimum_transactions:
        failures.append(
            f"only {len(valid_ids)} valid matched transactions; "
            f"expected at least {minimum_transactions}"
        )
    if invalid:
        failures.append(f"{len(invalid)} matched transaction(s) failed validation")

    result: dict[str, object] = {
        "status": "fail" if failures else "pass",
        "minimumTransactions": minimum_transactions,
        "requestIds": sorted(observations.requests),
        "handlerIds": sorted(observations.handlers),
        "responseIds": sorted(observations.responses),
        "matchedIds": matched_ids,
        "validIds": valid_ids,
        "handlerCalls": sorted(handler_calls),
        "handlerCallSequence": handler_calls,
        "readyRoles": sorted(observations.ready_streams),
        "readySessions": observations.ready_sessions,
        "firmwareFaults": observations.faults,
        "transactions": transactions,
        "invalidTransactions": invalid,
        "failures": failures,
    }
    return result, failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--initiator-port")
    parser.add_argument("--responder-port")
    parser.add_argument("--initiator-log", type=Path)
    parser.add_argument("--responder-log", type=Path)
    parser.add_argument("--duration", type=float, default=75.0)
    parser.add_argument("--minimum-transactions", type=int, default=3)
    args = parser.parse_args()

    port_mode = args.initiator_port is not None or args.responder_port is not None
    log_mode = args.initiator_log is not None or args.responder_log is not None
    if port_mode == log_mode:
        parser.error("select exactly one complete serial-port pair or log-file pair")
    if port_mode:
        if args.initiator_port is None or args.responder_port is None:
            parser.error("both serial ports are required")
        if args.initiator_port == args.responder_port:
            parser.error("initiator and responder ports must be different")
    if log_mode and (args.initiator_log is None or args.responder_log is None):
        parser.error("both log files are required")
    if args.duration <= 0.0:
        parser.error("duration must be positive")
    if args.minimum_transactions < 3:
        parser.error("minimum-transactions must be at least 3")
    return args


def main() -> int:
    args = parse_args()
    observations = Observations()
    if args.initiator_port is not None:
        capture_serial(
            observations,
            args.initiator_port,
            args.responder_port,
            args.duration,
        )
        inputs = {
            "initiatorPort": args.initiator_port,
            "responderPort": args.responder_port,
        }
    else:
        capture_logs(observations, args.initiator_log, args.responder_log)
        inputs = {
            "initiatorLog": str(args.initiator_log),
            "responderLog": str(args.responder_log),
        }

    result, failures = evaluate(observations, args.minimum_transactions)
    result.update(inputs)
    print(json.dumps(result, indent=2, sort_keys=True))
    if failures:
        raise RuntimeError("; ".join(failures))
    print(f"JHCMD1 HOST PASS matched={len(result['validIds'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
