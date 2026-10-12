"""Hardware of a hardware-test fixture, read from its device_tree.json.

Verifiers take the assembly list and the pins from the fixture's own
description instead of repeating them. The HAL resolver reads the file in
process and writes nothing.
"""

from __future__ import annotations

from pathlib import Path
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import hardware_renderers  # noqa: E402
import hardware_resolver  # noqa: E402


def assemblies(fixture: Path) -> dict[str, dict[str, str]]:
    """Assembly name -> {"target", "board"} of the fixture."""
    data = hardware_resolver.load_device_tree(Path(fixture) / "device_tree.json")
    return {item["name"]: {"target": item["target"], "board": item["board"]}
            for item in hardware_resolver.list_assemblies(data)}


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
    resolver = hardware_resolver.Resolver(Path(fixture) / "device_tree.json")
    return resolver.resolve(assembly=assembly)["hardware"]


def pin(fixture: Path, assembly: str, node: str, signal: str) -> int:
    """HAL pin number of a connected SoC signal, as the firmware reports it."""
    endpoint = hardware(fixture, assembly)["nodes"][node]["signals"][signal]["endpoint"]
    encoded = hardware_renderers.encode_pin(endpoint)
    if encoded is None:
        raise ValueError(f"{node}.{signal} has no SoC pin in {assembly}")
    return encoded
