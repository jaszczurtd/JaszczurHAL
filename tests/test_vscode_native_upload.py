#!/usr/bin/env python3
"""Unit checks for native RP 1200-bps touch and BOOTSEL wait helpers."""

from __future__ import annotations

import errno
import importlib.util
import hashlib
import hmac
import inspect
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(sys.argv[1]).resolve()
RUNTIME = ROOT / "vscode" / "linux" / "runtime" / "jh_vscode.py"

spec = importlib.util.spec_from_file_location("jh_vscode_native_upload_test", RUNTIME)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {RUNTIME}")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

assert module.NATIVE_RP_TARGETS == {"rp2040", "rp2350-arm", "rp2350-riscv"}
assert "upload-ota" in module.SUPPORTED_ACTIONS
assert "ota-discover" in module.SUPPORTED_ACTIONS
assert "clear-identity" in module.SUPPORTED_ACTIONS
assert module.UF2_TARGETS == module.NATIVE_RP_TARGETS
assert module.ota_listen_port({}) == 8266
assert module.ota_listen_port({"listenPort": 0}) == 0
assert module.ota_listen_port({"listenPort": 9000}) == 9000
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
    return_value=(ROOT / "examples" / "01_blink", serial_upload_config, 0),
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
    return_value=(ROOT / "examples" / "01_blink", stale_port_config, 0),
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
        ROOT / "examples" / "01_blink",
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
    ROOT / "examples" / "01_blink",
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
    return_value=(ROOT / "examples" / "01_blink", stale_port_config, 0),
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
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_blink", monitor_config, 0),
), patch.object(
    module, "upload_port_path_exists", return_value=False
), patch.object(
    module,
    "select_verified_identity_port",
    return_value=(replacement_port, 0),
) as monitor_select, patch.object(
    module, "run_command", return_value=0
) as monitor_run:
    assert module.command_monitor(monitor_args, "pico") == 0

monitor_select.assert_called_once_with(monitor_config)
monitor_command = monitor_run.call_args.args[0]
assert "--follow-identity" in monitor_command
assert "--identity-token" in monitor_command
assert monitor_command[-1] == replacement_port

explicit_monitor_args = SimpleNamespace(
    baud=115200,
    lock_policy="replace-own",
    port="/dev/serial/by-id/usb-Fixture-explicit-if00",
)
with patch.object(
    module,
    "load_config_for_action",
    return_value=(ROOT / "examples" / "01_blink", monitor_config, 0),
), patch.object(
    module, "upload_port_path_exists"
) as explicit_monitor_exists, patch.object(
    module, "select_verified_identity_port"
) as explicit_monitor_select, patch.object(
    module, "run_command", return_value=0
) as explicit_monitor_run:
    assert module.command_monitor(explicit_monitor_args, "pico") == 0

explicit_monitor_exists.assert_not_called()
explicit_monitor_select.assert_not_called()
explicit_monitor_command = explicit_monitor_run.call_args.args[0]
assert "--follow-identity" not in explicit_monitor_command
assert explicit_monitor_command[-1] == explicit_monitor_args.port

assert module.managed_build_dir_allowed(
    ROOT / ".build" / "examples" / "01_blink",
    ROOT / "examples" / "01_blink",
)
assert not module.managed_build_dir_allowed(
    ROOT / "build_legacy",
    ROOT / "examples" / "01_blink",
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
    ROOT / "examples" / "01_blink",
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
    return_value=(ROOT / "examples" / "01_blink", serial_style_config, 0),
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
