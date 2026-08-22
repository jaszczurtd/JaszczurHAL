#!/usr/bin/env python3
"""Unit checks for native RP 1200-bps touch and BOOTSEL wait helpers."""

from __future__ import annotations

import errno
import hashlib
import hmac
import inspect
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT))

from vscode.runtime import jh_vscode as module

assert module.NATIVE_RP_TARGETS == {"rp2040", "rp2350-arm", "rp2350-riscv"}
assert "upload-ota" in module.SUPPORTED_ACTIONS
assert "ota-discover" in module.SUPPORTED_ACTIONS
assert "clear-identity" in module.SUPPORTED_ACTIONS
assert module.UF2_TARGETS == module.NATIVE_RP_TARGETS
assert module.ota_listen_port({}) == 8266
assert module.ota_listen_port({"listenPort": 0}) == 0
assert module.ota_listen_port({"listenPort": 9000}) == 9000
auth2_arguments = {
    "password": "correct horse battery staple",
    "command": 0,
    "tcp_port": 3232,
    "image_size": 4096,
    "image_md5": "0123456789abcdef0123456789abcdef",
    "device_nonce": "00112233445566778899aabbccddeeff",
    "client_nonce": "ffeeddccbbaa99887766554433221100",
}
assert module.ota_auth2_tag(**auth2_arguments) == (
    "c704cfc163213195568901d2399b8434e8e199d221fbfa82e83e2bfd8446bdf1"
)
uppercase_auth2_arguments = dict(auth2_arguments)
uppercase_auth2_arguments["image_md5"] = auth2_arguments["image_md5"].upper()
uppercase_auth2_arguments["device_nonce"] = auth2_arguments["device_nonce"].upper()
uppercase_auth2_arguments["client_nonce"] = auth2_arguments["client_nonce"].upper()
assert module.ota_auth2_tag(**uppercase_auth2_arguments) == module.ota_auth2_tag(
    **auth2_arguments
)
for changed_field, changed_value in (
    ("command", 100),
    ("tcp_port", 3233),
    ("image_size", 4097),
    ("image_md5", "1123456789abcdef0123456789abcdef"),
    ("device_nonce", "10112233445566778899aabbccddeeff"),
    ("client_nonce", "0feeddccbbaa99887766554433221100"),
):
    changed = dict(auth2_arguments)
    changed[changed_field] = changed_value
    assert module.ota_auth2_tag(**changed) != module.ota_auth2_tag(
        **auth2_arguments
    )
for invalid_auth2 in (
    {**auth2_arguments, "command": 1},
    {**auth2_arguments, "tcp_port": 0},
    {**auth2_arguments, "image_size": 0x100000000},
    {**auth2_arguments, "device_nonce": "not-a-nonce"},
):
    try:
        module.ota_auth2_tag(**invalid_auth2)
    except ValueError as error:
        assert "AUTH2" in str(error)
    else:
        raise AssertionError("invalid AUTH2 input was accepted")
assert module.ota_control_line(b"OK") == "OK"
assert module.ota_control_line(b"OK\n") == "OK"
assert module.ota_control_line(b"OK\r\n") == "OK"
for invalid_control_line in (
    b"",
    b" OK\n",
    b"OK \n",
    b"OK\r",
    b"OK\n\n",
    b"OK\x00\n",
    b"OK\x1b[31m\n",
    b"\xff",
):
    try:
        module.ota_control_line(invalid_control_line)
    except RuntimeError as error:
        assert "OTA" in str(error)
    else:
        raise AssertionError("invalid OTA control line was accepted")
assert module.ota_acknowledged_bytes(b"1024\n", 1024) == 1024
for invalid_acknowledgement in (
    b"0\n",
    b"01\n",
    b"+1\n",
    b"1 \n",
    b"1\r\n",
    b"2\n",
):
    try:
        module.ota_acknowledged_bytes(invalid_acknowledgement, 1)
    except RuntimeError as error:
        assert "acknowledgement" in str(error)
    else:
        raise AssertionError("invalid OTA acknowledgement was accepted")
assert (
    inspect.signature(module.upload_ota_container)
    .parameters["listen_port"]
    .default
    == 8266
)
ota_manifest = {
    "project": "ota-loader-test",
    "ota": {
        "host": "192.0.2.20",
        "port": 8266,
        "listenPort": 8266,
        "passwordEnv": "JH_OTA_TEST_PASSWORD",
    },
}
assert module.normalize_manifest(ota_manifest)["ota"] == ota_manifest["ota"]
for invalid_listen_port in (-1, 65536):
    try:
        module.upload_ota_container({}, b"", "", invalid_listen_port)
    except ValueError as error:
        assert "listen port" in str(error)
    else:
        raise AssertionError("invalid OTA TCP listen port was accepted")
