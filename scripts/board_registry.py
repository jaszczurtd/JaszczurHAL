#!/usr/bin/env python3
"""Tooling view of the authoritative target and board descriptor registry."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import Any, Sequence

from generate_board_config import load_registry


LINK_LIBRARY_PROVIDERS = frozenset({"pico-sdk", "jh-stm32-baremetal", "esp-idf"})


def _registry_targets(
    jh_root: Path,
) -> list[tuple[str, dict[str, Any], dict[str, dict[str, Any]]]]:
    targets, boards, _ = load_registry(jh_root / "boards")
    return [
        (target_id, target, boards)
        for target_id, target in sorted(targets.items())
    ]


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
    registry: dict[str, dict[str, Any]] = {}

    for target_id, target, boards in _registry_targets(jh_root):
        provider = target["build"]["provider"]
        if provider != "host" and provider not in LINK_LIBRARY_PROVIDERS:
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
    registry: dict[str, dict[str, Any]] = {}

    for target_id, target, boards in _registry_targets(jh_root):
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
                "${jhRoot}/link_libraries/stm32_lib/toolchain_stm32g474.cmake"
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


def target_facts(jh_root: Path, target_id: str) -> dict[str, Any]:
    """Return the build facts a library runner needs for one target."""
    targets, _, _ = load_registry(jh_root / "boards")
    if target_id not in targets:
        known = ", ".join(sorted(targets))
        raise KeyError(f"unknown target {target_id!r}; known targets: {known}")
    target = targets[target_id]
    return {
        "provider": target["build"]["provider"],
        "defaultBoard": target["defaultBoard"],
        "isa": target["architecture"]["isa"],
        "status": target["status"],
        "supportedFeatures": [
            str(item).removesuffix("=1")
            for item in target.get("supportedFeatures", [])
        ],
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Query the target and board descriptor registry."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    subparsers = parser.add_subparsers(dest="action", required=True)
    facts = subparsers.add_parser(
        "target-facts", help="Print KEY=VALUE build facts for one target"
    )
    facts.add_argument("target")
    subparsers.add_parser(
        "list-targets", help="Print every target that has a library runner"
    )
    args = parser.parse_args(argv)
    jh_root = args.repo_root.resolve()
    if args.action == "target-facts":
        try:
            result = target_facts(jh_root, args.target)
        except KeyError as error:
            print(f"error: {error.args[0]}", file=sys.stderr)
            return 2
        for key, value in result.items():
            if isinstance(value, list):
                value = " ".join(value)
            print(f"{key}={value}")
        return 0
    for target_id, target in sorted(library_target_registry(jh_root).items()):
        if target["provider"] == "host":
            continue
        print(f"{target_id} {target['provider']} {target['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
