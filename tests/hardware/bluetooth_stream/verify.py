#!/usr/bin/env python3
"""BlueZ hardware verifier for JH BLE Stream v1."""

from __future__ import annotations

import argparse
import hashlib
import hmac
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
MIN_ATT_MTU = 81
AUTH_ATTEMPT_LIMIT = 5
FRAME_HELLO = 0x01
FRAME_HELLO_ACK = 0x02
FRAME_AUTH = 0x03
FRAME_AUTH_ACK = 0x04
FRAME_DATA = 0x05
DIR_DEVICE_TO_CLIENT = 0x01
DIR_CLIENT_TO_DEVICE = 0x02
SECRET = bytes.fromhex(
    "8f2c51e4b70d93a6147bc8356ef12a59"
    "d3608b47e21c75b039a84fd6621ec497"
)


def frame(frame_type: int, body: bytes, flags: int = 0) -> bytes:
    if len(body) > 255:
        raise ValueError("frame body exceeds v1 length field")
    return bytes((VERSION, frame_type, flags, len(body))) + body


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
        self.characteristics: dict[str, dbus.Interface] = {}
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
        try:
            self.device.Connect()
        except dbus.DBusException as exc:
            if "AlreadyConnected" not in exc.get_dbus_name():
                raise

        def resolved():
            props = dbus.Interface(
                self.bus.get_object(BLUEZ, self.device_path), PROPS_IFACE
            )
            return bool(props.Get(DEVICE_IFACE, "ServicesResolved"))

        self.wait(resolved, "GATT services were not resolved")
        for path, interfaces in self.objects().items():
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
                self.characteristics[uuid] = dbus.Interface(
                    self.bus.get_object(BLUEZ, path), GATT_IFACE
                )
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
        if interface != GATT_IFACE or "Value" not in changed:
            return
        value = bytes(changed["Value"])
        self.notifications.append(value)

    def read(self, uuid: str) -> bytes:
        return bytes(self.characteristics[uuid].ReadValue({}))

    def write(self, value: bytes) -> None:
        options = dbus.Dictionary(
            {"type": dbus.String("request")}, signature="sv"
        )
        self.characteristics[RX_UUID].WriteValue(
            dbus.Array(value, signature="y"), options
        )

    def mtu(self) -> int:
        for path, interfaces in self.objects().items():
            properties = interfaces.get(GATT_IFACE)
            if not properties or str(properties.get("UUID", "")).lower() != RX_UUID:
                continue
            if str(path).startswith(self.device_path) and "MTU" in properties:
                return int(properties["MTU"])
        return 0

    def next_notification(self, frame_type: int, timeout: float | None = None) -> bytes:
        def select():
            while self.notifications:
                candidate = self.notifications.popleft()
                if len(candidate) >= 4 and candidate[1] == frame_type:
                    return candidate
            return None

        return self.wait(select, f"notification type 0x{frame_type:02x} timed out", timeout)

    def expect_silence(self, timeout: float = 0.6) -> None:
        deadline = time.monotonic() + timeout
        context = GLib.MainContext.default()
        while time.monotonic() < deadline:
            while context.pending():
                context.iteration(False)
            if self.notifications:
                value = self.notifications.popleft()
                raise AssertionError(f"unexpected notification: {value.hex()}")
            time.sleep(0.02)

    def disconnect(self) -> None:
        try:
            if TX_UUID in self.characteristics:
                self.characteristics[TX_UUID].StopNotify()
        except dbus.DBusException:
            pass
        try:
            if self.device is not None:
                self.device.Disconnect()
        except dbus.DBusException:
            pass
        if self._signal is not None:
            self._signal.remove()