for invalid_upload in (
    ({"address": "192.0.2.1", "port": 8266}, b""),
    ({"address": "192.0.2.1", "port": 0}, b"image"),
    ({"address": "192.0.2.1", "port": 65536}, b"image"),
):
    try:
        module.upload_ota_container(*invalid_upload, "", 8266)
    except ValueError as error:
        assert "OTA" in str(error)
    else:
        raise AssertionError("invalid OTA upload bounds were accepted")


class FakeOtaReader:
    def __init__(self, connection) -> None:
        self.connection = connection

    def readline(self, _size: int) -> bytes:
        return f"{len(self.connection.sent[-1])}\n".encode("ascii")

    def read(self, _size: int) -> bytes:
        return self.connection.final_response


class FakeOtaConnection:
    def __init__(self, final_response: bytes = b"OK") -> None:
        self.sent: list[bytes] = []
        self.closed = False
        self.final_response = final_response

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()

    def settimeout(self, _timeout: float) -> None:
        return None

    def makefile(self, mode: str):
        assert mode == "rb"
        return FakeOtaReader(self)

    def sendall(self, payload: bytes) -> None:
        self.sent.append(payload)

    def close(self) -> None:
        self.closed = True


class FakeOtaServer:
    def __init__(self, peer_ip: str, final_response: bytes = b"OK") -> None:
        self.peer_ip = peer_ip
        self.connection = FakeOtaConnection(final_response)
        self.bound = ("", 0)
        self.accept_count = 0
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.closed = True

    def setsockopt(self, *_args) -> None:
        return None

    def bind(self, address) -> None:
        self.bound = address

    def listen(self, _backlog: int) -> None:
        return None

    def settimeout(self, _timeout: float) -> None:
        return None

    def getsockname(self):
        return ("0.0.0.0", self.bound[1] or 43210)

    def accept(self):
        self.accept_count += 1
        return self.connection, (self.peer_ip, 55000)


class FakeOtaUdp:
    def __init__(self, responses: list[bytes], peer_ip: str) -> None:
        self.responses = list(responses)
        self.peer_ip = peer_ip
        self.sent: list[bytes] = []
        self.connected = None
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.closed = True

    def bind(self, _address) -> None:
        return None

    def settimeout(self, _timeout: float) -> None:
        return None

    def connect(self, address) -> None:
        self.connected = address

    def getpeername(self):
        return (self.peer_ip, 8266)

    def send(self, payload: bytes) -> None:
        self.sent.append(payload)

    def recv(self, _size: int) -> bytes:
        return self.responses.pop(0)


class FakeOtaSockets:
    def __init__(
        self,
        responses: list[bytes],
        *,
        udp_peer_ip: str = "192.0.2.32",
        tcp_peer_ip: str = "192.0.2.32",
        final_response: bytes = b"OK",
    ) -> None:
        self.server = FakeOtaServer(tcp_peer_ip, final_response)
        self.udp = FakeOtaUdp(responses, udp_peer_ip)

    def __call__(self, _family: int, socket_type: int):
        if socket_type == module.socket.SOCK_STREAM:
            return self.server
        if socket_type == module.socket.SOCK_DGRAM:
            return self.udp
        raise AssertionError(f"unexpected socket type: {socket_type}")


ota_payload = b"AUTH2 upload fixture"
ota_device = {
    "address": "ota-device.example",
    "hostname": "ota-device",
    "target": "esp32s3",
    "port": 8266,
}
device_nonce = "00112233445566778899aabbccddeeff"
client_random = bytes.fromhex("ffeeddccbbaa99887766554433221100")
ota_sockets = FakeOtaSockets([f"AUTH2 {device_nonce}\n".encode("ascii"), b"OK\n"])
with patch.object(
    module.socket, "socket", side_effect=ota_sockets
), patch.object(
    module.socket,
    "gethostbyname",
    side_effect=AssertionError("TCP peer check must reuse the connected UDP IP"),
), patch.object(module.os, "urandom", return_value=client_random):
    module.upload_ota_container(
        ota_device, ota_payload, "correct horse battery staple", 3232
    )

