"""Hardware of a hardware-test fixture, read from its device_tree.json.

Verifiers take the assembly list and the pins from the fixture's own
description instead of repeating them. The native HAL tool reads the file
without publishing generated artifacts.
"""

from __future__ import annotations

from pathlib import Path
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import device_tree_project  # noqa: E402


def assemblies(fixture: Path) -> dict[str, dict[str, str]]:
    """Assembly name -> {"target", "board"} of the fixture."""
    return {item["name"]: {"target": item["target"], "board": item["board"]}
            for item in device_tree_project.assemblies(Path(fixture))}


def select(fixture: Path, assembly: str | None = None) -> str:
    """The named assembly, or the only one when none is named."""
    names = sorted(assemblies(fixture))
    if assembly is None and len(names) == 1:
        return names[0]
    if assembly not in names:
        raise ValueError(f"{Path(fixture).name}: choose an assembly from {names}")
    return assembly


def hardware(fixture: Path, assembly: str) -> dict[str, Any]:
    """Resolved hardware model of one assembly."""
    return device_tree_project.resolved(Path(fixture), assembly=assembly)["document"]["hardware"]


def pin(fixture: Path, assembly: str, node: str, signal: str) -> int:
    """HAL pin number of a connected SoC signal, as the firmware reports it."""
    result = device_tree_project.resolved(Path(fixture), assembly=assembly, include_board=True)
    encoded = result["assemblyBoard"]["pinNumbers"].get(f"{node}.{signal}")
    if encoded is None:
        raise ValueError(f"{node}.{signal} has no SoC pin in {assembly}")
    return encoded
