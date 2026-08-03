#!/usr/bin/env python3
"""Deterministic checks for Windows BOOTSEL discovery and durable UF2 upload."""

from __future__ import annotations

from contextlib import redirect_stderr
import io
import os
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT))

from vscode.runtime import jh_vscode as runtime
from vscode.runtime.platform_api import set_platform_adapter
from vscode.windows.runtime.adapter import WindowsPlatformAdapter


def volume_record(
    guid: str,
    mount: Path,
    *,
    label: str = "RPI-RP2",
    filesystem: str = "FAT",
) -> dict[str, object]:
    return {
        "path": guid,
        "volumeGuid": guid,
        "label": label,
        "fstype": filesystem,
        "mountpoints": [str(mount)],
    }


def uf2_bytes(block_count: int = 2) -> bytes:
    artifact = bytearray()
    for block_number in range(block_count):
        block = bytearray(runtime.UF2_BLOCK_SIZE)
        struct.pack_into(
            "<IIIIIIII",
            block,
            0,
            runtime.UF2_MAGIC_START0,
            runtime.UF2_MAGIC_START1,
            0x00002000,
            0x10000000 + block_number * 256,
            256,
            block_number,
            block_count,
            0xE48BFF56,
        )
        block[32:288] = bytes([block_number + 1]) * 256
        struct.pack_into("<I", block, 508, runtime.UF2_MAGIC_END)
        artifact.extend(block)
    return bytes(artifact)


class FailingCopyAdapter(WindowsPlatformAdapter):
    def __init__(self, records, error: OSError):
        super().__init__(lambda: [], lambda: list(records))
        self.error = error

    def durable_copy(self, source: Path, destination: Path) -> None:
        del source, destination
        raise self.error