ota_md5 = hashlib.md5(ota_payload).hexdigest()
expected_auth2 = module.ota_auth2_tag(
    "correct horse battery staple",
    0,
    3232,
    len(ota_payload),
    ota_md5,
    device_nonce,
    client_random.hex(),
)
assert ota_sockets.udp.connected == ("ota-device.example", 8266)
assert ota_sockets.udp.sent == [
    f"0 3232 {len(ota_payload)} {ota_md5}\n".encode("ascii"),
    f"201 {client_random.hex()} {expected_auth2}\n".encode("ascii"),
]
assert ota_sockets.server.connection.sent == [ota_payload]

direct_ok_sockets = FakeOtaSockets([b"OK\n"])
with patch.object(module.socket, "socket", side_effect=direct_ok_sockets):
    try:
        module.upload_ota_container(ota_device, ota_payload, "secret", 3232)
    except RuntimeError as error:
        assert "skipped AUTH2" in str(error)
    else:
        raise AssertionError("password-protected upload accepted direct OK")
assert direct_ok_sockets.server.accept_count == 0

legacy_sockets = FakeOtaSockets([f"AUTH {device_nonce}\n".encode("ascii")])
with patch.object(module.socket, "socket", side_effect=legacy_sockets):
    try:
        module.upload_ota_container(ota_device, ota_payload, "secret", 3232)
    except RuntimeError as error:
        assert "obsolete OTA authentication" in str(error)
    else:
        raise AssertionError("legacy AUTH response enabled a downgrade")
assert legacy_sockets.server.accept_count == 0

legacy_200_sockets = FakeOtaSockets(
    [b"200 0123456789abcdef0123456789abcdef\n"]
)
with patch.object(module.socket, "socket", side_effect=legacy_200_sockets):
    try:
        module.upload_ota_container(ota_device, ota_payload, "secret", 3232)
    except RuntimeError as error:
        assert "required OTA AUTH2" in str(error)
    else:
        raise AssertionError("legacy 200 response enabled a downgrade")
assert legacy_200_sockets.server.accept_count == 0

wrong_peer_sockets = FakeOtaSockets(
    [f"AUTH2 {device_nonce}\n".encode("ascii"), b"OK\n"],
    tcp_peer_ip="192.0.2.99",
)
with patch.object(
    module.socket, "socket", side_effect=wrong_peer_sockets
), patch.object(module.os, "urandom", return_value=client_random):
    try:
        module.upload_ota_container(ota_device, ota_payload, "secret", 3232)
    except RuntimeError as error:
        assert "TCP peer differs" in str(error)
    else:
        raise AssertionError("OTA accepted a TCP peer from another address")
assert wrong_peer_sockets.server.connection.closed
assert wrong_peer_sockets.server.connection.sent == []

passwordless_sockets = FakeOtaSockets([b"OK\n"])
with patch.object(module.socket, "socket", side_effect=passwordless_sockets):
    module.upload_ota_container(ota_device, ota_payload, "", 3232)
assert passwordless_sockets.server.connection.sent == [ota_payload]

trailing_final_sockets = FakeOtaSockets([b"OK\n"], final_response=b"OK\n")
with patch.object(module.socket, "socket", side_effect=trailing_final_sockets):
    try:
        module.upload_ota_container(ota_device, ota_payload, "", 3232)
    except RuntimeError as error:
        assert "confirm OTA completion" in str(error)
    else:
        raise AssertionError("non-canonical final OTA confirmation was accepted")

noncanonical_ok_sockets = FakeOtaSockets([b"OK \n"])
with patch.object(module.socket, "socket", side_effect=noncanonical_ok_sockets):
    try:
        module.upload_ota_container(ota_device, ota_payload, "", 3232)
    except RuntimeError as error:
        assert "OTA" in str(error)
    else:
        raise AssertionError("non-canonical OTA OK response was accepted")
assert noncanonical_ok_sockets.server.accept_count == 0

invalid_challenge_sockets = FakeOtaSockets(
    [f"AUTH2 {device_nonce} extra\n".encode("ascii")]
)
with patch.object(module.socket, "socket", side_effect=invalid_challenge_sockets):
    try:
        module.upload_ota_container(ota_device, ota_payload, "secret", 3232)
    except RuntimeError as error:
        assert "invalid OTA AUTH2 challenge" in str(error)
    else:
        raise AssertionError("non-canonical AUTH2 challenge was accepted")
