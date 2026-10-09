#!/usr/bin/env python3
"""Generate the hardware configuration of a project from its device_tree.json.

list      prints the assemblies of a configuration directory as JSON;
resolve   resolves one assembly and writes jh_hardware.h and
          jh_hardware_resolved.json;
finalize  adds the program configuration and writes jh_board_config.h, the
          CMake record, jh_hardware_config.json, the link signature and
          generation.d.
A failed command prints its diagnostic on stderr, returns nonzero and leaves
no output of that command behind.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from typing import Any, Sequence

import clock_registry
from codegen_support import atomic_write_text
import generate_board_config as boards_module
from hardware_literals import macro_token, node_token
from hardware_model import HardwareError
import hardware_output
import hardware_renderers as renderers
import hardware_resolver
import hardware_schema
import project_config

REPO_ROOT = Path(__file__).resolve().parents[1]
DEVICE_TREE = "device_tree.json"
HARDWARE_HEADER = "jh_hardware.h"
RESOLVED_JSON = "jh_hardware_resolved.json"
RESOLVE_OUTPUTS = (HARDWARE_HEADER, RESOLVED_JSON)
BOARD_HEADER = "jh_board_config.h"
CMAKE_RECORD = "jh_hardware_config.cmake"
CONFIG_JSON = "jh_hardware_config.json"
LINK_OUTPUTS = ("jh_link_contract.h", "jh_link_contract_definition.c", "jh_link_contract_reference.c")
FINALIZE_OUTPUTS = (BOARD_HEADER, CMAKE_RECORD, *LINK_OUTPUTS, CONFIG_JSON, "generation.d")
# Program definitions that need hardware, as hal_config.h checks them against
# the board macros; WiFi needs one of its radios.
HARDWARE_DEFINITIONS = {
    "HAL_ENABLE_BLE": ("bluetooth-le-controller",),
    "HAL_ENABLE_BLUETOOTH_CLASSIC": ("bluetooth-classic-controller",),
    "HAL_ENABLE_WIFI": ("cyw43", "native-wifi"),
    "HAL_NETWORK_BACKEND_CYW43": ("cyw43",),
    "HAL_NETWORK_BACKEND_ESP_IDF": ("native-wifi",),
}
# Hardware macros the two generated headers own; no definition, #undef or
# -D of the project may set them again.
OWNED_MACROS = re.compile(r"JH_HW_\w+|HAL_LED_BUILTIN|HAL_BOARD_\w+|HAL_STM32G474_CLOCK_\w+|HAL_CYW43_PIN_\w+")
# Components a project CYW43 brings, by build provider, with their modes.
CYW43_COMPONENTS = {"pico-sdk": "cyw43-pico-pio", "jh-stm32-baremetal": "cyw43-stm32-gspi"}
# Component slots the assembly decides: the clock tree and the selected radio.
ASSEMBLY_SLOTS = {"system-clock", "network-radio-transport", "network-stack", "bluetooth-host-stack"}
CYW43_PINS = {"WL_ON": "wlOn", "CHIP_SELECT": "cs", "DATA": "data", "CLOCK": "clock"}


def file_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependency_name(path: Path, config_dir: Path, project: bool) -> str:
    """Project files relative to the configuration directory, registry files
    in the hal: namespace."""
    path = path.resolve()
    if project:
        return Path(os_relpath(path, config_dir)).as_posix()
    return "hal:" + path.relative_to(REPO_ROOT).as_posix()


def os_relpath(path: Path, start: Path) -> str:
    return os.path.relpath(path, start)


def dependency_path(name: str, config_dir: Path) -> Path:
    return REPO_ROOT / name[4:] if name.startswith("hal:") else config_dir / name


def resolve_dependencies(resolver: hardware_resolver.Resolver, config_dir: Path) -> list[dict[str, str]]:
    """Every file one resolution reads, with its content digest."""
    target_id = resolver.target["id"]
    project = {resolver.path, *(binding.path for binding in resolver.catalogue.bindings.values()
                                if binding.origin == "project")}
    paths = {*(binding.path for binding in resolver.catalogue.bindings.values()),
             resolver.registry.boards_root / "capabilities.json",
             resolver.registry.boards_root / "targets" / f"{target_id}.json",
             resolver.board_source,
             REPO_ROOT / "config" / "hardware" / "clocks" / f"{target_id}.json",
             *(REPO_ROOT / "config" / "hardware").glob("*.schema.json"),
             *(Path(module.__file__) for module in list(sys.modules.values())
               if getattr(module, "__file__", None)
               and Path(module.__file__).resolve().parent == Path(__file__).resolve().parent)}
    items = [{"path": dependency_name(path, config_dir, path in project), "sha256": file_digest(path)}
             for path in paths | project]
    return sorted(items, key=lambda item: item["path"])


def resolve(config_dir: Path, target: str | None, assembly: str | None) -> tuple[dict[str, Any], str]:
    """The resolved document and its jh_hardware.h text."""
    resolver = hardware_resolver.Resolver(config_dir / DEVICE_TREE)
    model = resolver.resolve(target, assembly)
    document = {"schemaVersion": hardware_output.SCHEMA_VERSION, **model,
                "hardwareSha256": hardware_output.hardware_digest(model),
                "dependencies": resolve_dependencies(resolver, config_dir.resolve())}
    header = hardware_output.render_header(document, sorted(resolver.registry.capabilities))
    return document, header


class Finalize:
    """Add the program configuration to one resolved model."""

    def __init__(self, hardware_file: Path, header_file: Path, config_dir: Path,
                 variant: str | None, defines: Sequence[str]) -> None:
        self.hardware_file, self.header_file = hardware_file.resolve(), header_file.resolve()
        self.config_dir, self.variant, self.defines = config_dir.resolve(), variant, list(defines)

    def fail(self, code: str, message: str, source: Path | None = None, **where: Any) -> HardwareError:
        return HardwareError(code, source or self.hardware_file, where.pop("at", ""), message, **where)

    def load(self, registry: hardware_resolver.Registry) -> dict[str, Any]:
        """The model, checked against its digest, its inputs and its header,
        which must be exactly the text the model produces."""
        try:
            document = hardware_schema.load_json(self.hardware_file)
            header = self.header_file.read_text(encoding="utf-8")
        except (OSError, ValueError) as error:
            raise self.fail("JH-HW-GENERATION", str(error)) from error
        digest = hardware_output.hardware_digest(document)
        if document.get("hardwareSha256") != digest:
            raise self.fail("JH-HW-GENERATION", "the hardware model does not match its digest")
        if header != hardware_output.render_header(document, sorted(registry.capabilities)):
            raise self.fail("JH-HW-GENERATION", "the header is not the one this model produces",
                            self.header_file)
        for item in document["dependencies"]:
            path = dependency_path(item["path"], self.config_dir)
            if not path.is_file() or hardware_output.hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
                raise self.fail("JH-HW-GENERATION", f"{item['path']} changed after resolve; resolve again")
        return document

    def program(self, target_id: str) -> project_config.BuildConfig:
        header = self.config_dir / project_config.HEADER_NAME
        lines = project_config.target_declarations(header)
        if lines:
            raise self.fail("JH-HW-TARGETS", "a device tree takes its targets from the assemblies; "
                            "remove JH_PROJECT_TARGETS", header, line=lines[0])
        facts = project_config.load_targets()[target_id]
        try:
            return project_config.evaluate_build(self.config_dir, facts, self.variant, self.defines,
                                                 self.header_file)
        except project_config.ProjectConfigError as error:
            raise self.fail("JH-HW-FEATURE", str(error), header) from error

    def origin(self, build: project_config.BuildConfig, name: str) -> dict[str, Any]:
        """Where a definition came from: --define, --variant or a header line."""
        macro = build.config.macros.get(name)
        if macro is None or macro.source == "predefined" or any(
                d.split("=", 1)[0] == name for d in self.defines):
            argument = "--define" if any(d.split("=", 1)[0] == name for d in self.defines) else "--variant"
            return {"argument": argument}
        source, _, line = macro.source.rpartition(":")
        return {"source": Path(source), "line": int(line)}

    def check_ownership(self, build: project_config.BuildConfig) -> None:
        """No -D, variant definition or project file sets a generated macro."""
        for definition in build.definitions:
            name = definition.split("=", 1)[0]
            if OWNED_MACROS.fullmatch(name):
                where = self.origin(build, name)
                raise self.fail("JH-HW-FEATURE", f"{name} belongs to a generated header",
                                where.pop("source", None), **where)
        for path in sorted({Path(item).resolve() for item in build.config.files} - {self.header_file}):
            for line, kind, name in project_config.macro_directives(path):
                if OWNED_MACROS.fullmatch(name):
                    raise self.fail("JH-HW-FEATURE", f"#{kind} {name}: the macro belongs to a "
                                    "generated header", path, line=line)

    def check_hardware(self, document: dict[str, Any], build: project_config.BuildConfig,
                       features: list[str]) -> None:
        hardware = document["hardware"]
        selection = document["selection"]
        if selection["target"] == "mock":
            return
        requested = set(features) | set(build.config.macros)
        providers = self.assembly_providers(document)
        for name, needed in sorted(HARDWARE_DEFINITIONS.items()):
            if name not in requested or any(hardware["capabilities"].get(cap) for cap in needed):
                continue
            chosen = [cap for cap in needed if cap in providers]
            if chosen:
                raise self.fail("JH-HW-FEATURE", f"{name} needs {chosen[0]}, and the selected provider "
                                f"{providers[chosen[0]]} is absent or not completely wired",
                                self.config_dir / DEVICE_TREE,
                                at=hardware_resolver.pointer("assemblies", selection["assembly"],
                                                             "providers", chosen[0]))
            where = self.origin(build, name)
            raise self.fail("JH-HW-FEATURE", f"{name} needs one of {list(needed)} in the assembly",
                            where.pop("source", self.config_dir / DEVICE_TREE), **where)

    def assembly_providers(self, document: dict[str, Any]) -> dict[str, str]:
        data = hardware_schema.load_json(self.config_dir / DEVICE_TREE)
        return data["assemblies"][document["selection"]["assembly"]].get("providers", {})

    def run(self) -> dict[str, str]:
        registry = hardware_resolver.load_registry()
        document = self.load(registry)
        selection = document["selection"]
        target = registry.targets[selection["target"]]
        build = self.program(selection["target"])
        self.check_ownership(build)
        requested = build.requested_features()
        try:
            _, resolved, _ = boards_module.resolve_features(requested, target)
        except boards_module.DescriptorError as error:
            raise self.fail("JH-HW-FEATURE", str(error)) from error
        self.check_hardware(document, build, list(resolved))
        software = {"variant": build.variant.id if build.variant else None,
                    "requestedFeatures": sorted(requested), "resolvedFeatures": list(resolved),
                    "definitions": sorted(d for d in build.definitions
                                          if not project_config.FEATURE_PATTERN.fullmatch(d.split("=", 1)[0])),
                    "tunables": {name: macro.body for name, macro in sorted(build.config.macros.items())
                                 if name.startswith("HAL_") and macro.source != "predefined"
                                 and not macro.function_like
                                 and not project_config.FEATURE_PATTERN.fullmatch(name)},
                    "provider": target["build"]["provider"]}
        config_digest = hardware_output.digest({"hardwareSha256": document["hardwareSha256"],
                                                "software": software})
        board, header_definitions, components = assembly_board(document, registry, target)
        outputs = boards_module.build_outputs(
            target, board, registry.boards, registry.capabilities, requested, registry.boards_root,
            header_definitions=header_definitions, cmake_definitions=[], components=components,
            contract_hash=config_digest,
            header_prelude=["#ifndef JH_HW_SCHEMA_VERSION",
                            '#error "jh_hardware.h must be loaded before jh_board_config.h"',
                            "#endif"],
            generation_extra=[self.hardware_file, self.header_file,
                              *(Path(f) for f in build.config.files),
                              *(dependency_path(item["path"], self.config_dir)
                                for item in document["dependencies"])])
        cmake = outputs.pop("jh_board_config.cmake").splitlines()
        cmake[0] = "# Generated by generate_hardware_config.py; do not edit."
        cmake += [f'set(JH_HARDWARE_HEADER "{self.header_file.as_posix()}")',
                  f'set(JH_HARDWARE_RESOLVED_FILE "{self.hardware_file.as_posix()}")',
                  f'set(JH_HARDWARE_SHA256 "{document["hardwareSha256"]}")',
                  f'set(JH_CONFIG_SHA256 "{config_digest}")',
                  f'set(JH_ASSEMBLY "{selection["assembly"]}")',
                  f'set(JH_PROJECT_VARIANT "{software["variant"] or ""}")']
        outputs.pop("jh_board_resolved.json")
        final = {key: value for key, value in document.items() if key not in ("software", "configSha256")}
        final.update(software=software, configSha256=config_digest)
        return {**outputs, CMAKE_RECORD: "\n".join(cmake) + "\n",
                CONFIG_JSON: hardware_output.dump_json(final)}


def assembly_board(document: dict[str, Any], registry: hardware_resolver.Registry,
                   target: dict[str, Any]) -> tuple[dict[str, Any], list[str], list[str]]:
    """The board as the assembly uses it, the definitions its header carries
    and the build components. Values the early header exports refer to it."""
    selection, hardware = document["selection"], document["hardware"]
    board = copy.deepcopy(registry.boards[selection["board"]])
    tree_component = (clock_registry.STM32G474_TREE_COMPONENTS.get(hardware["clock"]["tree"])
                      if target["id"] == "stm32g474" else None)
    radio = hardware["providers"].get("cyw43") if hardware["capabilities"].get("cyw43") else None
    slot = lambda key: boards_module.COMPONENT_REGISTRY[key]["slot"]
    board["components"] = {key: value for key, value in board["components"].items()
                           if slot(key) not in ASSEMBLY_SLOTS or (radio == "board" and slot(key) != "system-clock")}
    if tree_component:
        board["components"][tree_component] = {"mode": "board-owned"}
    definitions = boards_module.board_compile_definitions(target, board)
    board["capabilities"] = {cap: {"present": bool(hardware["capabilities"].get(cap)),
                                   "macro": f"JH_HW_HAS_{macro_token(cap)}"}
                             for cap in registry.capabilities}
    status = hardware["statusLed"]
    board["devices"].pop("statusLed", None)
    if status is not None:
        endpoint = dict(status["endpoint"])
        token = node_token(status["node"])
        endpoint["macro"] = f"JH_HW_NODE_{token}_PIN_{macro_token(status['signal'])}"
        kind = ("addressable" if status["kind"] == "addressable"
                else "gpio" if endpoint["domain"] == "soc-gpio" else "component-gpio"
                if endpoint["domain"] == "component-gpio" else "device-pin")
        board["devices"]["statusLed"] = {"kind": kind, "endpoint": endpoint,
                                         **({"pixelOrder": status["pixelOrder"]} if kind == "addressable" else {})}
    released = {key for key, item in hardware["reservations"].items() if item["released"]}
    board["gpio"]["reservations"] = {key: item for key, item in board["gpio"].get("reservations", {}).items()
                                     if key not in released}
    board.setdefault("can", {"channels": []})
    board["can"]["channels"] = [*board["can"].get("channels", []), *can_channels(hardware)]
    components = sorted(set(target["components"]) | set(board["components"]))
    if radio not in (None, "board"):
        token = node_token(radio)
        definitions += [f"HAL_CYW43_PIN_{name}=JH_HW_NODE_{token}_PIN_{macro_token(signal)}"
                        for name, signal in CYW43_PINS.items()]
        definitions.append("HAL_CYW43_MAX_TRANSACTION_BYTES=2048u")
        if target["build"]["provider"] == "pico-sdk":
            definitions.append("HAL_CYW43_GSPI_TARGET_HZ=31250000u")
        components = sorted({*components, CYW43_COMPONENTS[target["build"]["provider"]],
                             "cyw43-lwip", "btstack-host"})
    return board, definitions, components


def can_channels(hardware: dict[str, Any]) -> list[dict[str, Any]]:
    """Project FDCAN channels in controller order, as board CAN channels."""
    channels = []
    for path, node in sorted(hardware["nodes"].items(), key=lambda item: (
            item[1].get("controller") or {}).get("index", 0)):
        if renderers.RENDERERS.get(node["compatible"]) != "fdcan" or not node["present"]:
            continue
        if renderers.config_init(path, node, hardware) is None:
            continue
        token = node_token(path)
        pins = {}
        for signal in ("rx", "tx", "standby"):
            endpoint = node["signals"][signal]["endpoint"]
            if endpoint is not None:
                pins[signal] = {**endpoint, "macro": f"JH_HW_NODE_{token}_PIN_{macro_token(signal)}"}
        channels.append({"controller": "stm32g474-fdcan", "instance": node["controller"]["index"] + 1,
                         **pins, "standbyActiveHigh": node["properties"]["standbyActiveHigh"]["value"],
                         "maxBitrateHz": node["properties"]["maxBitrateHz"]["value"],
                         "transceiver": "mcp2562fd" if node["compatible"] == "microchip,mcp2562fd" else "generic"})
    return channels


def write_outputs(output_dir: Path, outputs: dict[str, str], names: Sequence[str]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for name in names:
        if name not in outputs:
            (output_dir / name).unlink(missing_ok=True)
    for name, text in outputs.items():
        atomic_write_text(output_dir / name, text, error_type=HardwareError)


def remove_outputs(output_dir: Path, names: Sequence[str]) -> None:
    for name in names:
        (output_dir / name).unlink(missing_ok=True)


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    listing = commands.add_parser("list", help="print the assemblies as JSON")
    listing.add_argument("--config-dir", type=Path, required=True)
    resolving = commands.add_parser("resolve", help="write jh_hardware.h and the resolved JSON")
    resolving.add_argument("--config-dir", type=Path, required=True)
    resolving.add_argument("--target")
    resolving.add_argument("--assembly")
    resolving.add_argument("--output-dir", type=Path, required=True)
    resolving.add_argument("--output-root", type=Path)
    finalizing = commands.add_parser("finalize", help="add the program configuration")
    finalizing.add_argument("--hardware", type=Path, required=True)
    finalizing.add_argument("--hardware-header", type=Path, required=True)
    finalizing.add_argument("--config-dir", type=Path, required=True)
    finalizing.add_argument("--variant")
    finalizing.add_argument("--define", action="append", default=[])
    finalizing.add_argument("--output-dir", type=Path, required=True)
    finalizing.add_argument("--output-root", type=Path)
    return parser.parse_args(argv)


def report(error: HardwareError) -> int:
    print(f"error: {error}", file=sys.stderr)
    return 2


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    config_dir = args.config_dir.resolve()
    if args.command == "list":
        try:
            data = hardware_resolver.load_device_tree(config_dir / DEVICE_TREE)
        except HardwareError as error:
            return report(error)
        print(json.dumps({"schemaVersion": hardware_output.SCHEMA_VERSION,
                          "assemblies": hardware_resolver.list_assemblies(data)}, indent=2))
        return 0
    try:
        output_dir = boards_module.require_managed_output(args.output_dir, args.output_root, REPO_ROOT)
    except boards_module.DescriptorError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if args.command == "finalize":
        try:
            outputs = Finalize(args.hardware, args.hardware_header, config_dir, args.variant,
                               args.define).run()
        except HardwareError as error:
            remove_outputs(output_dir, FINALIZE_OUTPUTS)
            return report(error)
        write_outputs(output_dir, outputs, FINALIZE_OUTPUTS)
        return 0
    try:
        document, header = resolve(config_dir, args.target, args.assembly)
    except HardwareError as error:
        remove_outputs(output_dir, RESOLVE_OUTPUTS)
        return report(error)
    write_outputs(output_dir, {HARDWARE_HEADER: header, RESOLVED_JSON: hardware_output.dump_json(document)},
                  RESOLVE_OUTPUTS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
