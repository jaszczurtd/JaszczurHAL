#!/usr/bin/env python3
"""Short BlueZ verifier for the command router over JH BLE Stream v1."""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify as stream_verify  # noqa: E402


WIRE_MAGIC = b"JC"
WIRE_VERSION = 1
WIRE_HEADER_LENGTH = 16
WIRE_NAME_MAX = 32
WIRE_PAYLOAD_MAX = 512
MESSAGE_REQUEST = 1
MESSAGE_RESPONSE = 2
MESSAGE_EVENT = 3
ENCODING_BINARY = 0
ENCODING_TEXT = 1
STATUS_NONE = 0
STATUS_OK = 1
STATUS_ENOENT = -6
STATUS_EPERM = -12
SECURITY_ALL = 0x0000000F
STREAM_DATA_OVERHEAD = 31
READY_EVENT = "peripheral.ready"
RESULT_EVENT = "peripheral.result"
HOST_ECHO_COMMAND = "host.echo"
HOST_ECHO_PAYLOAD = b"peripheral-to-central"


@dataclass(frozen=True)
class WireMessage:
    message_type: int
    encoding: int
    request_id: int
    status: int
    name: str
    payload: bytes


def encode_message(message: WireMessage) -> bytes:
    try:
        name = message.name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("command-wire name is not ASCII") from exc
    if not 0 <= len(name) < WIRE_NAME_MAX:
        raise ValueError("command-wire name length is invalid")
    if len(message.payload) > WIRE_PAYLOAD_MAX:
        raise ValueError("command-wire payload is too large")
    header = struct.pack(
        ">2sBBBBHIhH",
        WIRE_MAGIC,
        WIRE_VERSION,
        message.message_type,
        message.encoding,
        len(name),
        0,
        message.request_id,
        message.status,
        len(message.payload),
    )
    return header + name + message.payload


def decode_prefix(buffer: bytearray) -> tuple[WireMessage | None, int]:
    if len(buffer) < WIRE_HEADER_LENGTH:
        return None, WIRE_HEADER_LENGTH
    (
        magic,
        version,
        message_type,
        encoding,
        name_length,
        reserved,
        request_id,
        status,
        payload_length,
    ) = struct.unpack_from(">2sBBBBHIhH", buffer)
    if magic != WIRE_MAGIC or version != WIRE_VERSION or reserved != 0:
        raise AssertionError("invalid command-wire header")
    if message_type not in {MESSAGE_REQUEST, MESSAGE_RESPONSE, MESSAGE_EVENT}:
        raise AssertionError(f"invalid command-wire type {message_type}")
    if encoding not in {ENCODING_BINARY, ENCODING_TEXT, 2}:
        raise AssertionError(f"invalid command-wire encoding {encoding}")
    if name_length >= WIRE_NAME_MAX or payload_length > WIRE_PAYLOAD_MAX:
        raise AssertionError("command-wire bounds are invalid")
    total = WIRE_HEADER_LENGTH + name_length + payload_length
    if len(buffer) < total:
        return None, total
    try:
        name = bytes(buffer[WIRE_HEADER_LENGTH : WIRE_HEADER_LENGTH + name_length]).decode(
            "ascii"
        )
    except UnicodeDecodeError as exc:
        raise AssertionError("command-wire name is not ASCII") from exc
    payload = bytes(buffer[WIRE_HEADER_LENGTH + name_length : total])
    if message_type == MESSAGE_REQUEST:
        valid = request_id != 0 and status == STATUS_NONE and bool(name)
    elif message_type == MESSAGE_RESPONSE:
        valid = request_id != 0 and status != STATUS_NONE and not name
    else:
        valid = request_id == 0 and status == STATUS_NONE and bool(name)
    if not valid:
        raise AssertionError("invalid command-wire message fields")
    return WireMessage(message_type, encoding, request_id, status, name, payload), total


class CommandChannel:
    def __init__(self, session: stream_verify.Session, mtu: int, timeout: float) -> None:
        self.session = session
        self.timeout = timeout
        effective_mtu = mtu or stream_verify.MIN_ATT_MTU
        self.chunk_size = min(
            stream_verify.MAX_PAYLOAD_LENGTH,
            effective_mtu - STREAM_DATA_OVERHEAD,
        )
        if self.chunk_size <= 0:
            raise AssertionError(f"ATT MTU {effective_mtu} cannot carry DATA")
        self.receive_buffer = bytearray()
        self.last_sent_chunks = 0
        self.last_received_chunks = 0

    def send_message(self, message: WireMessage) -> None:
        wire = encode_message(message)
        chunks = 0
        for offset in range(0, len(wire), self.chunk_size):
            self.session.send_only(wire[offset : offset + self.chunk_size])
            chunks += 1
        self.last_sent_chunks = chunks

    def next_message(self, timeout: float | None = None) -> WireMessage:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        chunks = 0
        while True:
            message, consumed = decode_prefix(self.receive_buffer)
            if message is not None:
                del self.receive_buffer[:consumed]
                self.last_received_chunks = chunks
                return message
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                raise TimeoutError("command-wire message timed out")
            self.receive_buffer.extend(self.session.receive(remaining))
            chunks += 1

    def request(
        self,
        request_id: int,
        name: str,
        encoding: int,
        payload: bytes = b"",
    ) -> WireMessage:
        self.send_message(
            WireMessage(
                MESSAGE_REQUEST,
                encoding,
                request_id,
                STATUS_NONE,
                name,
                payload,
            )
        )
        response = self.next_message()
        if response.message_type != MESSAGE_RESPONSE:
            raise AssertionError(f"expected RESPONSE, got {response}")
        if response.request_id != request_id:
            raise AssertionError(
                f"response id mismatch: expected {request_id}, got {response.request_id}"
            )
        return response