assert invalid_challenge_sockets.server.accept_count == 0
assert module.native_rp_upload_uses_serial_bootsel(
    {"target": "rp2040"}, "serial"
)
assert module.native_rp_upload_uses_serial_bootsel(
    {"target": "rp2350-arm"}, "uf2"
)
assert module.native_rp_upload_uses_serial_bootsel(
    {"target": "rp2350-riscv"}, None
)
assert not module.native_rp_upload_uses_serial_bootsel(
    {"target": "stm32g474"}, "serial"
)
assert not module.native_rp_upload_uses_serial_bootsel(
    {"target": "rp2040"}, "custom"
)

serial_upload_args = SimpleNamespace(
    allow_unverified_port=False,
    port=None,
)
serial_upload_config = {
    "toolchain": "cmake",
    "target": "rp2040",
    "uploadPort": "/dev/ttyACM7",
    "upload": {"strategy": "serial"},
}
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_core_runtime", serial_upload_config, 0),
), patch.object(
    module, "build_preflight_diagnostics", return_value=[]
), patch.object(
    module, "upload_port_path_exists", return_value=True
), patch.object(
    module, "verify_upload_port", return_value=0
), patch.object(
    module, "resolve_upload_port_for_tool", side_effect=lambda port: port
), patch.object(
    module, "release_port_for_upload", return_value=0
), patch.object(
    module, "command_build", return_value=0
), patch.object(
    module, "bootsel_candidate_ids", return_value=set()
), patch.object(
    module, "touch_rp_bootloader_port", return_value=0
) as touch_mock, patch.object(
    module, "command_upload_uf2", return_value=0
) as uf2_mock, patch.object(
    module, "run_cmake_target", return_value=0
) as cmake_target_mock, patch.object(
    module, "end_upload_release"
):
    assert module.command_upload(serial_upload_args) == 0

touch_mock.assert_called_once_with("/dev/ttyACM7")
uf2_mock.assert_called_once_with(
    serial_upload_args,
    build_first=False,
    bootsel_wait_s=8.0,
    excluded_bootsel_ids=set(),
)
cmake_target_mock.assert_not_called()

stale_port_config = {
    "toolchain": "cmake",
    "target": "rp2040",
    "uploadPort": "/dev/serial/by-id/usb-Fixture-stale-if00",
    "upload": {"strategy": "serial"},
}
boot_mount = Path("/media/test/RPI-RP2")
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_core_runtime", stale_port_config, 0),
), patch.object(
    module, "build_preflight_diagnostics", return_value=[]
), patch.object(
    module, "upload_port_path_exists", return_value=False
), patch.object(
    module,
    "find_single_bootsel_mount",
    return_value=(boot_mount, [str(boot_mount)]),
), patch.object(
    module, "command_upload_uf2", return_value=0
) as stale_port_uf2, patch.object(
    module, "verify_upload_port"
) as stale_port_verify, patch.object(
    module, "touch_rp_bootloader_port"
) as stale_port_touch:
    assert module.command_upload(serial_upload_args) == 0

stale_port_uf2.assert_called_once_with(serial_upload_args)
stale_port_verify.assert_not_called()
stale_port_touch.assert_not_called()

replacement_port_config = {
    "toolchain": "cmake",
    "target": "rp2040",
    "uploadPort": "/dev/serial/by-id/usb-Fixture-old-if00",
    "upload": {"strategy": "serial"},
    "identity": {
        "enabled": True,
        "usbManufacturer": "Fixture",
        "usbProduct": "Named module",
    },
}
replacement_port = "/dev/ttyACM9"
with patch.object(
    module,
    "load_config_for_action",
    return_value=(
        ROOT / "examples" / "01_core_runtime",
        replacement_port_config,
        0,
    ),
), patch.object(
    module, "build_preflight_diagnostics", return_value=[]
), patch.object(
    module, "upload_port_path_exists", return_value=False
), patch.object(
    module, "find_single_bootsel_mount", return_value=(None, [])
), patch.object(
    module,
    "select_verified_identity_port",
    return_value=(replacement_port, 0),
) as replacement_select, patch.object(
    module, "verify_upload_port", return_value=0
) as replacement_verify, patch.object(
    module, "resolve_upload_port_for_tool", side_effect=lambda port: port
), patch.object(
    module, "release_port_for_upload", return_value=0
) as replacement_release, patch.object(
    module, "command_build", return_value=0
), patch.object(
    module, "bootsel_candidate_ids", return_value=set()
), patch.object(
    module, "touch_rp_bootloader_port", return_value=0
) as replacement_touch, patch.object(
    module, "command_upload_uf2", return_value=0
) as replacement_uf2, patch.object(
    module, "end_upload_release"
):
    assert module.command_upload(serial_upload_args) == 0

