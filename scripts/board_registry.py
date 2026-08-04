#!/usr/bin/env python3
"""Tooling view of the authoritative target and board descriptor registry."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from generate_board_config import load_registry


def tooling_target_registry(jh_root: Path) -> dict[str, dict[str, Any]]:
    """Return the jh-vscode registry derived exclusively from ``boards/``."""
    targets, boards, _ = load_registry(jh_root / "boards")
    registry: dict[str, dict[str, Any]] = {}

    for target_id, target in sorted(targets.items()):
        provider = target["build"]["provider"]
        if provider == "host":
            continue

        target_boards = []
        for board_id, board in sorted(boards.items()):
            if target_id not in board["compatibleTargets"]:
                continue
            target_boards.append(
                {
                    "id": board_id,
                    "displayName": board["displayName"],
                    "status": board["status"],
                    "cache": {"JH_BOARD": board_id},
                    "components": sorted((board.get("components") or {}).keys()),
                }
            )

        cache: dict[str, Any] = {"JH_TARGET": target_id}
        upload: dict[str, Any]
        if provider == "pico-sdk":
            cache["PICO_SDK_PATH"] = "${jhRoot}/third_party/pico-sdk"
            if target["architecture"]["isa"] == "riscv32":
                cache["PICO_TOOLCHAIN_PATH"] = (
                    "${jhRoot}/third_party/riscv-toolchain"
                )
            upload = {"strategy": "uf2"}
        elif provider == "jh-stm32-baremetal":
            cache["CMAKE_TOOLCHAIN_FILE"] = (
                "${jhRoot}/stm32_lib/toolchain_stm32g474.cmake"
            )
            upload = {
                "strategy": "openocd",
                "openocd": {
                    "interface": "interface/stlink.cfg",
                    "target": "target/stm32g4x.cfg",
                },
            }
        else:
            continue

        registry[target_id] = {
            "id": target_id,
            "displayName": target["displayName"],
            "description": target["description"],
            "status": target["status"],
            "toolchain": "cmake",
            "defaultBoard": target["defaultBoard"],
            "cache": cache,
            "upload": upload,
            "boards": target_boards,
        }

    return registry