def expect_message(
    message: WireMessage,
    message_type: int,
    name: str,
    encoding: int,
    status: int,
) -> None:
    expected_id = 0 if message_type == MESSAGE_EVENT else message.request_id
    expected = (message_type, encoding, expected_id, status, name)
    actual = (
        message.message_type,
        message.encoding,
        message.request_id,
        message.status,
        message.name,
    )
    if actual != expected:
        raise AssertionError(f"command-wire fields mismatch: {actual} != {expected}")


def verify_ready(
    args: argparse.Namespace,
    session: stream_verify.Session,
    channel: CommandChannel,
) -> None:
    message = channel.next_message()
    expect_message(
        message,
        MESSAGE_EVENT,
        READY_EVENT,
        ENCODING_TEXT,
        STATUS_NONE,
    )
    try:
        marker, target, board, runtime, session_text = message.payload.decode(
            "ascii"
        ).split("|")
    except (UnicodeDecodeError, ValueError) as exc:
        raise AssertionError(f"malformed ready event: {message.payload!r}") from exc
    expected_session = int.from_bytes(session.session_id, "little")
    if (marker, target, board, runtime) != (
        "JBC1",
        args.target,
        args.board,
        args.runtime,
    ):
        raise AssertionError(
            "ready identity mismatch: "
            f"{(marker, target, board, runtime)!r}"
        )
    if int(session_text, 16) != expected_session:
        raise AssertionError("ready event session identifier mismatch")


def verify_peripheral_request(channel: CommandChannel) -> None:
    request = channel.next_message()
    expect_message(
        request,
        MESSAGE_REQUEST,
        HOST_ECHO_COMMAND,
        ENCODING_BINARY,
        STATUS_NONE,
    )
    if request.payload != HOST_ECHO_PAYLOAD:
        raise AssertionError(f"unexpected Peripheral request: {request.payload!r}")
    channel.send_message(
        WireMessage(
            MESSAGE_RESPONSE,
            request.encoding,
            request.request_id,
            STATUS_OK,
            "",
            request.payload,
        )
    )
    result = channel.next_message()
    expect_message(
        result,
        MESSAGE_EVENT,
        RESULT_EVENT,
        ENCODING_TEXT,
        STATUS_NONE,
    )
    if result.payload != b"PASS":
        raise AssertionError(f"Peripheral result event failed: {result.payload!r}")


def deterministic_payload(length: int, generation: int) -> bytes:
    return bytes(
        (index * 29 + generation * 17 + (index >> 8)) & 0xFF
        for index in range(length)
    )


def verify_metadata(
    response: WireMessage,
    session: stream_verify.Session,
    expected_last_counter: int,
) -> None:
    if response.status != STATUS_OK or response.encoding != ENCODING_TEXT:
        raise AssertionError(f"metadata response failed: {response}")
    try:
        fields = response.payload.decode("ascii").split("|")
        (
            marker,
            source,
            security_text,
            peer_text,
            session_text,
            connection_text,
            mtu_text,
            ble_generation_text,
            stream_generation_text,
            first_counter_text,
            last_counter_text,
            call_text,
        ) = fields
        security = int(security_text, 16)
        peer_id = int(peer_text, 16)
        session_id = int(session_text, 16)
        connection = int(connection_text)
        mtu = int(mtu_text)
        ble_generation = int(ble_generation_text)
        stream_generation = int(stream_generation_text)
        first_counter = int(first_counter_text)
        last_counter = int(last_counter_text)
        call = int(call_text)
    except (UnicodeDecodeError, ValueError) as exc:
        raise AssertionError(f"malformed metadata response: {response.payload!r}") from exc
    expected_session = int.from_bytes(session.session_id, "little")
    if marker != "JBCM1" or source != "BLE_STREAM":
        raise AssertionError(f"metadata source mismatch: {fields!r}")
    if security != SECURITY_ALL or peer_id == 0 or session_id != expected_session:
        raise AssertionError(f"metadata security or identity mismatch: {fields!r}")
    if connection == 0 or mtu < stream_verify.MIN_ATT_MTU:
        raise AssertionError(f"metadata link values are invalid: {fields!r}")
    if ble_generation == 0 or stream_generation == 0 or call == 0:
        raise AssertionError(f"metadata counters are invalid: {fields!r}")
    if first_counter != expected_last_counter or last_counter != expected_last_counter:
        raise AssertionError(
            "metadata DATA counter mismatch: "
            f"expected {expected_last_counter}, got {first_counter}-{last_counter}"
        )


