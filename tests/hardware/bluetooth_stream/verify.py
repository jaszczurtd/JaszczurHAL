#!/usr/bin/env python3
"""BlueZ hardware verifier for JH BLE Stream v1."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import math
import os
import struct
import sys
import time
from collections import deque

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305


BLUEZ = "org.bluez"
DEVICE_IFACE = "org.bluez.Device1"
GATT_SERVICE_IFACE = "org.bluez.GattService1"
GATT_IFACE = "org.bluez.GattCharacteristic1"
PROPS_IFACE = "org.freedesktop.DBus.Properties"
ADAPTER_IFACE = "org.bluez.Adapter1"
SERVICE_UUID = "b7ce0001-3c13-4fe2-801f-d71bdab1369b"
RX_UUID = "b7ce0002-3c13-4fe2-801f-d71bdab1369b"
TX_UUID = "b7ce0003-3c13-4fe2-801f-d71bdab1369b"
VERSION_UUID = "b7ce0004-3c13-4fe2-801f-d71bdab1369b"
CAPABILITIES_UUID = "b7ce0005-3c13-4fe2-801f-d71bdab1369b"
PROFILE = b"JH BLE Stream"
VERSION = 1
CLIENT_CAPABILITIES = 0x0003
EXPECTED_DEVICE_CAPABILITIES = 0x0003
MIN_ATT_MTU = 81
AUTH_ATTEMPT_LIMIT = 5
AUTH_BACKOFF_SECONDS = 30.0
AUTH_BACKOFF_MARGIN_SECONDS = 1.0
AUTH_BACKOFF_PROBE_INTERVAL_SECONDS = 1.0
AUTH_BACKOFF_PROBE_SILENCE_SECONDS = 0.2
AUTH_BACKOFF_FINAL_PROBE_MARGIN_SECONDS = 0.5
MIN_RECONNECTS = 50
MIN_STREAM_SECONDS = 300.0
MIN_STREAM_RATE = 10.0
STREAM_ACCEPTANCE_RATIO = 0.90
MIN_SATURATION_HOLD_SECONDS = 2.0
MAX_SATURATION_HOLD_SECONDS = 30.0
FRAME_HEADER_LENGTH = 4
SESSION_ID_LENGTH = 8
NONCE_LENGTH = 32
PROOF_LENGTH = 32
AEAD_COUNTER_LENGTH = 8
AEAD_TAG_LENGTH = 16
MAX_PAYLOAD_LENGTH = 128
FRAME_HELLO = 0x01
FRAME_HELLO_ACK = 0x02
FRAME_AUTH = 0x03
FRAME_AUTH_ACK = 0x04
FRAME_DATA = 0x05
DIR_DEVICE_TO_CLIENT = 0x01
DIR_CLIENT_TO_DEVICE = 0x02
CMD_RESTART = b"JHBL5/RESTART"
RESPONSE_RESTARTING = b"JHBL5/RESTARTING"
CMD_SATURATE = b"JHBL5/SATURATE"
RESPONSE_SATURATION_READY = b"JHBL5/SATURATE-READY"
CMD_STATS = b"JHBL5/STATS"
RESPONSE_STATS = b"J5S1"
CMD_IDENTITY = b"JHBL5/IDENTITY"
RESPONSE_IDENTITY = "J5I1"
CMD_BOOT = b"JHBL5/BOOT"
RESPONSE_BOOT = b"J5B1"
CMD_POWER_LOSS = b"JHBL5/POWER-LOSS"
RESPONSE_POWER_LOSS_ARMED = b"JHBL5/POWER-LOSS-ARMED"
RESET_REASON_WATCHDOG = 4
SUPPORTED_HARDWARE = {
    "rp2040": {"picow", "pico-rm2"},
    "rp2350-arm": {"pico2w"},
    "stm32g474": {"nucleo-g474re-pim730"},
}
SUPPORTED_RUNTIMES = ("baremetal", "freertos")
REQUIRED_CHARACTERISTIC_FLAGS = {
    RX_UUID: {"write", "write-without-response"},
    TX_UUID: {"notify"},
    VERSION_UUID: {"read"},
    CAPABILITIES_UUID: {"read"},
}
STATS_FIELDS = (
    "rx_queue_depth",
    "tx_queue_depth",
    "received",
    "echoed",
    "receive_overflows",
    "dropped_rx",
    "dropped_tx",
    "restarts",
    "lifecycle_failures",
    "ble_generation",
    "stream_generation",
)
SECRET = bytes.fromhex(
    "8f2c51e4b70d93a6147bc8356ef12a59"
    "d3608b47e21c75b039a84fd6621ec497"
)


def frame(frame_type: int, body: bytes, flags: int = 0) -> bytes:
    if len(body) > 255:
        raise ValueError("frame body exceeds v1 length field")
    return bytes((VERSION, frame_type, flags, len(body))) + body


def parse_frame(
    value: bytes,
    expected_type: int,
    *,
    exact_body_length: int | None = None,
    minimum_body_length: int | None = None,
    maximum_body_length: int | None = None,
) -> bytes:
    if len(value) < FRAME_HEADER_LENGTH:
        raise AssertionError(f"truncated frame header: {value.hex()}")
    version, frame_type, flags, body_length = value[:FRAME_HEADER_LENGTH]
    if version != VERSION:
        raise AssertionError(
            f"frame version mismatch: expected {VERSION}, got {version}"
        )
    if frame_type != expected_type:
        raise AssertionError(
            "unexpected notification type: "
            f"expected 0x{expected_type:02x}, got 0x{frame_type:02x}"
        )
    if flags != 0:
        raise AssertionError(f"reserved frame flags are non-zero: 0x{flags:02x}")
    actual_body_length = len(value) - FRAME_HEADER_LENGTH
    if body_length != actual_body_length:
        raise AssertionError(
            "frame body length mismatch: "
            f"header={body_length}, actual={actual_body_length}"
        )
    if exact_body_length is not None and body_length != exact_body_length:
        raise AssertionError(
            "unexpected frame body length: "
            f"expected {exact_body_length}, got {body_length}"
        )
    if minimum_body_length is not None and body_length < minimum_body_length:
        raise AssertionError(
            "frame body is too short: "
            f"minimum {minimum_body_length}, got {body_length}"
        )
    if maximum_body_length is not None and body_length > maximum_body_length:
        raise AssertionError(
            "frame body is too long: "
            f"maximum {maximum_body_length}, got {body_length}"
        )
    return value[FRAME_HEADER_LENGTH:]


def transcript(
    domain: int,
    device_capabilities: int,
    session_id: bytes,
    client_nonce: bytes,
    device_nonce: bytes,
) -> bytes:
    return b"".join(
        (
            bytes((domain,)),
            PROFILE,
            bytes((VERSION,)),
            struct.pack("<H", device_capabilities),
            struct.pack("<H", CLIENT_CAPABILITIES),
            session_id,
            client_nonce,
            device_nonce,
        )
    )


def derive(
    secret: bytes,
    domain: int,
    device_capabilities: int,
    session_id: bytes,
    client_nonce: bytes,
    device_nonce: bytes,
) -> bytes:
    data = transcript(
        domain,
        device_capabilities,
        session_id,
        client_nonce,
        device_nonce,
    )
    return hmac.new(secret, data, hashlib.sha256).digest()


def nonce(direction: int, counter: int) -> bytes:
    return bytes((direction, VERSION, 0, 0)) + struct.pack("<Q", counter)


def aad(direction: int, counter: int, flags: int = 0) -> bytes:
    return bytes((VERSION, FRAME_DATA, direction, flags)) + struct.pack(
        "<Q", counter
    )


class HandshakeHistory:
    def __init__(self) -> None:
        self.session_ids: set[bytes] = set()
        self.device_nonces: set[bytes] = set()

    def observe(self, session_id: bytes, device_nonce: bytes) -> None:
        if len(session_id) != SESSION_ID_LENGTH or not any(session_id):
            raise AssertionError("device returned an invalid session identifier")
        if len(device_nonce) != NONCE_LENGTH or not any(device_nonce):
            raise AssertionError("device returned an invalid nonce")
        if session_id in self.session_ids:
            raise AssertionError(f"reused session identifier: {session_id.hex()}")
        if device_nonce in self.device_nonces:
            raise AssertionError(f"reused device nonce: {device_nonce.hex()}")
        self.session_ids.add(session_id)
        self.device_nonces.add(device_nonce)

    @property
    def count(self) -> int:
        return len(self.session_ids)


class BluezClient:
    def __init__(self, address: str | None, name: str, timeout: float) -> None:
        DBusGMainLoop(set_as_default=True)
        self.bus = dbus.SystemBus()
        self.timeout = timeout
        self.address = address.upper() if address else None
        self.name = name
        self.notifications: deque[bytes] = deque()
        self.device_path = ""
        self.device = None
        self.service_path = ""
        self.characteristics: dict[str, dbus.Interface] = {}
        self.characteristic_paths: dict[str, str] = {}
        self._signal = None

    def objects(self):
        manager = dbus.Interface(
            self.bus.get_object(BLUEZ, "/"),
            "org.freedesktop.DBus.ObjectManager",
        )
        return manager.GetManagedObjects()

    def wait(self, predicate, description: str, timeout: float | None = None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        context = GLib.MainContext.default()
        while time.monotonic() < deadline:
            while context.pending():
                context.iteration(False)
            value = predicate()
            if value:
                return value
            time.sleep(0.02)
        raise TimeoutError(description)

    def find_device(self) -> None:
        def locate():
            for path, interfaces in self.objects().items():
                properties = interfaces.get(DEVICE_IFACE)
                if not properties:
                    continue
                address = str(properties.get("Address", "")).upper()
                alias = str(properties.get("Alias", properties.get("Name", "")))
                if self.address and address == self.address:
                    return str(path)
                if not self.address and alias == self.name:
                    return str(path)
            return ""

        path = locate()
        if not path:
            objects = self.objects()
            adapters = [
                str(path)
                for path, interfaces in objects.items()
                if ADAPTER_IFACE in interfaces
            ]
            if not adapters:
                raise RuntimeError("BlueZ exposes no adapter")
            adapter = dbus.Interface(
                self.bus.get_object(BLUEZ, adapters[0]), ADAPTER_IFACE
            )
            adapter.SetDiscoveryFilter(
                dbus.Dictionary(
                    {"Transport": dbus.String("le")}, signature="sv"
                )
            )
            adapter.StartDiscovery()
            try:
                path = self.wait(locate, "BLE peripheral discovery timed out")
            finally:
                adapter.StopDiscovery()
        self.device_path = path
        self.device = dbus.Interface(
            self.bus.get_object(BLUEZ, path), DEVICE_IFACE
        )

    def connect(self) -> None:
        self.find_device()
        deadline = time.monotonic() + self.timeout
        last_error = None
        while time.monotonic() < deadline:
            try:
                self.device.Connect()
                last_error = None
                break
            except dbus.DBusException as exc:
                if "AlreadyConnected" in exc.get_dbus_name():
                    last_error = None
                    break
                if self.is_connected():
                    last_error = None
                    break
                last_error = exc
                time.sleep(0.2)
        if last_error is not None:
            raise TimeoutError(f"BLE connection failed: {last_error}")

        def resolved():
            props = dbus.Interface(
                self.bus.get_object(BLUEZ, self.device_path), PROPS_IFACE
            )
            return bool(props.Get(DEVICE_IFACE, "ServicesResolved"))

        self.wait(resolved, "GATT services were not resolved")
        objects = self.objects()
        services = []
        for path, interfaces in objects.items():
            if not str(path).startswith(self.device_path):
                continue
            properties = interfaces.get(GATT_SERVICE_IFACE)
            if not properties:
                continue
            if str(properties.get("UUID", "")).lower() == SERVICE_UUID:
                services.append((str(path), properties))
        if len(services) != 1:
            raise RuntimeError(
                "expected exactly one JH BLE Stream service, "
                f"found {len(services)}"
            )
        self.service_path, service_properties = services[0]
        if str(service_properties.get("Device", "")) != self.device_path:
            raise RuntimeError("JH BLE Stream service belongs to another device")
        if not bool(service_properties.get("Primary", False)):
            raise RuntimeError("JH BLE Stream service is not primary")

        for path, interfaces in objects.items():
            if not str(path).startswith(self.device_path):
                continue
            properties = interfaces.get(GATT_IFACE)
            if not properties:
                continue
            uuid = str(properties.get("UUID", "")).lower()
            if uuid in {
                RX_UUID,
                TX_UUID,
                VERSION_UUID,
                CAPABILITIES_UUID,
            }:
                if uuid in self.characteristics:
                    raise RuntimeError(f"duplicate GATT characteristic: {uuid}")
                if str(properties.get("Service", "")) != self.service_path:
                    raise RuntimeError(
                        f"GATT characteristic {uuid} belongs to another service"
                    )
                flags = {str(flag) for flag in properties.get("Flags", [])}
                expected_flags = REQUIRED_CHARACTERISTIC_FLAGS[uuid]
                if flags != expected_flags:
                    raise RuntimeError(
                        f"GATT characteristic {uuid} flags mismatch: "
                        f"expected={sorted(expected_flags)}, "
                        f"actual={sorted(flags)}"
                    )
                self.characteristics[uuid] = dbus.Interface(
                    self.bus.get_object(BLUEZ, path), GATT_IFACE
                )
                self.characteristic_paths[uuid] = str(path)
        missing = {
            RX_UUID,
            TX_UUID,
            VERSION_UUID,
            CAPABILITIES_UUID,
        } - self.characteristics.keys()
        if missing:
            raise RuntimeError(f"missing GATT characteristics: {sorted(missing)}")

        self._signal = self.bus.add_signal_receiver(
            self._properties_changed,
            dbus_interface=PROPS_IFACE,
            signal_name="PropertiesChanged",
            path_keyword="path",
        )
        self.characteristics[TX_UUID].StartNotify()

    def _properties_changed(self, interface, changed, _invalidated, path=None):
        if (
            interface != GATT_IFACE
            or "Value" not in changed
            or str(path) != self.characteristic_paths.get(TX_UUID)
        ):
            return
        value = bytes(changed["Value"])
        self.notifications.append(value)

    def read(self, uuid: str) -> bytes:
        return bytes(self.characteristics[uuid].ReadValue({}))

    def write(self, value: bytes, write_type: str = "request") -> None:
        if write_type not in {"request", "command"}:
            raise ValueError(f"unsupported GATT write type: {write_type}")
        options = dbus.Dictionary(
            {"type": dbus.String(write_type)}, signature="sv"
        )
        self.characteristics[RX_UUID].WriteValue(
            dbus.Array(value, signature="y"), options
        )

    def mtu(self) -> int:
        for path, interfaces in self.objects().items():
            properties = interfaces.get(GATT_IFACE)
            if not properties or str(properties.get("UUID", "")).lower() != RX_UUID:
                continue
            if (
                str(path) == self.characteristic_paths.get(RX_UUID)
                and str(properties.get("Service", "")) == self.service_path
                and "MTU" in properties
            ):
                return int(properties["MTU"])
        return 0

    def connected_address(self) -> str:
        props = dbus.Interface(
            self.bus.get_object(BLUEZ, self.device_path), PROPS_IFACE
        )
        return str(props.Get(DEVICE_IFACE, "Address")).upper()

    def connection_state(self) -> bool:
        if self.device is None or not self.device_path:
            return False
        props = dbus.Interface(
            self.bus.get_object(BLUEZ, self.device_path), PROPS_IFACE
        )
        return bool(props.Get(DEVICE_IFACE, "Connected"))

    def is_connected(self) -> bool:
        try:
            return self.connection_state()
        except dbus.DBusException:
            return False

    def assert_connected(self, operation: str) -> None:
        if not self.connection_state():
            raise AssertionError(f"BLE link was lost during {operation}")

    def next_notification(self, frame_type: int, timeout: float | None = None) -> bytes:
        def select():
            if self.notifications:
                candidate = self.notifications.popleft()
                parse_frame(candidate, frame_type)
                return candidate
            self.assert_connected(
                f"notification wait for frame type 0x{frame_type:02x}"
            )
            return None

        return self.wait(
            select,
            f"notification type 0x{frame_type:02x} timed out",
            timeout,
        )

    def expect_silence(self, timeout: float = 0.6) -> None:
        deadline = time.monotonic() + timeout
        context = GLib.MainContext.default()
        while True:
            while context.pending():
                context.iteration(False)
            if self.notifications:
                value = self.notifications.popleft()
                raise AssertionError(f"unexpected notification: {value.hex()}")
            self.assert_connected("negative-response window")
            if time.monotonic() >= deadline:
                break
            time.sleep(0.02)

    def wait_disconnected(self, timeout: float | None = None) -> None:
        self.wait(
            lambda: not self.is_connected(),
            "BLE peripheral did not disconnect",
            timeout,
        )

    def disconnect(
        self, request_disconnect: bool = True, strict: bool = False
    ) -> None:
        disconnect_error = None
        connected_before = False
        disconnect_requested = False
        if request_disconnect:
            try:
                connected_before = self.connection_state()
                if strict and not connected_before:
                    raise AssertionError(
                        "strict disconnect started without an active BLE link"
                    )
            except (dbus.DBusException, AssertionError) as exc:
                disconnect_error = exc
            try:
                if TX_UUID in self.characteristics:
                    self.characteristics[TX_UUID].StopNotify()
            except dbus.DBusException:
                pass
            if disconnect_error is None:
                try:
                    if self.device is not None and connected_before:
                        disconnect_requested = True
                        self.device.Disconnect()
                        self.wait(
                            lambda: not self.connection_state(),
                            "BLE peripheral did not disconnect",
                        )
                    elif not strict and self.device is not None and self.is_connected():
                        disconnect_requested = True
                        self.device.Disconnect()
                        self.wait_disconnected()
                except (dbus.DBusException, TimeoutError) as exc:
                    disconnect_error = exc
            if (
                strict
                and disconnect_error is None
                and (not connected_before or not disconnect_requested)
            ):
                disconnect_error = AssertionError(
                    "strict disconnect did not prove connected-to-disconnected "
                    "transition"
                )
        if self._signal is not None:
            self._signal.remove()
            self._signal = None
        if strict and disconnect_error is not None:
            raise disconnect_error


class Session:
    def __init__(
        self,
        client: BluezClient,
        history: HandshakeHistory,
        secret: bytes = SECRET,
    ) -> None:
        self.client = client
        self.history = history
        self.secret = secret
        self.device_capabilities = 0
        self.session_id = b""
        self.client_nonce = b""
        self.device_nonce = b""
        self.key_d2c = b""
        self.key_c2d = b""
        self.tx_counter = 0
        self.rx_counter = 0
        self.auth_write_started_at = 0.0

    def hello(self) -> bytes:
        self.client_nonce = os.urandom(NONCE_LENGTH)
        body = struct.pack("<H", CLIENT_CAPABILITIES) + self.client_nonce
        self.client.write(frame(FRAME_HELLO, body))
        response = self.client.next_notification(FRAME_HELLO_ACK)
        body = parse_frame(
            response,
            FRAME_HELLO_ACK,
            exact_body_length=(
                2 + SESSION_ID_LENGTH + NONCE_LENGTH + PROOF_LENGTH
            ),
        )
        self.device_capabilities = struct.unpack_from("<H", body, 0)[0]
        self.session_id = body[2 : 2 + SESSION_ID_LENGTH]
        nonce_offset = 2 + SESSION_ID_LENGTH
        self.device_nonce = body[nonce_offset : nonce_offset + NONCE_LENGTH]
        proof_offset = nonce_offset + NONCE_LENGTH
        proof = body[proof_offset : proof_offset + PROOF_LENGTH]
        expected = derive(
            self.secret,
            0x01,
            self.device_capabilities,
            self.session_id,
            self.client_nonce,
            self.device_nonce,
        )
        if not hmac.compare_digest(proof, expected):
            raise AssertionError("device proof mismatch")
        self.history.observe(self.session_id, self.device_nonce)
        return response

    def authenticate(self, corrupt: bool = False) -> None:
        self.hello()
        proof = derive(
            self.secret,
            0x02,
            self.device_capabilities,
            self.session_id,
            self.client_nonce,
            self.device_nonce,
        )
        if corrupt:
            proof = bytes((proof[0] ^ 0x80,)) + proof[1:]
        self.auth_write_started_at = time.monotonic()
        self.client.write(frame(FRAME_AUTH, proof))
        if corrupt:
            self.client.expect_silence()
            return
        response = self.client.next_notification(FRAME_AUTH_ACK)
        body = parse_frame(
            response, FRAME_AUTH_ACK, exact_body_length=1
        )
        if body != b"\x00":
            raise AssertionError(f"unexpected AUTH_ACK: {response.hex()}")
        self.key_d2c = derive(
            self.secret,
            0x03,
            self.device_capabilities,
            self.session_id,
            self.client_nonce,
            self.device_nonce,
        )
        self.key_c2d = derive(
            self.secret,
            0x04,
            self.device_capabilities,
            self.session_id,
            self.client_nonce,
            self.device_nonce,
        )
        self.tx_counter = 0
        self.rx_counter = 0

    def data_frame(self, payload: bytes, counter: int, forge: bool = False) -> bytes:
        encrypted = ChaCha20Poly1305(self.key_c2d).encrypt(
            nonce(DIR_CLIENT_TO_DEVICE, counter),
            payload,
            aad(DIR_CLIENT_TO_DEVICE, counter),
        )
        if forge:
            encrypted = encrypted[:-1] + bytes((encrypted[-1] ^ 0x80,))
        return frame(FRAME_DATA, struct.pack("<Q", counter) + encrypted)

    def send_only(self, payload: bytes, write_type: str = "request") -> None:
        self.tx_counter += 1
        self.client.write(
            self.data_frame(payload, self.tx_counter), write_type=write_type
        )

    def receive(self, timeout: float | None = None) -> bytes:
        response = self.client.next_notification(FRAME_DATA, timeout)
        body = parse_frame(
            response,
            FRAME_DATA,
            minimum_body_length=(
                AEAD_COUNTER_LENGTH + AEAD_TAG_LENGTH + 1
            ),
            maximum_body_length=(
                AEAD_COUNTER_LENGTH + AEAD_TAG_LENGTH + MAX_PAYLOAD_LENGTH
            ),
        )
        counter = struct.unpack_from("<Q", body, 0)[0]
        if counter != self.rx_counter + 1:
            raise AssertionError(
                f"device counter gap: expected {self.rx_counter + 1}, got {counter}"
            )
        encrypted = body[AEAD_COUNTER_LENGTH:]
        plaintext = ChaCha20Poly1305(self.key_d2c).decrypt(
            nonce(DIR_DEVICE_TO_CLIENT, counter),
            encrypted,
            aad(DIR_DEVICE_TO_CLIENT, counter),
        )
        self.rx_counter = counter
        return plaintext

    def send(
        self,
        payload: bytes,
        timeout: float | None = None,
        write_type: str = "request",
    ) -> bytes:
        self.send_only(payload, write_type=write_type)
        return self.receive(timeout)


def parse_stats(payload: bytes) -> dict[str, int]:
    expected_length = len(RESPONSE_STATS) + (4 * len(STATS_FIELDS))
    if len(payload) != expected_length or not payload.startswith(RESPONSE_STATS):
        raise AssertionError(f"malformed fixture stats: {payload.hex()}")
    values = struct.unpack(
        f"<{len(STATS_FIELDS)}I", payload[len(RESPONSE_STATS) :]
    )
    return dict(zip(STATS_FIELDS, values))


def request_stats(session: Session) -> dict[str, int]:
    return parse_stats(session.send(CMD_STATS))


def request_boot_status(session: Session) -> tuple[int, int]:
    payload = session.send(CMD_BOOT)
    if len(payload) != len(RESPONSE_BOOT) + 1 + 8 or not payload.startswith(
        RESPONSE_BOOT
    ):
        raise AssertionError(f"malformed fixture boot status: {payload.hex()}")
    reason = payload[len(RESPONSE_BOOT)]
    boot_id = struct.unpack_from("<Q", payload, len(RESPONSE_BOOT) + 1)[0]
    if boot_id == 0:
        raise AssertionError("fixture returned an invalid zero boot identifier")
    return reason, boot_id


def parse_identity(payload: bytes) -> tuple[str, str, str]:
    try:
        fields = payload.decode("ascii").split("|")
    except UnicodeDecodeError as exc:
        raise AssertionError("fixture identity is not ASCII") from exc
    if len(fields) != 4 or fields[0] != RESPONSE_IDENTITY:
        raise AssertionError(f"malformed fixture identity: {payload!r}")
    target, board, runtime = fields[1:]
    if not target or not board or not runtime:
        raise AssertionError(f"incomplete fixture identity: {payload!r}")
    return target, board, runtime


def verify_identity(args, session: Session) -> tuple[str, str, str]:
    actual = parse_identity(session.send(CMD_IDENTITY))
    expected = (args.target, args.board, args.runtime)
    if actual != expected:
        raise AssertionError(
            f"fixture identity mismatch: expected={expected}, actual={actual}"
        )
    print(
        "JHBL5 identity PASS "
        f"target={actual[0]} board={actual[1]} runtime={actual[2]}"
    )
    return actual


def verify_metadata(client: BluezClient) -> tuple[int, int]:
    version = client.read(VERSION_UUID)
    capabilities = client.read(CAPABILITIES_UUID)
    if version != bytes((VERSION,)):
        raise AssertionError(f"protocol version mismatch: {version.hex()}")
    if len(capabilities) != 2:
        raise AssertionError("invalid capabilities characteristic")
    mtu = client.mtu()
    if mtu and mtu < MIN_ATT_MTU:
        raise AssertionError(f"ATT MTU {mtu} is below {MIN_ATT_MTU}")
    capability_bits = int.from_bytes(capabilities, "little")
    if capability_bits != EXPECTED_DEVICE_CAPABILITIES:
        raise AssertionError(
            "capabilities characteristic mismatch: "
            f"expected 0x{EXPECTED_DEVICE_CAPABILITIES:04x}, "
            f"got 0x{capability_bits:04x}"
        )
    print(
        "JHBL5 metadata "
        f"version={version[0]} capabilities=0x{capability_bits:04x} "
        f"mtu={mtu or 'bluez-managed'}"
    )
    return capability_bits, mtu


def connect_authenticated(
    args, history: HandshakeHistory
) -> tuple[BluezClient, Session]:
    client = BluezClient(args.address, args.name, args.timeout)
    try:
        client.connect()
        metadata_capabilities, _ = verify_metadata(client)
        session = Session(client, history)
        session.authenticate()
        if session.device_capabilities != metadata_capabilities:
            raise AssertionError(
                "capability mismatch between characteristic and HELLO_ACK: "
                f"characteristic=0x{metadata_capabilities:04x}, "
                f"HELLO_ACK=0x{session.device_capabilities:04x}"
            )
        return client, session
    except Exception:
        client.disconnect()
        raise


def verify_reconnects(args, client, session, history: HandshakeHistory):
    for cycle in range(1, args.reconnects + 1):
        client.disconnect(strict=True)
        next_client, next_session = connect_authenticated(args, history)
        try:
            payload = b"JHBL5/RECONNECT" + struct.pack("<I", cycle)
            if next_session.send(payload) != payload:
                raise AssertionError(
                    f"reconnect {cycle}: authenticated echo mismatch"
                )
            next_client.assert_connected(f"reconnect echo {cycle}")
        except Exception:
            next_client.disconnect()
            raise
        client, session = next_client, next_session
        if cycle % 10 == 0 or cycle == args.reconnects:
            print(f"JHBL5 reconnect progress={cycle}/{args.reconnects}")
    stats = request_stats(session)
    if stats["stream_generation"] != stats["ble_generation"]:
        raise AssertionError(
            "Stream did not observe the BLE generation during link loss"
        )
    return client, session


def verify_write_command(client: BluezClient, session: Session) -> None:
    payload = b"JHBL5/WRITE-COMMAND"
    if session.send(payload, write_type="command") != payload:
        raise AssertionError("write-without-response DATA echo mismatch")
    client.assert_connected("write-without-response DATA echo")
    print("JHBL5 write-command PASS")


def verify_simulated_power_loss(
    args,
    client: BluezClient,
    session: Session,
    history: HandshakeHistory,
    identity: tuple[str, str, str],
    address: str,
) -> tuple[BluezClient, Session]:
    before_reason, before_boot_id = request_boot_status(session)
    response = session.send(CMD_POWER_LOSS)
    if response != RESPONSE_POWER_LOSS_ARMED:
        raise AssertionError(f"unexpected power-loss response: {response!r}")
    client.wait_disconnected(args.timeout)
    client.disconnect(request_disconnect=False)

    next_client, next_session = connect_authenticated(args, history)
    try:
        if verify_identity(args, next_session) != identity:
            raise AssertionError("fixture identity changed across MCU reset")
        if next_client.connected_address() != address:
            raise AssertionError("BLE address changed across MCU reset")
        after_reason, after_boot_id = request_boot_status(next_session)
        if after_boot_id == before_boot_id:
            raise AssertionError(
                "simulated power-loss did not produce a fresh MCU boot identifier"
            )
        if after_reason != RESET_REASON_WATCHDOG:
            raise AssertionError(
                "simulated power-loss reset reason mismatch: "
                f"expected={RESET_REASON_WATCHDOG}, actual={after_reason}"
            )
        recovery = b"JHBL5/POWER-LOSS-RECOVERY"
        if next_session.send(recovery) != recovery:
            raise AssertionError("stream did not recover after MCU reset")
        next_client.assert_connected("simulated power-loss recovery")
    except Exception:
        next_client.disconnect()
        raise
    print(
        "JHBL5 power-loss PASS "
        f"reset_reason={before_reason}->{after_reason} "
        f"boot_id={before_boot_id:016x}->{after_boot_id:016x} "
        f"address={address}"
    )
    return next_client, next_session


def verify_sustained_stream(args, session: Session) -> int:
    started = time.monotonic()
    deadline = started + args.stream_seconds
    sent = 0
    total_latency = 0.0
    max_latency = 0.0
    while time.monotonic() < deadline:
        marker = struct.pack("<Q", sent)
        payload = b"JHBL5/SOAK" + marker + hashlib.sha256(marker).digest()[:16]
        request_started = time.monotonic()
        echoed = session.send(payload)
        latency = time.monotonic() - request_started
        total_latency += latency
        max_latency = max(max_latency, latency)
        if echoed != payload:
            raise AssertionError(
                f"sustained stream message {sent}: echo mismatch"
            )
        sent += 1
        next_send = started + (sent / args.stream_rate)
        remaining = next_send - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
    elapsed = time.monotonic() - started
    requested_messages = args.stream_seconds * args.stream_rate
    minimum_messages = math.floor(
        requested_messages * STREAM_ACCEPTANCE_RATIO
    )
    actual_rate = sent / elapsed if elapsed > 0.0 else 0.0
    minimum_rate = args.stream_rate * STREAM_ACCEPTANCE_RATIO
    tolerance_percent = (1.0 - STREAM_ACCEPTANCE_RATIO) * 100.0
    metrics = (
        f"requested_seconds={args.stream_seconds:.1f} "
        f"requested_rate_hz={args.stream_rate:.2f} "
        f"requested_messages={requested_messages:.1f} "
        f"actual_seconds={elapsed:.1f} actual_messages={sent} "
        f"actual_rate_hz={actual_rate:.2f} "
        f"minimum_rate_hz={minimum_rate:.2f} "
        f"tolerance_percent={tolerance_percent:.1f} "
        f"minimum_messages={minimum_messages}"
    )
    if sent < minimum_messages or actual_rate < minimum_rate:
        raise AssertionError(f"sustained stream rate below acceptance: {metrics}")
    print(
        f"JHBL5 stream PASS {metrics} "
        f"mean_ms={(total_latency / sent) * 1000.0:.1f} "
        f"max_ms={max_latency * 1000.0:.1f}"
    )
    return sent


def verify_saturation(args, client: BluezClient, session: Session) -> None:
    before = request_stats(session)
    queue_depth = before["rx_queue_depth"]
    if queue_depth < 2:
        raise AssertionError(f"invalid firmware RX queue depth: {queue_depth}")
    if args.saturation_frames <= queue_depth:
        raise AssertionError(
            "--saturation-frames must exceed the firmware RX queue depth "
            f"({queue_depth})"
        )

    hold_ms = round(args.saturation_hold * 1000.0)
    response = session.send(CMD_SATURATE + struct.pack("<I", hold_ms))
    if response != RESPONSE_SATURATION_READY:
        raise AssertionError(f"unexpected saturation response: {response!r}")

    payloads = [
        b"JHBL5/QUEUE" + struct.pack("<I", index)
        for index in range(args.saturation_frames)
    ]
    burst_started = time.monotonic()
    for payload in payloads:
        session.send_only(payload)
    burst_elapsed = time.monotonic() - burst_started
    if burst_elapsed >= args.saturation_hold:
        raise AssertionError(
            "saturation burst exceeded the requested firmware hold window: "
            f"{burst_elapsed:.3f}s >= {args.saturation_hold:.3f}s"
        )

    retained = [
        session.receive(
            args.saturation_hold + args.timeout
            if index == 0
            else args.timeout
        )
        for index in range(queue_depth)
    ]
    if retained != payloads[:queue_depth]:
        raise AssertionError(
            "RX queue did not retain the expected oldest payloads: "
            f"received={retained!r}"
        )
    client.expect_silence()

    recovery = b"JHBL5/QUEUE-RECOVERY"
    if session.send(recovery) != recovery:
        raise AssertionError("authenticated echo did not recover after saturation")
    after = request_stats(session)
    expected_drops = args.saturation_frames - queue_depth
    dropped_delta = after["dropped_rx"] - before["dropped_rx"]
    overflow_delta = (
        after["receive_overflows"] - before["receive_overflows"]
    )
    if dropped_delta != expected_drops:
        raise AssertionError(
            f"RX drop oracle mismatch: expected {expected_drops}, got {dropped_delta}"
        )
    if overflow_delta != 1:
        raise AssertionError(
            f"RX overflow oracle mismatch: expected 1, got {overflow_delta}"
        )
    if after["lifecycle_failures"] != before["lifecycle_failures"]:
        raise AssertionError("firmware reported a lifecycle failure during saturation")
    print(
        "JHBL5 saturation PASS "
        f"frames={args.saturation_frames} retained={queue_depth} "
        f"dropped={dropped_delta} overflows={overflow_delta}"
    )


def authenticated_security_recovery(
    client: BluezClient,
    history: HandshakeHistory,
    marker: bytes,
) -> Session:
    session = Session(client, history)
    session.authenticate()
    payload = b"JHBL5/SECURITY-RECOVERY/" + marker
    if session.send(payload) != payload:
        raise AssertionError(
            f"security recovery echo failed after {marker.decode('ascii')}"
        )
    client.assert_connected(
        f"security recovery after {marker.decode('ascii')}"
    )
    return session


def assert_backoff_hello_rejected(client: BluezClient) -> None:
    client.write(
        frame(
            FRAME_HELLO,
            struct.pack("<H", CLIENT_CAPABILITIES)
            + os.urandom(NONCE_LENGTH),
        )
    )
    client.expect_silence(AUTH_BACKOFF_PROBE_SILENCE_SECONDS)


def verify_security(
    client: BluezClient,
    session: Session,
    history: HandshakeHistory,
) -> Session:
    replay = session.data_frame(b"replay", session.tx_counter)
    client.write(replay)
    client.expect_silence()

    session = authenticated_security_recovery(client, history, b"replay")
    client.write(session.data_frame(b"gap", session.tx_counter + 2))
    client.expect_silence()

    session = authenticated_security_recovery(client, history, b"gap")
    client.write(
        session.data_frame(
            b"forged", session.tx_counter + 1, forge=True
        )
    )
    client.expect_silence()

    # A complete recovery proves that forged DATA did not stall the runtime and
    # resets the bounded authentication-attempt budget. Exhaust the fresh
    # budget, then verify that backoff rejects a new HELLO.
    session = authenticated_security_recovery(client, history, b"forged")
    backoff_started = 0.0
    for attempt in range(AUTH_ATTEMPT_LIMIT):
        failed_session = Session(client, history)
        failed_session.authenticate(corrupt=True)
        if attempt == AUTH_ATTEMPT_LIMIT - 1:
            backoff_started = failed_session.auth_write_started_at
    if backoff_started <= 0.0:
        raise AssertionError("authentication backoff start was not observed")

    final_probe_at = (
        backoff_started
        + AUTH_BACKOFF_SECONDS
        - AUTH_BACKOFF_FINAL_PROBE_MARGIN_SECONDS
    )
    next_probe_at = time.monotonic()
    probes = 0
    while next_probe_at < final_probe_at:
        remaining = next_probe_at - time.monotonic()
        if remaining > 0.0:
            time.sleep(remaining)
        assert_backoff_hello_rejected(client)
        probes += 1
        next_probe_at += AUTH_BACKOFF_PROBE_INTERVAL_SECONDS

    remaining = final_probe_at - time.monotonic()
    if remaining > 0.0:
        time.sleep(remaining)
    assert_backoff_hello_rejected(client)
    probes += 1

    recovery_deadline = (
        backoff_started
        + AUTH_BACKOFF_SECONDS
        + AUTH_BACKOFF_MARGIN_SECONDS
    )
    remaining = recovery_deadline - time.monotonic()
    if remaining > 0.0:
        client.expect_silence(remaining)
    session = authenticated_security_recovery(client, history, b"backoff")
    print(
        "JHBL5 security PASS "
        f"backoff_probes={probes} unique_handshakes={history.count}"
    )
    return session


def run(args) -> None:
    client = None
    stream_messages = 0
    identity = (args.target, args.board, args.runtime)
    history = HandshakeHistory()
    try:
        client, session = connect_authenticated(args, history)
        identity = verify_identity(args, session)
        verify_write_command(client, session)
        initial_address = client.connected_address()
        for index in range(args.burst):
            payload = f"echo-{index:02d}".encode()
            echoed = session.send(payload)
            if echoed != payload:
                raise AssertionError(f"echo mismatch: {echoed!r} != {payload!r}")

        before_restart = request_stats(session)
        restart_response = session.send(CMD_RESTART)
        if restart_response != RESPONSE_RESTARTING:
            raise AssertionError(
                f"unexpected restart response: {restart_response!r}"
            )
        client.wait_disconnected(args.timeout)
        client.disconnect(request_disconnect=False)
        client = None

        client, session = connect_authenticated(args, history)
        restarted_identity = verify_identity(args, session)
        if restarted_identity != identity:
            raise AssertionError("fixture identity changed across BLE restart")
        if client.connected_address() != initial_address:
            raise AssertionError("BLE address changed across subsystem restart")
        after_restart = request_stats(session)
        if after_restart["restarts"] != before_restart["restarts"] + 1:
            raise AssertionError("firmware did not confirm exactly one BLE restart")
        if (
            after_restart["lifecycle_failures"]
            != before_restart["lifecycle_failures"]
        ):
            raise AssertionError("firmware reported a lifecycle restart failure")
        if after_restart["ble_generation"] == before_restart["ble_generation"]:
            raise AssertionError("BLE generation did not change across restart")
        # Stream is deliberately deinitialized before BLE, so its generation
        # is validated after the real link-loss cycles instead of here.
        lifecycle_echo = b"JHBL5/LIFECYCLE-RECOVERY"
        if session.send(lifecycle_echo) != lifecycle_echo:
            raise AssertionError("stream did not recover after BLE restart")
        client.assert_connected("lifecycle recovery echo")
        print(
            "JHBL5 lifecycle PASS "
            f"address={initial_address} generation="
            f"{before_restart['ble_generation']}->{after_restart['ble_generation']}"
        )

        if args.target in {"rp2040", "rp2350-arm"}:
            client, session = verify_simulated_power_loss(
                args, client, session, history, identity, initial_address
            )

        client, session = verify_reconnects(
            args, client, session, history
        )
        stream_messages = verify_sustained_stream(args, session)
        verify_saturation(args, client, session)
        session = verify_security(client, session, history)
        client.assert_connected("final security recovery")
        print(
            "JHBL5 HOST PASS "
            f"target={identity[0]} board={identity[1]} "
            f"runtime={identity[2]} burst={args.burst} "
            f"reconnects={args.reconnects} "
            f"stream_messages={stream_messages} "
            f"stream_seconds={args.stream_seconds:.1f} "
            f"stream_rate_hz={args.stream_rate:.2f} "
            f"saturation_frames={args.saturation_frames} "
            f"unique_handshakes={history.count}"
        )
    finally:
        if client is not None:
            client.disconnect()


def integer_at_least(minimum: int):
    def parse(value: str) -> int:
        parsed = int(value)
        if parsed < minimum:
            raise argparse.ArgumentTypeError(
                f"must be greater than or equal to {minimum}"
            )
        return parsed

    return parse


def float_in_range(minimum: float, maximum: float | None = None):
    def parse(value: str) -> float:
        parsed = float(value)
        if parsed < minimum or (maximum is not None and parsed > maximum):
            description = (
                f"between {minimum} and {maximum}"
                if maximum is not None
                else f"greater than or equal to {minimum}"
            )
            raise argparse.ArgumentTypeError(f"must be {description}")
        return parsed

    return parse


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", help="public BLE address")
    parser.add_argument("--name", default="JH Stream HW")
    parser.add_argument(
        "--target",
        choices=sorted(SUPPORTED_HARDWARE),
        required=True,
        help="expected firmware target identity",
    )
    parser.add_argument(
        "--board",
        choices=sorted(
            board
            for boards in SUPPORTED_HARDWARE.values()
            for board in boards
        ),
        required=True,
        help="expected firmware board identity",
    )
    parser.add_argument(
        "--runtime",
        choices=SUPPORTED_RUNTIMES,
        required=True,
        help="expected firmware runtime identity",
    )
    parser.add_argument("--timeout", type=float_in_range(1.0), default=10.0)
    parser.add_argument("--burst", type=integer_at_least(1), default=12)
    parser.add_argument(
        "--reconnects",
        type=integer_at_least(MIN_RECONNECTS),
        default=MIN_RECONNECTS,
        help=f"authenticated reconnect cycles (minimum {MIN_RECONNECTS})",
    )
    parser.add_argument(
        "--stream-seconds",
        type=float_in_range(MIN_STREAM_SECONDS),
        default=300.0,
        help=(
            "continuous authenticated stream duration "
            f"(minimum {MIN_STREAM_SECONDS:g}s)"
        ),
    )
    parser.add_argument(
        "--stream-rate",
        type=float_in_range(MIN_STREAM_RATE, 100.0),
        default=MIN_STREAM_RATE,
        help=(
            "target authenticated echo rate in messages per second "
            f"(minimum {MIN_STREAM_RATE:g})"
        ),
    )
    parser.add_argument(
        "--saturation-frames",
        type=integer_at_least(3),
        default=12,
        help="encrypted frames sent while firmware RX draining is paused",
    )
    parser.add_argument(
        "--saturation-hold",
        type=float_in_range(
            MIN_SATURATION_HOLD_SECONDS, MAX_SATURATION_HOLD_SECONDS
        ),
        default=5.0,
        help="firmware RX pause in seconds",
    )
    args = parser.parse_args()
    if args.board not in SUPPORTED_HARDWARE[args.target]:
        parser.error(
            f"board {args.board!r} is not valid for target {args.target!r}"
        )
    return args


def main() -> int:
    try:
        run(parse_args())
    except Exception as exc:
        print(f"JHBL5 HOST FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