replacement_select.assert_called_once_with(replacement_port_config)
replacement_verify.assert_called_once_with(
    replacement_port_config,
    replacement_port,
    allow_unverified=False,
)
replacement_release.assert_called_once_with(
    replacement_port,
    ROOT / "examples" / "01_core_runtime",
)
replacement_touch.assert_called_once_with(replacement_port)
replacement_uf2.assert_called_once_with(
    serial_upload_args,
    build_first=False,
    bootsel_wait_s=8.0,
    excluded_bootsel_ids=set(),
)

explicit_port_args = SimpleNamespace(
    allow_unverified_port=False,
    port="/dev/serial/by-id/usb-Fixture-explicit-if00",
)
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_core_runtime", stale_port_config, 0),
), patch.object(
    module, "build_preflight_diagnostics", return_value=[]
), patch.object(
    module, "upload_port_path_exists"
) as explicit_exists, patch.object(
    module, "select_verified_identity_port"
) as explicit_select, patch.object(
    module, "verify_upload_port", return_value=module.EXIT_UNSAFE_DEVICE
) as explicit_verify:
    assert (
        module.command_upload(explicit_port_args)
        == module.EXIT_UNSAFE_DEVICE
    )

explicit_exists.assert_not_called()
explicit_select.assert_not_called()
explicit_verify.assert_called_once_with(
    stale_port_config,
    explicit_port_args.port,
    allow_unverified=False,
)

monitor_config = {
    "uploadPort": "/dev/serial/by-id/usb-Fixture-old-if00",
    "identity": {
        "enabled": True,
        "usbManufacturer": "Fixture",
        "usbProduct": "Named module",
        "byIdHint": "Named_Module",
    },
}
monitor_args = SimpleNamespace(
    baud=115200,
    lock_policy="replace-own",
    port=None,
)
monitor_platform = SimpleNamespace(
    persistent_monitor_path=lambda: (
        ROOT / "vscode" / "linux" / "runtime" / "serial_persistent.py"
    )
)
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_core_runtime", monitor_config, 0),
), patch.object(
    module, "upload_port_path_exists", return_value=False
), patch.object(
    module,
    "select_verified_identity_port",
    return_value=(replacement_port, 0),
) as monitor_select, patch.object(
    module, "run_command", return_value=0
) as monitor_run, patch.object(
    module, "get_platform_adapter", return_value=monitor_platform
):
    assert module.command_monitor(monitor_args, "pico") == 0

monitor_select.assert_called_once_with(monitor_config)
monitor_command = monitor_run.call_args.args[0]
assert "--follow-identity" in monitor_command
assert "--identity-token" in monitor_command
assert "--identity-json" in monitor_command
identity_json_index = monitor_command.index("--identity-json")
monitor_identity = json.loads(monitor_command[identity_json_index + 1])
assert monitor_identity["usbManufacturer"] == "Fixture"
assert monitor_identity["usbProduct"] == "Named module"
assert monitor_command[-1] == replacement_port

explicit_monitor_args = SimpleNamespace(
    baud=115200,
    lock_policy="replace-own",
    port="/dev/serial/by-id/usb-Fixture-explicit-if00",
)
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_core_runtime", monitor_config, 0),
), patch.object(
    module, "upload_port_path_exists"
) as explicit_monitor_exists, patch.object(
    module, "select_verified_identity_port"
) as explicit_monitor_select, patch.object(
    module, "run_command", return_value=0
) as explicit_monitor_run, patch.object(
    module, "get_platform_adapter", return_value=monitor_platform
):
    assert module.command_monitor(explicit_monitor_args, "pico") == 0

explicit_monitor_exists.assert_not_called()
explicit_monitor_select.assert_not_called()
explicit_monitor_command = explicit_monitor_run.call_args.args[0]
assert "--follow-identity" not in explicit_monitor_command
assert explicit_monitor_command[-1] == explicit_monitor_args.port

assert module.managed_build_dir_allowed(
    ROOT / ".build" / "examples" / "01_core_runtime",
    ROOT / "examples" / "01_core_runtime",
)
assert not module.managed_build_dir_allowed(
    ROOT / "build_legacy",
    ROOT / "examples" / "01_core_runtime",
)

