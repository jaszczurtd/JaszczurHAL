#!/usr/bin/env python3
"""Tooling view of the authoritative target and board descriptor registry."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from generate_board_config import load_registry


def board_programming_identity(board: dict[str, Any]) -> dict[str, Any]:
    """Return the exact USB identity declared by a board programmer."""
    programming = board.get("programming")
    usb = programming.get("usb") if isinstance(programming, dict) else None
    if not isinstance(usb, dict):
        return {}
    vid = usb.get("vid")
    pid = usb.get("pid")
    if not isinstance(vid, int) or not isinstance(pid, int):
        return {}
    return {"enabled": True, "usbVid": vid, "usbPid": pid}


def library_target_registry(jh_root: Path) -> dict[str, dict[str, Any]]:
    """Return every supported static-library target and compatible board."""
    targets, boards, _ = load_registry(jh_root / "boards")
    registry: dict[str, dict[str, Any]] = {}

    for target_id, target in sorted(targets.items()):
        provider = target["build"]["provider"]
        if provider not in {"host", "pico-sdk", "jh-stm32-baremetal"}:
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
                }
            )

        registry[target_id] = {
            "id": target_id,
            "displayName": target["displayName"],
            "description": target["description"],
            "status": target["status"],
            "provider": provider,
            "defaultBoard": target["defaultBoard"],
            "boards": target_boards,
        }

    return registry


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
            board_descriptor = {
                "id": board_id,
                "displayName": board["displayName"],
                "status": board["status"],
                "components": sorted((board.get("components") or {}).keys()),
            }
            identity = board_programming_identity(board)
            if identity:
                board_descriptor["identity"] = identity
            if provider in {"pico-sdk", "jh-stm32-baremetal"}:
                board_descriptor["cache"] = {"JH_BOARD": board_id}
            target_boards.append(board_descriptor)

        cache: dict[str, Any] = {"JH_TARGET": target_id}
        upload: dict[str, Any]
        toolchain: str
        provider_config: dict[str, Any] = {}
        if provider == "pico-sdk":
            toolchain = "cmake"
            cache["PICO_SDK_PATH"] = "${jhRoot}/third_party/pico-sdk"
            if target["architecture"]["isa"] == "riscv32":
                cache["PICO_TOOLCHAIN_PATH"] = (
                    "${jhRoot}/third_party/riscv-toolchain"
                )
            upload = {"strategy": "uf2"}
        elif provider == "jh-stm32-baremetal":
            toolchain = "cmake"
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
        elif provider == "esp-idf":
            toolchain = "esp-idf"
            cache = {}
            upload = {"strategy": "esp-idf"}
            provider_config = {
                "espIdf": {
                    "runner": "${jhRoot}/scripts/build_esp_idf.py",
                    "artifactManifest": (
                        "${buildDir}/jh_esp_idf_artifacts.json"
                    ),
                }
            }
        else:
            continue

        registry[target_id] = {
            "id": target_id,
            "displayName": target["displayName"],
            "description": target["description"],
            "status": target["status"],
            "provider": provider,
            "toolchain": toolchain,
            "defaultBoard": target["defaultBoard"],
            "requiredFeatures": list(target.get("requiredFeatures", [])),
            "cache": cache,
            "upload": upload,
            "boards": target_boards,
            **provider_config,
        }

    return registry