class Session:
    def __init__(self, client: BluezClient, secret: bytes = SECRET) -> None:
        self.client = client
        self.secret = secret
        self.device_capabilities = 0
        self.session_id = b""
        self.client_nonce = b""
        self.device_nonce = b""
        self.key_d2c = b""
        self.key_c2d = b""
        self.tx_counter = 0
        self.rx_counter = 0

    def hello(self) -> bytes:
        self.client_nonce = os.urandom(32)
        body = struct.pack("<H", CLIENT_CAPABILITIES) + self.client_nonce
        self.client.write(frame(FRAME_HELLO, body))
        response = self.client.next_notification(FRAME_HELLO_ACK)
        if response[0] != VERSION or response[3] != len(response) - 4:
            raise AssertionError("malformed HELLO_ACK")
        body = response[4:]
        self.device_capabilities = struct.unpack_from("<H", body, 0)[0]
        self.session_id = body[2:10]
        self.device_nonce = body[10:42]
        proof = body[42:74]
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
        self.client.write(frame(FRAME_AUTH, proof))
        if corrupt:
            self.client.expect_silence()
            return
        response = self.client.next_notification(FRAME_AUTH_ACK)
        if response != frame(FRAME_AUTH_ACK, b"\x00"):
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

    def send(self, payload: bytes) -> bytes:
        self.tx_counter += 1
        self.client.write(self.data_frame(payload, self.tx_counter))
        response = self.client.next_notification(FRAME_DATA)
        counter = struct.unpack_from("<Q", response, 4)[0]
        if counter != self.rx_counter + 1:
            raise AssertionError(
                f"device counter gap: expected {self.rx_counter + 1}, got {counter}"
            )
        encrypted = response[12:]
        plaintext = ChaCha20Poly1305(self.key_d2c).decrypt(
            nonce(DIR_DEVICE_TO_CLIENT, counter),
            encrypted,
            aad(DIR_DEVICE_TO_CLIENT, counter),
        )
        self.rx_counter = counter
        return plaintext


def run(args) -> None:
    client = BluezClient(args.address, args.name, args.timeout)
    client.connect()
    try:
        version = client.read(VERSION_UUID)
        capabilities = client.read(CAPABILITIES_UUID)
        if version != bytes((VERSION,)):
            raise AssertionError(f"protocol version mismatch: {version.hex()}")
        if len(capabilities) != 2:
            raise AssertionError("invalid capabilities characteristic")
        mtu = client.mtu()
        if mtu and mtu < MIN_ATT_MTU:
            raise AssertionError(f"ATT MTU {mtu} is below {MIN_ATT_MTU}")
        print(
            "JHBL5 metadata "
            f"version={version[0]} capabilities=0x{int.from_bytes(capabilities, 'little'):04x} "
            f"mtu={mtu or 'bluez-managed'}"
        )

        session = Session(client)
        session.authenticate()
        for index in range(args.burst):
            payload = f"echo-{index:02d}".encode()
            echoed = session.send(payload)
            if echoed != payload:
                raise AssertionError(f"echo mismatch: {echoed!r} != {payload!r}")

        replay = session.data_frame(b"replay", session.tx_counter)
        client.write(replay)
        client.expect_silence()

        session = Session(client)
        session.authenticate()
        client.write(session.data_frame(b"gap", 2))
        client.expect_silence()

        session = Session(client)
        session.authenticate()
        client.write(session.data_frame(b"forged", 1, forge=True))
        client.expect_silence()

        # The forged DATA tag above already consumes one authentication
        # attempt. Fill the remaining budget, then verify that backoff rejects
        # a fresh HELLO.
        for _ in range(AUTH_ATTEMPT_LIMIT - 1):
            session = Session(client)
            session.authenticate(corrupt=True)
        client.write(
            frame(
                FRAME_HELLO,
                struct.pack("<H", CLIENT_CAPABILITIES) + os.urandom(32),
            )
        )
        client.expect_silence()
        print(f"JHBL5 HOST PASS burst={args.burst}")
    finally:
        client.disconnect()


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", help="public BLE address")
    parser.add_argument("--name", default="JH Stream HW")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--burst", type=int, default=12)
    return parser.parse_args()


def main() -> int:
    try:
        run(parse_args())
    except Exception as exc:
        print(f"JHBL5 HOST FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