neutral_config = module.neutral_firmware_config(
    {
        "target": "rp2040",
        "board": "pico",
        "buildDir": str(ROOT / ".build" / "neutral-test"),
        "identity": {
            "enabled": True,
            "usbManufacturer": "Fixture",
            "usbProduct": "Named module",
        },
        "cmake": {
            "sourceDir": str(ROOT / "cmake" / "jh_firmware_project"),
            "targets": {"build": "project_specific_target"},
            "cache": {
                "JH_TARGET": "rp2040",
                "PICO_BOARD": "pico",
                "JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS=1",
                "JH_USB_PRODUCT": "Named module",
            },
        },
    },
    ROOT / "examples" / "01_core_runtime",
)
neutral_cache = neutral_config["cmake"]["cache"]
assert neutral_config["identity"] == {"enabled": False}
assert neutral_config["root"] == str(ROOT)
assert neutral_config["cmake"]["sourceDir"] == str(
    ROOT / "cmake" / "jh_firmware_project"
)
assert "targets" not in neutral_config["cmake"]
assert neutral_cache["JH_TARGET"] == "rp2040"
assert neutral_cache["PICO_BOARD"] == "pico"
assert neutral_cache["JH_PROJECT_DIR"] == str(module.neutral_firmware_source_dir())
assert neutral_cache["JH_MODULE_NAME"] == "neutral_identity"
assert "JH_EXTRA_DEFINES" not in neutral_cache
assert "JH_USB_PRODUCT" not in neutral_cache
assert Path(neutral_config["artifacts"]["uf2"]).name == "firmware.uf2"


class FakeSerialException(Exception):
    pass


class FakeTouch:
    def __init__(self, kwargs: dict) -> None:
        self.kwargs = kwargs
        self.dtr_changes: list[bool] = []

    def __enter__(self) -> "FakeTouch":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        return None

    @property
    def dtr(self) -> bool:
        return self.dtr_changes[-1] if self.dtr_changes else False

    @dtr.setter
    def dtr(self, value: bool) -> None:
        self.dtr_changes.append(value)


created: list[FakeTouch] = []


def serial_factory(**kwargs):
    touch = FakeTouch(kwargs)
    created.append(touch)
    return touch


fake_serial = SimpleNamespace(
    Serial=serial_factory,
    SerialException=FakeSerialException,
)

with patch.dict(sys.modules, {"serial": fake_serial}), patch.object(
    module.time, "sleep", return_value=None
):
    status = module.touch_rp_bootloader_port("/dev/ttyACM7")

assert status == 0
assert len(created) == 1
assert created[0].kwargs["port"] == "/dev/ttyACM7"
assert created[0].kwargs["baudrate"] == 1200
assert created[0].dtr_changes == [True, False]


class DisconnectingTouch(FakeTouch):
    @FakeTouch.dtr.setter
    def dtr(self, value: bool) -> None:
        self.dtr_changes.append(value)
        if not value:
            raise OSError(errno.EIO, "device entered BOOTSEL")


def disconnecting_serial_factory(**kwargs):
    return DisconnectingTouch(kwargs)


disconnecting_serial = SimpleNamespace(
    Serial=disconnecting_serial_factory,
    SerialException=FakeSerialException,
)
with patch.dict(sys.modules, {"serial": disconnecting_serial}), patch.object(
    module.time, "sleep", return_value=None
):
    assert module.touch_rp_bootloader_port("/dev/ttyACM7") == 0

serial_style_config = {
    "toolchain": "cmake",
    "target": "rp2040",
    "uploadPort": "/dev/serial/by-id/usb-Fixture-if00",
    "upload": {"strategy": "serial"},
}
upload_args = SimpleNamespace(port=None, allow_unverified_port=False)
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_core_runtime", serial_style_config, 0),
), patch.object(
    module, "build_preflight_diagnostics", return_value=[]
), patch.object(
    module, "upload_port_path_exists", return_value=True
), patch.object(
    module, "verify_upload_port", return_value=0
), patch.object(
    module,
    "resolve_upload_port_for_tool",
    return_value="/dev/ttyACM7",
), patch.object(
    module, "release_port_for_upload", return_value=0
), patch.object(
    module, "command_build", return_value=0
) as build, patch.object(
    module, "bootsel_candidate_ids", return_value={"block:/dev/sdd1"}
), patch.object(
    module, "touch_rp_bootloader_port", return_value=0
) as touch, patch.object(
    module, "command_upload_uf2", return_value=0
) as upload_uf2, patch.object(
    module, "run_cmake_target"
) as cmake_upload, patch.object(
    module, "end_upload_release"
):
    status = module.command_upload(upload_args)