def verify_router_requests(
    session: stream_verify.Session,
    channel: CommandChannel,
    generation: int,
    full_payload: bool,
) -> None:
    payload_length = 500 if full_payload else 64
    payload = deterministic_payload(payload_length, generation)
    response = channel.request(0x1000 + generation, "echo", ENCODING_BINARY, payload)
    request_chunks = channel.last_sent_chunks
    response_chunks = channel.last_received_chunks
    if (
        response.status != STATUS_OK
        or response.encoding != ENCODING_BINARY
        or response.payload != payload
    ):
        raise AssertionError("binary echo response mismatch")
    if full_payload and (request_chunks < 2 or response_chunks < 2):
        raise AssertionError(
            "500-byte echo did not exercise command-wire fragmentation: "
            f"request={request_chunks}, response={response_chunks}"
        )

    metadata = channel.request(
        0x2000 + generation, "metadata", ENCODING_TEXT
    )
    verify_metadata(metadata, session, session.tx_counter)

    if not full_payload:
        return
    forbidden = channel.request(
        0x3000 + generation, "ble-forbidden", ENCODING_TEXT
    )
    if forbidden.status != STATUS_EPERM or forbidden.payload:
        raise AssertionError(f"source policy was not enforced: {forbidden}")
    unknown = channel.request(
        0x4000 + generation, "missing-command", ENCODING_TEXT
    )
    if unknown.status != STATUS_ENOENT or unknown.payload:
        raise AssertionError(f"unknown command result mismatch: {unknown}")
    print(
        "JHBC1 router PASS "
        f"payload={payload_length} request_chunks={request_chunks} "
        f"response_chunks={response_chunks} session="
        f"{int.from_bytes(session.session_id, 'little'):016X}"
    )


def connect_channel(
    args: argparse.Namespace,
    history: stream_verify.HandshakeHistory,
) -> tuple[stream_verify.BluezClient, stream_verify.Session, CommandChannel]:
    client, session = stream_verify.connect_authenticated(args, history)
    mtu = client.mtu()
    return client, session, CommandChannel(session, mtu, args.timeout)


def verify_session(
    args: argparse.Namespace,
    session: stream_verify.Session,
    channel: CommandChannel,
    generation: int,
    full_payload: bool,
) -> None:
    verify_ready(args, session, channel)
    verify_peripheral_request(channel)
    verify_router_requests(session, channel, generation, full_payload)


def run(args: argparse.Namespace) -> None:
    history = stream_verify.HandshakeHistory()
    client = None
    try:
        client, session, channel = connect_channel(args, history)
        verify_session(args, session, channel, 1, True)
        for generation in range(2, args.reconnects + 2):
            client.disconnect(strict=True)
            client = None
            client, session, channel = connect_channel(args, history)
            verify_session(args, session, channel, generation, False)
            print(f"JHBC1 reconnect PASS cycle={generation - 1}/{args.reconnects}")
        print(
            "JHBC1 HOST PASS "
            f"target={args.target} board={args.board} runtime={args.runtime} "
            f"reconnects={args.reconnects} sessions={history.count}"
        )
    finally:
        if client is not None:
            client.disconnect()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", help="public BLE address")
    parser.add_argument("--name", default="JH Commands")
    parser.add_argument(
        "--target",
        choices=sorted(stream_verify.SUPPORTED_HARDWARE),
        required=True,
    )
    parser.add_argument(
        "--board",
        choices=sorted(
            board
            for boards in stream_verify.SUPPORTED_HARDWARE.values()
            for board in boards
        ),
        required=True,
    )
    parser.add_argument(
        "--runtime",
        choices=stream_verify.SUPPORTED_RUNTIMES,
        required=True,
    )
    parser.add_argument(
        "--timeout",
        type=stream_verify.float_in_range(1.0),
        default=10.0,
    )
    parser.add_argument(
        "--reconnects",
        type=stream_verify.integer_at_least(0),
        default=1,
    )
    args = parser.parse_args()
    if args.board not in stream_verify.SUPPORTED_HARDWARE[args.target]:
        parser.error(
            f"board {args.board!r} is not valid for target {args.target!r}"
        )
    return args


def main() -> int:
    try:
        run(parse_args())
    except Exception as exc:
        print(f"JHBC1 HOST FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
