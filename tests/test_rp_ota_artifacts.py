#!/usr/bin/env python3
"""Regression checks for native RP OTA artifact generation."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
SCRIPT = ROOT / "scripts" / "rp_ota_artifacts.py"

spec = importlib.util.spec_from_file_location("rp_ota_artifacts_test", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {SCRIPT}")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

sys.path.insert(0, str(ROOT))
from vscode.runtime import jh_vscode as vscode_runtime

FLASH_BASE = 0x10000000
FAMILY_ID = 0xE48BFF56
ABSOLUTE_FAMILY_ID = 0xE48BFF57
RP2350_ARM_FAMILY_ID = 0xE48BFF59


def make_block(
    address: int,
    fill: int,
    block_number: int,
    block_count: int,
    family_id: int = FAMILY_ID,
) -> bytes:
    block = bytearray(module.UF2_BLOCK_SIZE)
    struct.pack_into(
        "<IIIIIIII",
        block,
        0,
        module.UF2_MAGIC_START0,
        module.UF2_MAGIC_START1,
        0x00002000,
        address,
        module.UF2_PAGE_SIZE,
        block_number,
        block_count,
        family_id,
    )
    block[32 : 32 + module.UF2_PAGE_SIZE] = bytes([fill]) * module.UF2_PAGE_SIZE
    struct.pack_into("<I", block, 508, module.UF2_MAGIC_END)
    return bytes(block)


def write_uf2(
    path: Path,
    pages: list[tuple[int, int]],
    family_id: int = FAMILY_ID,
) -> None:
    path.write_bytes(
        b"".join(
            make_block(address, fill, index, len(pages), family_id)
            for index, (address, fill) in enumerate(pages)
        )
    )


def blocks_by_address(path: Path) -> dict[int, bytearray]:
    return {
        struct.unpack_from("<I", block, 12)[0]: block
        for block in module.uf2_blocks(path)
    }


with TemporaryDirectory() as temporary_dir:
    temporary = Path(temporary_dir)
    boot = temporary / "boot.uf2"
    application = temporary / "application.uf2"
    merged = temporary / "merged.uf2"

    write_uf2(
        boot,
        [
            (FLASH_BASE, 0x11),
            (FLASH_BASE + 0x2000, 0x22),
        ],
    )
    write_uf2(
        application,
        [
            (FLASH_BASE + 0x4000, 0x44),
            (FLASH_BASE + 0x4100, 0x45),
        ],
    )

    module.merge_uf2(boot, application, merged)
    merged_blocks = blocks_by_address(merged)
    merged_addresses = sorted(merged_blocks)

    expected_sector_zero = {
        FLASH_BASE + offset
        for offset in range(0, module.FLASH_SECTOR_ERASE_SIZE, module.UF2_PAGE_SIZE)
    }
    expected_sector_two = {
        FLASH_BASE + 0x2000 + offset
        for offset in range(0, module.FLASH_SECTOR_ERASE_SIZE, module.UF2_PAGE_SIZE)
    }
    assert expected_sector_zero <= set(merged_addresses)
    assert expected_sector_two <= set(merged_addresses)
    assert FLASH_BASE + 0x1000 not in merged_blocks
    assert FLASH_BASE + 0x4200 not in merged_blocks

    assert merged_blocks[FLASH_BASE][32:288] == bytes([0x11]) * module.UF2_PAGE_SIZE
    assert merged_blocks[FLASH_BASE + 0x2000][32:288] == bytes(
        [0x22]
    ) * module.UF2_PAGE_SIZE
    assert merged_blocks[FLASH_BASE + 0x4000][32:288] == bytes(
        [0x44]
    ) * module.UF2_PAGE_SIZE
    assert merged_blocks[FLASH_BASE + 0x4100][32:288] == bytes(
        [0x45]
    ) * module.UF2_PAGE_SIZE

    dummy = merged_blocks[FLASH_BASE + 0x2100]
    assert dummy[32:508] == bytes(476)
    assert struct.unpack_from("<I", dummy, 8)[0] == 0x00002000
    assert struct.unpack_from("<I", dummy, 28)[0] == FAMILY_ID

    block_count = len(merged_addresses)
    for index, address in enumerate(merged_addresses):
        block = merged_blocks[address]
        assert struct.unpack_from("<II", block, 20) == (index, block_count)

    mixed_boot = temporary / "mixed-boot.uf2"
    mixed_application = temporary / "mixed-application.uf2"
    mixed_merged = temporary / "mixed-merged.uf2"
    write_uf2(
        mixed_boot,
        [(FLASH_BASE + 0x8000, 0x81), (FLASH_BASE + 0x8100, 0x82)],
        ABSOLUTE_FAMILY_ID,
    )
    write_uf2(
        mixed_application,
        [(FLASH_BASE + 0xC000, 0xC1), (FLASH_BASE + 0xC100, 0xC2)],
        RP2350_ARM_FAMILY_ID,
    )
    module.merge_uf2(mixed_boot, mixed_application, mixed_merged)
    mixed_blocks = module.uf2_blocks(mixed_merged)
    mixed_count = len(mixed_blocks)
    assert {
        struct.unpack_from("<I", block, 28)[0] for block in mixed_blocks
    } == {ABSOLUTE_FAMILY_ID, RP2350_ARM_FAMILY_ID}
    assert all(
        struct.unpack_from("<I", block, 24)[0] == mixed_count
        for block in mixed_blocks
    )
    assert vscode_runtime.validate_uf2_artifact(mixed_merged) is None

    copy_calls: list[tuple[Path, Path]] = []
    bootsel_mount = temporary / "bootsel"
    bootsel_mount.mkdir()
    upload_adapter = SimpleNamespace(
        durable_copy=lambda source, destination: copy_calls.append(
            (source, destination)
        )
    )
    with patch.object(
        vscode_runtime,
        "find_single_bootsel_mount",
        return_value=(bootsel_mount, [str(bootsel_mount)]),
    ), patch.object(
        vscode_runtime,
        "get_platform_adapter",
        return_value=upload_adapter,
    ), patch.object(
        vscode_runtime,
        "print_memory_map_overview",
    ), patch.object(
        vscode_runtime.time,
        "sleep",
    ):
        assert vscode_runtime.upload_uf2_artifact(
            mixed_merged,
            {},
            temporary,
        ) == 0
    assert copy_calls == [(mixed_merged, bootsel_mount / mixed_merged.name)]

    duplicated_sequence = temporary / "mixed-duplicate.uf2"
    duplicate_blocks = module.uf2_blocks(mixed_merged)
    struct.pack_into("<I", duplicate_blocks[-1], 20, 0)
    duplicated_sequence.write_bytes(b"".join(duplicate_blocks))
    duplicate_error = vscode_runtime.validate_uf2_artifact(duplicated_sequence)
    assert duplicate_error is not None
    assert "duplicates global sequence number 0" in duplicate_error

    conflicting = temporary / "conflicting.uf2"
    write_uf2(conflicting, [(FLASH_BASE, 0x99)])
    try:
        module.merge_uf2(boot, conflicting, merged)
    except ValueError as error:
        assert "overlapping UF2 block" in str(error)
    else:
        raise AssertionError("conflicting UF2 overlap was accepted")


def make_e10_block(block_number: int, block_count: int) -> bytes:
    """picotool's RP2350-E10 marker: absolute family, 0xef payload, RP2 ignore tag."""
    block = bytearray(module.UF2_BLOCK_SIZE)
    struct.pack_into(
        "<IIIIIIII",
        block,
        0,
        module.UF2_MAGIC_START0,
        module.UF2_MAGIC_START1,
        0x00002000 | module.UF2_FLAG_EXTENSION_FLAGS_PRESENT,
        0x10FFFF00,
        module.UF2_PAGE_SIZE,
        block_number,
        block_count,
        ABSOLUTE_FAMILY_ID,
    )
    block[32 : 32 + module.UF2_PAGE_SIZE] = bytes([0xEF]) * module.UF2_PAGE_SIZE
    struct.pack_into(
        "<I", block, 32 + module.UF2_PAGE_SIZE, module.UF2_EXTENSION_RP2_IGNORE_BLOCK
    )
    struct.pack_into("<I", block, 508, module.UF2_MAGIC_END)
    return bytes(block)