assert status == 0
build.assert_called_once()
touch.assert_called_once_with("/dev/ttyACM7")
upload_uf2.assert_called_once_with(
    upload_args,
    build_first=False,
    bootsel_wait_s=8.0,
    excluded_bootsel_ids={"block:/dev/sdd1"},
)
cmake_upload.assert_not_called()

with patch.object(
    module,
    "find_single_bootsel_mount",
    side_effect=[(None, []), (boot_mount, [str(boot_mount)])],
), patch.object(module.time, "sleep", return_value=None):
    mount, candidates = module.wait_for_single_bootsel_mount(1.0)

assert mount == boot_mount
assert candidates == [str(boot_mount)]

ambiguous = ["/media/test/RPI-RP2", "/media/test/RP2350"]
with patch.object(
    module, "find_single_bootsel_mount", return_value=(None, ambiguous)
):
    mount, candidates = module.wait_for_single_bootsel_mount(1.0)

assert mount is None
assert candidates == ambiguous

old_mount = Path("/media/test/RP2350")
new_mount = Path("/media/test/RPI-RP2")
blocks = [
    {
        "path": "/dev/sdd1",
        "label": "RP2350",
        "fstype": "vfat",
        "mountpoints": [str(old_mount)],
    },
    {
        "path": "/dev/sde1",
        "label": "RPI-RP2",
        "fstype": "vfat",
        "mountpoints": [str(new_mount)],
    },
]
with patch.object(module, "find_bootsel_blocks", return_value=blocks), patch.object(
    module, "find_bootsel_mounts", return_value=[old_mount, new_mount]
):
    assert module.bootsel_candidate_ids() == {
        "block:/dev/sdd1",
        "block:/dev/sde1",
    }
    mount, candidates = module.find_single_bootsel_mount({"block:/dev/sdd1"})

assert mount == new_mount
assert candidates == [str(new_mount)]

with TemporaryDirectory() as temporary_dir:
    build_dir = Path(temporary_dir)
    empty_config = {"cmake": {"cache": {}}}
    assert "-UJH_EXTRA_DEFINES" in module.removed_cmake_cache_args(
        empty_config, build_dir
    )

    configured = {
        "cmake": {
            "cache": {
                "JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS=1",
                "JH_CUSTOM_OPTION": "enabled",
            }
        }
    }
    module.record_cmake_cache_keys(configured, build_dir)
    assert module.removed_cmake_cache_args(configured, build_dir) == []

    removed = module.removed_cmake_cache_args(empty_config, build_dir)
    assert "-UJH_EXTRA_DEFINES" in removed
    assert "-UJH_CUSTOM_OPTION" in removed

    payload = b"native-rp-ota-payload"
    header = bytearray(160)
    header[:8] = b"JHOTA1\r\n"
    struct.pack_into("<HHHHIIII", header, 8, 1, 160, 1, 0, 0x4000, len(payload), 7, 0)
    header[32:64] = hashlib.sha256(payload).digest()
    header[64:69] = b"test\0"
    unsigned = build_dir / "firmware.ota"
    signed = build_dir / "firmware.signed.ota"
    unsigned.write_bytes(header + payload)
    container = module.sign_ota_container(unsigned, "secret", signed)
    key = hashlib.md5(b"secret").hexdigest().encode("ascii")
    assert container[96:128] == hmac.new(key, container[:96], hashlib.sha256).digest()
    assert signed.read_bytes() == container

device = module.parse_ota_discovery_response(
    b"JHOTA 1 pico-kitchen rp2040 8266 1007616 12 2",
    ("192.0.2.10", 8266),
)
assert device == {
    "address": "192.0.2.10",
    "hostname": "pico-kitchen",
    "target": "rp2040",
    "port": 8266,
    "slotSize": 1007616,
    "generation": 12,
    "bootMode": 2,
}
assert module.parse_ota_discovery_response(b"not ota", ("192.0.2.1", 1)) is None
for invalid_discovery in (
    b" JHOTA 1 pico-kitchen rp2040 8266 1007616 12 2\n",
    b"JHOTA  1 pico-kitchen rp2040 8266 1007616 12 2\n",
    b"JHOTA 1 pico-kitchen rp2040 08266 1007616 12 2\n",
    b"JHOTA 1 pico-kitchen rp2040 8266 1007616 -1 2\n",
    b"JHOTA 1 pico-kitchen rp2040 8266 1007616 12 5\n",
    b"JHOTA 1 pico\x1b[31m rp2040 8266 1007616 12 2\n",
):
    assert module.parse_ota_discovery_response(
        invalid_discovery, ("192.0.2.1", 8266)
    ) is None

