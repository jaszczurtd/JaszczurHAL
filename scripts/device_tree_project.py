#!/usr/bin/env python3
"""What the tools read from a configuration directory with device_tree.json.

The assemblies of the file are the only targets and boards such a project
builds for. This module lists them, picks one by the rules every tool shares
(an explicit choice first, then the stored ones in order), evaluates the
project configuration with each assembly's jh_hardware.h and names the build
directory of a library configured from the directory.

build-key  prints <assembly or "auto">/<16 hex digits> for a library build
           directory: the configuration directory, variant and requests
           select it, so independent projects never share a build.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
from pathlib import Path
import sys
import tempfile
from typing import Any, Iterable, Sequence

DEVICE_TREE = "device_tree.json"


def device_tree_path(config_dir: Path | None) -> Path | None:
    """The device tree of ``config_dir``, or None when the directory builds
    on the board path. Only the presence of the file selects the mode."""
    if config_dir is None:
        return None
    path = Path(config_dir) / DEVICE_TREE
    return path if path.is_file() else None


def _load(config_dir: Path) -> tuple[Path, dict[str, Any]]:
    import hardware_resolver

    path = Path(config_dir) / DEVICE_TREE
    return path, hardware_resolver.load_device_tree(path)


def assemblies(config_dir: Path) -> list[dict[str, str]]:
    """``{name, target, board}`` of every assembly, sorted by name. A broken
    file is an error, never a reason to fall back to the board path."""
    import hardware_resolver

    _, data = _load(config_dir)
    return hardware_resolver.list_assemblies(data)


def choose(config_dir: Path, target: str | None, assembly: str | None,
           stored: Iterable[tuple[str | None, str]] = ()) -> tuple[dict[str, str], str]:
    """The assembly one build uses and where the choice came from.

    ``target`` and ``assembly`` are explicit arguments; an explicit assembly
    must exist and match the target. Otherwise the first ``stored`` key
    (local selection, then manifest) that belongs to the target is used; a
    stored key the file does not declare is an error. Without any, one
    assembly for the target is chosen automatically and several need a choice.
    """
    import hardware_resolver
    from hardware_model import HardwareError

    path, data = _load(config_dir)
    known = data["assemblies"]
    if assembly is None:
        for key, source in stored:
            if not key:
                continue
            if key not in known:
                raise HardwareError(
                    "JH-HW-ASSEMBLY", path, "",
                    f"{source} selects assembly {key!r}, which the device tree does not declare; "
                    f"known: {sorted(known)}")
            if target is None or known[key]["target"] == target:
                return _entry(known, key), source
    name = hardware_resolver.select_assembly(path, data, target, assembly)
    source = "explicit" if assembly is not None else "automatic"
    return _entry(known, name), source


def _entry(known: dict[str, Any], name: str) -> dict[str, str]:
    return {"name": name, "target": known[name]["target"], "board": known[name]["board"]}


def hardware_header(config_dir: Path, assembly: str, directory: Path) -> Path:
    """Write the jh_hardware.h of ``assembly`` into ``directory`` and return
    its path: what the project configuration is read with."""
    import generate_hardware_config

    _, header = generate_hardware_config.resolve(Path(config_dir), None, assembly)
    path = Path(directory) / generate_hardware_config.HARDWARE_HEADER
    path.write_text(header, encoding="utf-8")
    return path


def components(config_dir: Path, assembly: str) -> set[str]:
    """The build components of ``assembly``, as finalize gives them to the
    build: the board's, a module radio's transport and the clock tree."""
    import generate_hardware_config
    import hardware_resolver

    document, _ = generate_hardware_config.resolve(Path(config_dir), None, assembly)
    registry = hardware_resolver.load_registry()
    target = registry.targets[document["selection"]["target"]]
    return set(generate_hardware_config.assembly_board(document, registry, target)[2])


def evaluate(config_dir: Path, assembly: str, target: Any, variant_id: str | None = None) -> Any:
    """``hal_project_config.h`` evaluated for one assembly and variant, with
    its jh_hardware.h, as the compiler of that build sees it."""
    import project_config

    with tempfile.TemporaryDirectory(prefix="jh-device-tree-") as scratch:
        header = hardware_header(config_dir, assembly, Path(scratch))
        build = project_config.evaluate_build(Path(config_dir), target, variant_id, (), header)
    return dataclasses.replace(build, assembly=assembly)


def builds(config_dir: Path, targets: dict[str, Any]) -> list[Any]:
    """Every build of the directory: each assembly times the base and each
    variant, without the builds the header stops on ``#error``. The list
    depends only on the files, never on a stored selection."""
    import project_config

    header = Path(config_dir) / project_config.HEADER_NAME
    lines = project_config.target_declarations(header)
    if lines:
        raise project_config.ProjectConfigError(
            f"{header}:{lines[0]}: [JH-HW-TARGETS] a device tree takes its targets from the "
            "assemblies; remove JH_PROJECT_TARGETS")
    variants = project_config.read_project_config(header).variants
    result = []
    for item in assemblies(config_dir):
        if item["target"] not in targets:
            raise project_config.ProjectConfigError(
                f"[JH-CFG-TARGET] assembly {item['name']} names unknown target {item['target']!r}")
        with tempfile.TemporaryDirectory(prefix="jh-device-tree-") as scratch:
            hardware = hardware_header(config_dir, item["name"], Path(scratch))
            for variant_id in [None, *(variant.id for variant in variants)]:
                try:
                    build = project_config.evaluate_build(
                        Path(config_dir), targets[item["target"]], variant_id, (), hardware)
                except project_config.ConfigurationRejected:
                    continue
                result.append(dataclasses.replace(build, assembly=item["name"]))
    return result


def build_key(config_dir: Path, assembly: str, requests: Sequence[str] = ()) -> str:
    """``<assembly or "auto">/<16 hex digits>`` naming a library build of the
    directory: the directory, the assembly and the variant and requests
    (``requests`` in any order) select it, so edits rebuild in place."""
    text = "\0".join([Path(config_dir).resolve().as_posix(), assembly, *sorted(requests)])
    return f"{assembly or 'auto'}/{hashlib.sha256(text.encode()).hexdigest()[:16]}"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    key = commands.add_parser("build-key", help="print the library build directory key")
    key.add_argument("config_dir", type=Path)
    key.add_argument("assembly")
    key.add_argument("requests", nargs="*")
    args = parser.parse_args(argv)
    print(build_key(args.config_dir, args.assembly, args.requests))
    return 0


if __name__ == "__main__":
    sys.exit(main())
