#!/usr/bin/env python3
"""Regression checks for native RP OTA artifact generation."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory


ROOT = Path(sys.argv[1]).resolve()
SCRIPT = ROOT / "scripts" / "rp_ota_artifacts.py"

spec = importlib.util.spec_from_file_location("rp_ota_artifacts_test", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {SCRIPT}")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

FLASH_BASE = 0x10000000
FAMILY_ID = 0xE48BFF56


def make_block(address: int, fill: int, block_number: int, block_count: int) -> bytes:
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
        FAMILY_ID,
    )
    block[32 : 32 + module.UF2_PAGE_SIZE] = bytes([fill]) * module.UF2_PAGE_SIZE
    struct.pack_into("<I", block, 508, module.UF2_MAGIC_END)
    return bytes(block)


def write_uf2(path: Path, pages: list[tuple[int, int]]) -> None:
    path.write_bytes(
        b"".join(
            make_block(address, fill, index, len(pages))
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

    conflicting = temporary / "conflicting.uf2"
    write_uf2(conflicting, [(FLASH_BASE, 0x99)])
    try:
        module.merge_uf2(boot, conflicting, merged)
    except ValueError as error:
        assert "overlapping UF2 block" in str(error)
    else:
        raise AssertionError("conflicting UF2 overlap was accepted")