discover_args = SimpleNamespace(host=None, json=False)
with patch.object(
    module,
    "load_config_for_action",
    return_value=(
        ROOT,
        {
            "ota": {
                "host": "192.0.2.20",
                "broadcast": "192.0.2.255",
                "port": 8266,
            }
        },
        0,
    ),
), patch.object(
    module, "discover_ota_devices", return_value=[device]
) as discover_mock, patch.object(module, "print_ota_devices") as print_mock:
    assert module.command_ota_discover(discover_args) == 0

discover_mock.assert_called_once_with(8266, "192.0.2.20")
print_mock.assert_called_once_with([device], as_json=False)

configured = module.choose_ota_device(
    [],
    {"target": "rp2350-arm"},
    {"host": "192.0.2.20", "hostname": "pico-office", "port": 9000},
    SimpleNamespace(host=None, interactive=False),
)
assert configured == {
    "address": "192.0.2.20",
    "hostname": "pico-office",
    "target": "rp2350-arm",
    "port": 9000,
}

with TemporaryDirectory(prefix="jh-esp-ota-") as temporary_dir:
    esp_project = Path(temporary_dir)
    esp_build = esp_project / ".build"
    esp_build.mkdir()
    esp_application = esp_build / "application.bin"
    esp_payload = b"\xe9raw-esp-idf-application-image"
    esp_application.write_bytes(esp_payload)
    esp_manifest = {
        "integration": {
            "resolvedFeatures": ["HAL_ENABLE_FREERTOS", "HAL_ENABLE_OTA"]
        },
        "flashImages": [
            {
                "offset": "0x20000",
                "path": "application.bin",
                "size": len(esp_payload),
                "sha256": hashlib.sha256(esp_payload).hexdigest(),
            }
        ],
    }
    esp_config = {
        "toolchain": "esp-idf",
        "target": "esp32s3",
        "board": "waveshare-esp32-s3-zero",
        "buildDir": str(esp_build),
        "ota": {
            "host": "192.0.2.32",
            "port": 8266,
            "allowEmptyPassword": True,
        },
    }
    with patch.object(
        module,
        "validate_esp_idf_artifact_manifest",
        return_value=(
            esp_manifest,
            {"applicationBinary": esp_application},
            [esp_application],
        ),
    ):
        source, image = module.esp_idf_ota_image(esp_config, esp_project)
    assert source == esp_application
    assert image == esp_payload

    invalid_manifest = json.loads(json.dumps(esp_manifest))
    invalid_manifest["flashImages"][0]["sha256"] = "0" * 64
    with patch.object(
        module,
        "validate_esp_idf_artifact_manifest",
        return_value=(
            invalid_manifest,
            {"applicationBinary": esp_application},
            [esp_application],
        ),
    ):
        try:
            module.esp_idf_ota_image(esp_config, esp_project)
        except ValueError as error:
            assert "differs from its manifest" in str(error)
        else:
            raise AssertionError("ESP OTA accepted an application hash mismatch")

    esp_upload_args = SimpleNamespace(host=None, interactive=False)
    with patch.object(
        module,
        "load_config_for_action",
        return_value=(esp_project, esp_config, 0),
    ), patch.object(
        module, "command_build", return_value=0
    ) as esp_build_mock, patch.object(
        module,
        "esp_idf_ota_image",
        return_value=(esp_application, esp_payload),
    ), patch.object(
        module, "sign_ota_container"
    ) as rp_sign_mock, patch.object(
        module, "upload_ota_container"
    ) as esp_upload_mock, patch.object(
        module, "print_memory_map_overview"
    ):
        assert module.command_upload_ota(esp_upload_args) == 0

    esp_build_mock.assert_called_once()
    rp_sign_mock.assert_not_called()
    esp_upload_mock.assert_called_once_with(
        {
            "address": "192.0.2.32",
            "hostname": "192.0.2.32",
            "target": "esp32s3",
            "port": 8266,
        },
        esp_payload,
        "",
        8266,
    )