with TemporaryDirectory(prefix="jh windows bootsel ") as temporary:
    root = Path(temporary)
    rp2040_mount = root / "RP2040 drive"
    rp2350_mount = root / "RP2350 drive"
    rp2040_mount.mkdir()
    rp2350_mount.mkdir()
    rp2040_guid = "\\\\?\\Volume{11111111-1111-1111-1111-111111111111}\\"
    rp2350_guid = "\\\\?\\Volume{22222222-2222-2222-2222-222222222222}\\"
    records: list[dict[str, object]] = []
    adapter = WindowsPlatformAdapter(lambda: [], lambda: list(records))

    assert adapter.find_bootsel_blocks(runtime.BOOTSEL_LABELS) == []
    assert adapter.find_bootsel_mounts(runtime.BOOTSEL_LABELS) == []

    records.extend(
        [
            volume_record(
                "\\\\?\\Volume{33333333-3333-3333-3333-333333333333}\\",
                root,
                label="NOT-BOOTSEL",
            ),
            volume_record(
                "\\\\?\\Volume{44444444-4444-4444-4444-444444444444}\\",
                root,
                filesystem="NTFS",
            ),
            volume_record(rp2040_guid, rp2040_mount),
        ]
    )
    blocks = adapter.find_bootsel_blocks(runtime.BOOTSEL_LABELS)
    assert len(blocks) == 1
    assert blocks[0]["path"] == rp2040_guid
    assert blocks[0]["volumeGuid"] == rp2040_guid
    assert blocks[0]["fstype"] == "FAT"
    assert adapter.find_bootsel_mounts(runtime.BOOTSEL_LABELS) == [rp2040_mount]

    set_platform_adapter(adapter)
    try:
        snapshot = runtime.bootsel_candidate_ids()
        assert snapshot == {f"block:{rp2040_guid}"}
        mount, candidates = runtime.find_single_bootsel_mount()
        assert mount == rp2040_mount
        assert candidates == [str(rp2040_mount)]

        records.append(
            volume_record(
                rp2350_guid,
                rp2350_mount,
                label="RP2350",
                filesystem="fat32",
            )
        )
        mount, candidates = runtime.find_single_bootsel_mount()
        assert mount is None
        assert candidates == sorted([str(rp2040_mount), str(rp2350_mount)])

        mount, candidates = runtime.find_single_bootsel_mount(
            selected_id=rp2350_guid
        )
        assert mount == rp2350_mount
        assert candidates == [str(rp2350_mount)]
        mount, candidates = runtime.find_single_bootsel_mount(
            selected_id=str(rp2040_mount)
        )
        assert mount == rp2040_mount
        assert candidates == [str(rp2040_mount)]

        upload_args = SimpleNamespace(
            port=None,
            allow_unverified_port=False,
        )
        explicit_config = {
            "toolchain": "cmake",
            "target": "rp2040",
            "uploadPort": "COM17",
            "upload": {
                "strategy": "serial",
                "bootselVolume": rp2350_guid,
            },
        }
        with patch.object(
            runtime,
            "load_config_for_action",
            return_value=(root, explicit_config, 0),
        ), patch.object(
            runtime, "build_preflight_diagnostics", return_value=[]
        ), patch.object(
            runtime, "command_upload_uf2", return_value=0
        ) as explicit_upload, patch.object(
            runtime, "touch_rp_bootloader_port"
        ) as touch:
            assert runtime.command_upload(upload_args) == 0
        explicit_upload.assert_called_once_with(upload_args)
        touch.assert_not_called()

        mount, candidates = runtime.find_single_bootsel_mount(snapshot)
        assert mount == rp2350_mount
        assert candidates == [str(rp2350_mount)]

        old_block = blocks[0]
        records.clear()
        assert (
            adapter.mount_bootsel_block(
                old_block,
                runtime.BOOTSEL_LABELS,
                runtime.bootsel_mountpoint,
            )
            is None
        )
    finally:
        set_platform_adapter(None)

    source = root / "firmware.uf2"
    source.write_bytes(uf2_bytes())
    destination = root / "copied.uf2"
    with patch.object(os, "fsync", wraps=os.fsync) as durable_flush:
        adapter.durable_copy(source, destination)
    assert destination.read_bytes() == source.read_bytes()
    durable_flush.assert_called_once()
    assert runtime.validate_uf2_artifact(source) is None

    truncated = root / "truncated.uf2"
    truncated.write_bytes(uf2_bytes()[:-1])
    assert "not a multiple" in str(runtime.validate_uf2_artifact(truncated))
    missing_block = root / "missing-block.uf2"
    missing_block.write_bytes(uf2_bytes()[: runtime.UF2_BLOCK_SIZE])
    assert "declares 2 blocks but contains 1" in str(
        runtime.validate_uf2_artifact(missing_block)
    )
    invalid_magic = root / "invalid-magic.uf2"
    invalid_magic.write_bytes(bytes(runtime.UF2_BLOCK_SIZE))
    assert "invalid UF2 magic" in str(runtime.validate_uf2_artifact(invalid_magic))

    records[:] = [volume_record(rp2040_guid, rp2040_mount)]
    set_platform_adapter(adapter)
    try:
        with patch.object(runtime, "print_memory_map_overview"), patch.object(
            runtime.time, "sleep"
        ):
            assert runtime.upload_uf2_artifact(source, {}, root) == 0
        assert (rp2040_mount / source.name).read_bytes() == source.read_bytes()

        incomplete_error = io.StringIO()
        with redirect_stderr(incomplete_error):
            assert (
                runtime.upload_uf2_artifact(truncated, {}, root)
                == runtime.EXIT_UPLOAD
            )
        assert "invalid or incomplete UF2 artifact" in incomplete_error.getvalue()

        records.clear()
        missing_error = io.StringIO()
        with redirect_stderr(missing_error):
            assert runtime.upload_uf2_artifact(source, {}, root) == runtime.EXIT_UNSAFE_DEVICE
        assert "BOOTSEL drive not found" in missing_error.getvalue()

        records.extend(
            [
                volume_record(rp2040_guid, rp2040_mount),
                volume_record(rp2350_guid, rp2350_mount, label="RPI-RP2350"),
            ]
        )
        multiple_error = io.StringIO()
        with redirect_stderr(multiple_error):
            assert runtime.upload_uf2_artifact(source, {}, root) == runtime.EXIT_UNSAFE_DEVICE
        assert "multiple BOOTSEL drives" in multiple_error.getvalue()

        with patch.object(runtime, "print_memory_map_overview"), patch.object(
            runtime.time, "sleep"
        ):
            assert (
                runtime.upload_uf2_artifact(
                    source,
                    {},
                    root,
                    selected_bootsel_id=rp2350_guid,
                )
                == 0
            )
    finally:
        set_platform_adapter(None)

    for error, expected_message in (
        (PermissionError("read-only media"), "read-only or access was denied"),
        (FileNotFoundError("volume removed"), "disappeared during UF2 copy"),
        (OSError("short write"), "UF2 copy failed"),
    ):
        failing = FailingCopyAdapter(
            [volume_record(rp2040_guid, rp2040_mount)],
            error,
        )
        set_platform_adapter(failing)
        try:
            copy_error = io.StringIO()
            with redirect_stderr(copy_error):
                assert runtime.upload_uf2_artifact(source, {}, root) == runtime.EXIT_UPLOAD
            assert expected_message in copy_error.getvalue()
        finally:
            set_platform_adapter(None)

    local_project = root / "local project"
    local_vscode = local_project / ".vscode"
    local_vscode.mkdir(parents=True)
    (local_vscode / "settings.json").write_text("{}", encoding="utf-8")
    (local_vscode / "jaszczurhal.local.json").write_text(
        '{"bootselVolume": "E:\\\\"}',
        encoding="utf-8",
    )
    local_config = runtime.load_project_config(local_project)
    assert local_config["upload"]["bootselVolume"] == "E:\\"
    assert (
        runtime.build_parser().parse_args(
            ["upload-uf2", "--bootsel-volume", rp2040_guid]
        ).bootsel_volume
        == rp2040_guid
    )


class WindowsStyleDisconnect(Exception):
    pass


class DisconnectingSerial:
    def __init__(self, **kwargs):
        self.kwargs = kwargs

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    @property
    def dtr(self):
        return False

    @dtr.setter
    def dtr(self, value: bool) -> None:
        if not value:
            raise WindowsStyleDisconnect("ClearCommError failed after detach")


touch_adapter = WindowsPlatformAdapter(lambda: [], lambda: [])
set_platform_adapter(touch_adapter)
try:
    fake_serial = SimpleNamespace(
        Serial=lambda **kwargs: DisconnectingSerial(**kwargs),
        SerialException=WindowsStyleDisconnect,
    )
    with patch.dict(sys.modules, {"serial": fake_serial}), patch.object(
        runtime.time, "sleep"
    ):
        assert runtime.touch_rp_bootloader_port("COM17") == 0
finally:
    set_platform_adapter(None)


if sys.platform == "win32":
    native_adapter = WindowsPlatformAdapter()
    native_volumes = native_adapter._winapi_volumes()
    for native_volume in native_volumes:
        assert native_volume["path"].startswith("\\\\?\\Volume{")
        assert native_volume["mountpoints"]
        assert "label" in native_volume
        assert "fstype" in native_volume