def write_rp2350_uf2(path: Path, pages: list[tuple[int, int]]) -> None:
    """Layout picotool writes for RP2350 images: E10 marker first, then the pages."""
    path.write_bytes(
        make_e10_block(0, 2)
        + b"".join(
            make_block(address, fill, index, len(pages), RP2350_ARM_FAMILY_ID)
            for index, (address, fill) in enumerate(pages)
        )
    )


def expect_range_error(image: Path, start: int, end: int, fragment: str) -> None:
    try:
        module.check_range(image, start, end)
    except ValueError as error:
        assert fragment in str(error), str(error)
    else:
        raise AssertionError(f"check_range accepted {image.name} for {start:#x}..{end:#x}")


with TemporaryDirectory() as temporary_dir:
    temporary = Path(temporary_dir)

    # RP2350 OTA merge: the E10 marker is not flash content. Padding may only
    # fill the real sectors and must stop at the last real page.
    boot = temporary / "rp2350-boot.uf2"
    application = temporary / "rp2350-application.uf2"
    merged = temporary / "rp2350-merged.uf2"
    write_rp2350_uf2(boot, [(FLASH_BASE, 0x11), (FLASH_BASE + 0x2000, 0x22)])
    write_rp2350_uf2(application, [(FLASH_BASE + 0x4000, 0x44), (FLASH_BASE + 0x4100, 0x45)])
    module.merge_uf2(boot, application, merged)
    rp2350_blocks = blocks_by_address(merged)
    marker_sector = [
        address for address in rp2350_blocks if 0x10FFF000 <= address < 0x11000000
    ]
    assert marker_sector == [0x10FFFF00], [hex(address) for address in marker_sector]
    marker = rp2350_blocks[0x10FFFF00]
    assert module.is_ignored_block(marker)
    assert marker[32:288] == bytes([0xEF]) * module.UF2_PAGE_SIZE
    assert FLASH_BASE + 0x2100 in rp2350_blocks
    assert FLASH_BASE + 0x4200 not in rp2350_blocks, "tail of the last real sector was padded"
    assert not any(
        module.is_ignored_block(block)
        for address, block in rp2350_blocks.items()
        if address != 0x10FFFF00
    )
    assert vscode_runtime.validate_uf2_artifact(merged) is None

    # check-range: the page layout a region override must produce.
    slot_start = FLASH_BASE + 0x4000
    slot_end = FLASH_BASE + 0x6000
    in_slot = temporary / "in-slot.uf2"
    write_rp2350_uf2(in_slot, [(slot_start, 0x01), (slot_end - 0x100, 0x02)])
    module.check_range(in_slot, slot_start, slot_end)

    rp2040_in_slot = temporary / "rp2040-in-slot.uf2"
    write_uf2(rp2040_in_slot, [(slot_start, 0x01), (slot_start + 0x100, 0x02)])
    module.check_range(rp2040_in_slot, slot_start, slot_end)

    # A lost override links the image at the start of flash.
    at_flash_start = temporary / "at-flash-start.uf2"
    write_uf2(at_flash_start, [(FLASH_BASE, 0x01), (FLASH_BASE + 0x100, 0x02)])
    expect_range_error(at_flash_start, slot_start, slot_end, "expected 0x10004000")

    # A region longer than requested lets the image run into storage.
    past_end = temporary / "past-end.uf2"
    write_uf2(past_end, [(slot_start, 0x01), (slot_end, 0x02)])
    expect_range_error(past_end, slot_start, slot_end, "past the region end")

    only_marker = temporary / "only-marker.uf2"
    only_marker.write_bytes(make_e10_block(0, 1))
    expect_range_error(only_marker, slot_start, slot_end, "no flash pages")

    try:
        module.check_range(in_slot, slot_start + 1, slot_end)
    except ValueError as error:
        assert "invalid flash range" in str(error)
    else:
        raise AssertionError("unaligned range start was accepted")

    # The CLI turns a range violation into a failing exit status for CMake.
    with patch.object(
        sys,
        "argv",
        [
            "rp_ota_artifacts.py",
            "check-range",
            "--uf2",
            str(at_flash_start),
            "--start",
            hex(slot_start),
            "--end",
            hex(slot_end),
        ],
    ):
        assert module.main() == 1
    with patch.object(
        sys,
        "argv",
        [
            "rp_ota_artifacts.py",
            "check-range",
            "--uf2",
            str(in_slot),
            "--start",
            hex(slot_start),
            "--end",
            hex(slot_end),
        ],
    ):
        assert module.main() == 0
