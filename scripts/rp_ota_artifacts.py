#!/usr/bin/env python3
"""Create native RP OTA containers and merge boot/application UF2 images."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import os
import struct
from pathlib import Path


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK_SIZE = 512
UF2_PAGE_SIZE = 256
FLASH_SECTOR_ERASE_SIZE = 4096
OTA_MAGIC = b"JHOTA1\r\n"
OTA_HEADER_SIZE = 160
TARGET_IDS = {
    "rp2040": 1,
    "rp2350-arm": 2,
    "rp2350-riscv": 3,
}


def uf2_blocks(path: Path) -> list[bytearray]:
    data = path.read_bytes()
    if len(data) % UF2_BLOCK_SIZE:
        raise ValueError(f"{path}: UF2 size is not a multiple of {UF2_BLOCK_SIZE}")
    blocks = [
        bytearray(data[offset : offset + UF2_BLOCK_SIZE])
        for offset in range(0, len(data), UF2_BLOCK_SIZE)
    ]
    for block in blocks:
        start0, start1 = struct.unpack_from("<II", block, 0)
        end = struct.unpack_from("<I", block, 508)[0]
        if (start0, start1, end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise ValueError(f"{path}: invalid UF2 block")
        address, payload_size = struct.unpack_from("<II", block, 12)
        if address % UF2_PAGE_SIZE:
            raise ValueError(f"{path}: unaligned UF2 address 0x{address:08x}")
        if payload_size != UF2_PAGE_SIZE:
            raise ValueError(
                f"{path}: unsupported UF2 payload size {payload_size} "
                f"at 0x{address:08x}"
            )
    return blocks


def pad_touched_flash_sectors(blocks_by_address: dict[int, bytearray]) -> None:
    if not blocks_by_address:
        raise ValueError("merged UF2 contains no blocks")

    touched_sectors: dict[int, bytearray] = {}
    for address, block in blocks_by_address.items():
        touched_sectors.setdefault(address // FLASH_SECTOR_ERASE_SIZE, block)

    last_page = max(blocks_by_address)
    zero_payload = bytes(UF2_BLOCK_SIZE - 36)
    for sector, template in touched_sectors.items():
        sector_start = sector * FLASH_SECTOR_ERASE_SIZE
        sector_end = sector_start + FLASH_SECTOR_ERASE_SIZE
        for address in range(sector_start, sector_end, UF2_PAGE_SIZE):
            if address >= last_page or address in blocks_by_address:
                continue
            dummy = bytearray(template)
            struct.pack_into("<II", dummy, 12, address, UF2_PAGE_SIZE)
            dummy[32:508] = zero_payload
            blocks_by_address[address] = dummy


def merge_uf2(boot: Path, application: Path, output: Path) -> None:
    blocks_by_address: dict[int, bytearray] = {}
    for block in [*uf2_blocks(boot), *uf2_blocks(application)]:
        address = struct.unpack_from("<I", block, 12)[0]
        if address in blocks_by_address:
            existing = blocks_by_address[address]
            if existing[:20] == block[:20] and existing[28:] == block[28:]:
                continue
            raise ValueError(f"overlapping UF2 block at 0x{address:08x}")
        blocks_by_address[address] = block
    pad_touched_flash_sectors(blocks_by_address)
    blocks = [blocks_by_address[address] for address in sorted(blocks_by_address)]
    for index, block in enumerate(blocks):
        struct.pack_into("<II", block, 20, index, len(blocks))
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(b"".join(blocks))
    os.replace(temporary, output)


def package_ota(
    binary: Path,
    output: Path,
    target: str,
    program_offset: int,
    generation: int,
    version: str,
) -> None:
    payload = binary.read_bytes()
    version_bytes = version.encode("utf-8")
    if not payload:
        raise ValueError("OTA payload is empty")
    if len(version_bytes) >= 32:
        raise ValueError("OTA version must be shorter than 32 UTF-8 bytes")
    header = bytearray(OTA_HEADER_SIZE)
    header[0:8] = OTA_MAGIC
    struct.pack_into(
        "<HHHHIIII",
        header,
        8,
        1,
        OTA_HEADER_SIZE,
        TARGET_IDS[target],
        0,
        program_offset,
        len(payload),
        generation,
        0,
    )
    header[32:64] = hashlib.sha256(payload).digest()
    header[64 : 64 + len(version_bytes)] = version_bytes
    struct.pack_into("<I", header, OTA_HEADER_SIZE - 4, binascii.crc32(header[:-4]) & 0xFFFFFFFF)
    output.write_bytes(header + payload)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    merge = subparsers.add_parser("merge-uf2")
    merge.add_argument("--boot", type=Path, required=True)
    merge.add_argument("--application", type=Path, required=True)
    merge.add_argument("--output", type=Path, required=True)

    package = subparsers.add_parser("package")
    package.add_argument("--binary", type=Path, required=True)
    package.add_argument("--output", type=Path, required=True)
    package.add_argument("--target", choices=sorted(TARGET_IDS), required=True)
    package.add_argument("--program-offset", type=lambda value: int(value, 0), required=True)
    package.add_argument("--generation", type=int, required=True)
    package.add_argument("--version", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "merge-uf2":
        merge_uf2(args.boot, args.application, args.output)
    else:
        package_ota(
            args.binary,
            args.output,
            args.target,
            args.program_offset,
            args.generation,
            args.version,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
