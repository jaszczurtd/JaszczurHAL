"""Resolve a project device tree into one hardware model.

The phases follow the device tree specification: input and schema, binding
declarations and macro names, assembly selection, presence and references,
properties and conditions, clock, endpoints/nets/reservations/controllers/
addresses/transport, status LED, then hardware providers. A phase reports
its first error by source path, JSON Pointer and code; later phases never
see data an earlier one rejected. The model is plain JSON-ready data that
the renderers and the build share; nothing here writes files.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path
from typing import Any

import clock_registry
import generate_board_config as boards_module
from hardware_literals import macro_token, node_token
from hardware_model import Binding, Catalogue, HardwareError, check_property_value, load_catalogue
import hardware_model
import hardware_output
import hardware_renderers as renderers
import hardware_schema

REPO_ROOT = Path(__file__).resolve().parents[1]
BOARDS_ROOT = REPO_ROOT / "boards"
# Bus and controller kinds the HAL APIs select by index, with their count.
HAL_BUS_COUNT = {"spi": 2, "i2c": 2, "uart": 2}
BUS_PINS = {"spi": ({"sck", "mosi", "miso"}, set()), "i2c": ({"sda", "scl"}, set()),
            "uart": ({"tx", "rx"}, {"cts", "rts"})}
NODE_SUFFIXES = ("PRESENT", "CONFIG_AVAILABLE", "CONFIG_INIT", "BUS_INDEX", "CONTROLLER_INDEX",
                 "ADDRESS", "FREQUENCY_HZ", "SPI_MODE", "SPI_BIT_ORDER", "SPI_SETTINGS_INIT")
PIN_SUFFIXES = ("", "_CONNECTED", "_DOMAIN", "_OWNER", "_PROVIDER", "_INDEX")
BUS_SUFFIXES = ("INDEX", "FREQUENCY_HZ", "UART_PORT", "BAUD_RATE", "DATA_BITS", "PARITY",
                "STOP_BITS", "FRAME_CONFIG")
GLOBAL_MACROS = ("JH_HW_SCHEMA_VERSION", "JH_HW_TARGET_NAME", "JH_HW_BOARD_NAME",
                 "JH_HW_ASSEMBLY_NAME", "JH_HW_HARDWARE_SHA256")
RESERVED_ROOTS = ("board", "bus")
# Board GPIO devices by role: LEDs expose "out", buttons "in".
BOARD_GPIO_TYPES = {"statusLed": ("jaszczurhal,gpio-output", "out"),
                    "userButton": ("jaszczurhal,gpio-input", "in"),
                    "bootButton": ("jaszczurhal,gpio-input", "in")}
BOARD_ROLE_TYPES = {"sx1262-radio": "semtech,sx1262"}


class Collector:
    """Errors of one phase; the first by source, JSON Pointer and code wins."""

    def __init__(self, base: Path) -> None:
        self.base = base
        self.errors: list[HardwareError] = []

    def add(self, code: str, source: Path | str, pointer: str, message: str,
            related: tuple[str, ...] = ()) -> None:
        self.errors.append(HardwareError(code, source, pointer, message, related=related))

    def check(self) -> None:
        if self.errors:
            raise min(self.errors, key=lambda e: (os.path.relpath(e.source, self.base),
                                                  hardware_schema.pointer_key(e.pointer), e.code))


@dataclass
class Registry:
    targets: dict[str, dict[str, Any]]
    boards: dict[str, dict[str, Any]]
    capabilities: dict[str, Any]
    clocks: dict[str, dict[str, Any]]
    boards_root: Path = BOARDS_ROOT

    def board_path(self, board_id: str) -> Path:
        return self.boards_root / "profiles" / f"{board_id}.json"


def load_registry(boards_root: Path = BOARDS_ROOT) -> Registry:
    targets, boards, capabilities = boards_module.load_registry(boards_root)
    return Registry(targets, boards, capabilities, clock_registry.load_registry(), boards_root)


@dataclass
class Node:
    path: str
    binding: Binding
    origin: tuple[Path, str]           # where the node is declared
    board: bool = False
    required: bool = True
    present: bool = False
    properties: dict[str, tuple[Any, Path, str]] = field(default_factory=dict)
    pins: dict[str, dict[str, Any]] = field(default_factory=dict)
    entry: dict[str, Any] = field(default_factory=dict)   # assembly entry
    board_facts: dict[str, Any] = field(default_factory=dict)
    transport: dict[str, Any] = field(default_factory=dict)  # effective bus settings

    @property
    def compatible(self) -> str:
        return self.binding.compatible


def pointer(*parts: Any) -> str:
    return "".join(hardware_schema.pointer_join("", part) for part in parts)


def load_device_tree(path: Path) -> dict[str, Any]:
    """Phase 1: UTF-8 JSON and the device tree schema."""
    try:
        data = hardware_schema.load_json(path)
    except (OSError, ValueError) as error:
        raise HardwareError("JH-HW-SCHEMA", path, "", str(error)) from error
    first = hardware_schema.SchemaSet().first_error("device_tree.schema.json", data)
    if first is not None:
        raise HardwareError("JH-HW-SCHEMA", path, first.pointer, first.message)
    return data


def list_assemblies(data: dict[str, Any]) -> list[dict[str, str]]:
    return [{"name": name, "target": item["target"], "board": item["board"]}
            for name, item in sorted(data["assemblies"].items())]


def select_assembly(path: Path, data: dict[str, Any], target: str | None,
                    assembly: str | None) -> str:
    """One assembly for the target is automatic, several need --assembly."""
    assemblies = data["assemblies"]

    def fail(message: str, argument: str) -> HardwareError:
        return HardwareError("JH-HW-ASSEMBLY", path, "", message, argument=argument)

    if assembly is not None:
        if assembly not in assemblies:
            raise fail(f"unknown assembly {assembly!r}; known: {sorted(assemblies)}", "--assembly")
        if target is not None and assemblies[assembly]["target"] != target:
            raise fail(f"assembly {assembly} is for {assemblies[assembly]['target']}, not {target}",
                       "--target")
        return assembly
    names = sorted(name for name, item in assemblies.items()
                   if target is None or item["target"] == target)
    if len(names) == 1:
        return names[0]
    if not names:
        raise fail(f"no assembly for target {target}", "--target")
    raise fail(f"choose one of the assemblies {names} with --assembly",
               "--assembly" if target is not None else "--target")


class Resolver:
    """One resolution of one assembly; resolve() returns the model."""

    def __init__(self, path: Path, registry: Registry | None = None,
                 hal: Catalogue | None = None) -> None:
        self.path = path.resolve()
        self.base = self.path.parent
        self.registry = registry or load_registry()
        self.hal = hal
        self.data: dict[str, Any] = {}
        self.catalogue = Catalogue()
        self.nodes: dict[str, Node] = {}

    # -- phases 1 and 2 ----------------------------------------------------
    def load(self) -> None:
        self.data = load_device_tree(self.path)
        self.catalogue = load_catalogue(self.path, self.data.get("bindings", []), self.hal)
        hardware_model.check_catalogue(self.catalogue, hardware_model.hal_features(),
                                       set(self.registry.capabilities))
        errors = Collector(self.base)
        for name, node in self.data["nodes"].items():
            at = pointer("nodes", name)
            if name in RESERVED_ROOTS:
                errors.add("JH-HW-MACRO-NAME", self.path, at, f"root node {name} is reserved")
            elif node["compatible"] not in self.catalogue.bindings:
                errors.add("JH-HW-BINDING", self.path, at + "/compatible",
                           f"unknown type {node['compatible']}")
            else:
                self._check_project_children(errors, node, self.catalogue.bindings[node["compatible"]],
                                             at)
        errors.check()
        self._check_macro_names()

    def _check_project_children(self, errors: Collector, entry: dict, binding: Binding,
                                at: str) -> None:
        declared = binding.data.get("children", {})
        for name, child in entry.get("children", {}).items():
            if name not in declared:
                errors.add("JH-HW-BINDING", self.path, f"{at}/children/{name}",
                           f"{binding.compatible} declares no child {name}")
                continue
            self._check_project_children(errors, child,
                                         self.catalogue.bindings[declared[name]["compatible"]],
                                         f"{at}/children/{name}")

    def _expand(self, path: str, binding: Binding, origin: tuple[Path, str],
                into: dict[str, tuple[Binding, tuple[Path, str]]]) -> None:
        into[path] = (binding, origin)
        for name, child in binding.data.get("children", {}).items():
            self._expand(f"{path}.{name}", self.catalogue.bindings[child["compatible"]],
                         (binding.path, pointer("children", name)), into)

    def declared_nodes(self) -> dict[str, tuple[Binding, tuple[Path, str]]]:
        nodes: dict[str, tuple[Binding, tuple[Path, str]]] = {}
        for name, node in self.data["nodes"].items():
            self._expand(name, self.catalogue.bindings[node["compatible"]],
                         (self.path, pointer("nodes", name)), nodes)
        return nodes

    def _check_macro_names(self) -> None:
        """Every name any assembly may produce, from distinct sources, is unique."""
        names: dict[str, tuple[tuple, tuple[Path, str], tuple[Path, str]]] = {}
        errors = Collector(self.base)
        order = lambda o: (os.path.relpath(o[0], self.base), hardware_schema.pointer_key(o[1]))

        def claim(name: str, entity: tuple, origin: tuple[Path, str],
                  member: tuple[Path, str] | None = None) -> None:
            """entity[:2] names the node or bus; a member origin is reported only
            when two members of the same node meet."""
            earlier = names.get(name)
            if earlier is None or earlier[0] == entity:
                names.setdefault(name, (entity, origin, member or origin))
                return
            same = earlier[0][:2] == entity[:2]
            first, second = sorted([earlier[2] if same else earlier[1],
                                    (member or origin) if same else origin], key=order)
            errors.add("JH-HW-MACRO-NAME", second[0], second[1],
                       f"{name} is also produced by {first[0]}:{first[1]}", (first[1],))

        for name in GLOBAL_MACROS:
            claim(name, ("global",), (self.path, ""))
        for capability in self.registry.capabilities:
            claim(f"JH_HW_HAS_{macro_token(capability)}", ("global",), (self.path, ""))
        nodes = self.declared_nodes()
        for assembly_name, assembly in sorted(self.data["assemblies"].items()):
            board = self.registry.boards.get(assembly["board"])
            if board is not None:
                for path, (binding, origin) in self.board_nodes(board).items():
                    nodes.setdefault(path, (binding, origin))
            for bus, item in assembly.get("buses", {}).items():
                token = f"JH_HW_BUS_{macro_token(bus)}_"
                origin = (self.path, pointer("assemblies", assembly_name, "buses", bus))
                for suffix in BUS_SUFFIXES:
                    claim(token + suffix, ("bus", bus), origin)
                for pin in item["pins"]:
                    for suffix in ("", "_CONNECTED"):
                        claim(f"{token}PIN_{macro_token(pin)}{suffix}", ("bus", bus, pin), origin)
        for path, (binding, origin) in sorted(nodes.items()):
            token = f"JH_HW_NODE_{node_token(path)}_"
            for suffix in NODE_SUFFIXES:
                claim(token + suffix, ("node", path), origin)
            for prop in binding.data.get("properties", {}):
                claim(f"{token}PROP_{macro_token(prop)}", ("node", path, "property", prop), origin,
                      (binding.path, pointer("properties", prop)))
            for signal in binding.data.get("signals", {}):
                for suffix in PIN_SUFFIXES:
                    claim(f"{token}PIN_{macro_token(signal)}{suffix}", ("node", path, "pin", signal),
                          origin, (binding.path, pointer("signals", signal)))
        errors.check()

    # -- board import -------------------------------------------------------
    def board_nodes(self, board: dict[str, Any]) -> dict[str, tuple[Binding, tuple[Path, str]]]:
        """Board devices as board.<id> nodes of HAL types."""
        nodes = {}
        source = self.registry.board_path(board["id"])
        for device_id, device in sorted(board.get("devices", {}).items()):
            compatible = self._board_type(device_id, device)
            if compatible in self.catalogue.bindings:
                nodes[f"board.{device_id}"] = (self.catalogue.bindings[compatible],
                                               (source, pointer("devices", device_id)))
        return nodes

    @staticmethod
    def _board_type(device_id: str, device: dict[str, Any]) -> str | None:
        if device["kind"] == "addressable":
            return "jaszczurhal,addressable-led"
        if device["kind"] == "bus-device":
            return BOARD_ROLE_TYPES.get(device.get("role", ""))
        return BOARD_GPIO_TYPES.get(device_id, (None,))[0]

    # -- phase 3 ------------------------------------------------------------
    def select(self, target: str | None = None, assembly: str | None = None) -> None:
        self.name = select_assembly(self.path, self.data, target, assembly)
        self.assembly = self.data["assemblies"][self.name]
        self.at = pointer("assemblies", self.name)
        board_id, target_id = self.assembly["board"], self.assembly["target"]
        board = self.registry.boards.get(board_id)
        if board is None:
            raise HardwareError("JH-HW-BOARD", self.path, self.at + "/board",
                                f"unknown board {board_id}")
        if target_id not in board["compatibleTargets"] or target_id not in self.registry.targets:
            raise HardwareError("JH-HW-TARGET-BOARD", self.path, self.at + "/target",
                                f"{board_id} supports {board['compatibleTargets']}, not {target_id}")
        self.board, self.target = board, self.registry.targets[target_id]
        self.board_source = self.registry.board_path(board_id)

    # -- phase 4 ------------------------------------------------------------
    def presence(self) -> None:
        errors = Collector(self.base)
        declared = self.declared_nodes()
        entries = self.assembly["nodes"]
        for path, item in sorted(entries.items()):
            if path.split(".")[0] == "board":
                errors.add("JH-HW-BOARD", self.path, pointer("assemblies", self.name, "nodes", path),
                           "board-owned devices cannot be changed by an assembly")
            elif path not in declared:
                errors.add("JH-HW-PRESENCE", self.path, pointer("assemblies", self.name, "nodes", path),
                           "no such root node or declared child")
        for path, (binding, origin) in sorted(declared.items()):
            node = Node(path, binding, origin, entry=entries.get(path, {}))
            at = pointer("assemblies", self.name, "nodes", path)
            if "." not in path:
                if "present" not in node.entry:
                    errors.add("JH-HW-PRESENCE", self.path, at + "/present",
                               "every root node states present in every assembly")
                node.present = node.entry.get("present", False)
            else:
                parent_path, _, child = path.rpartition(".")
                parent = self.nodes[parent_path]
                spec = parent.binding.data["children"][child]
                node.required = spec["required"]
                node.present = node.entry.get("present", parent.present)
                if not parent.present and node.entry:
                    first = "present" if node.entry.get("present") else sorted(node.entry)[0]
                    errors.add("JH-HW-PRESENCE", self.path, f"{at}/{first}",
                               f"{parent_path} is absent")
                elif parent.present and node.required and not node.present:
                    errors.add("JH-HW-PRESENCE", self.path, at + "/present",
                               f"{child} is a required child of {parent_path}")
            self.nodes[path] = node
        for path, (binding, origin) in self.board_nodes(self.board).items():
            device_id = path.split(".", 1)[1]
            self.nodes[path] = Node(path, binding, origin, board=True, present=True,
                                    board_facts=self.board["devices"][device_id])
        for name, item in self.assembly.get("nodes", {}).items():
            if "bus" in item and item["bus"] is not None and item["bus"] not in self.assembly.get("buses", {}):
                errors.add("JH-HW-TRANSPORT", self.path,
                           pointer("assemblies", self.name, "nodes", name, "bus"),
                           f"no bus {item['bus']} in this assembly")
            for signal, value in item.get("pins", {}).items():
                if isinstance(value, dict) and "net" in value and value["net"] not in self.assembly.get("nets", {}):
                    errors.add("JH-HW-NET-OWNER", self.path,
                               pointer("assemblies", self.name, "nodes", name, "pins", signal),
                               f"no net {value['net']} in this assembly")
        errors.check()

    # -- phase 5 ------------------------------------------------------------
    def properties(self) -> None:
        """Phase 5: properties, then the conditions and signals of every node
        whose properties resolved; one phase, one first error."""
        errors = Collector(self.base)
        invalid: set[str] = set()
        for path, node in sorted(self.nodes.items()):
            if not node.present:
                continue
            found = len(errors.errors)
            definitions = node.binding.data.get("properties", {})
            values = self._property_sources(node)
            for name, (value, source, at) in sorted(values.items()):
                definition = definitions.get(name)
                if definition is None:
                    errors.add("JH-HW-PROPERTY", source, at, f"{node.compatible} has no property {name}")
                    continue
                try:
                    check_property_value(node.binding, definition, value, at, source)
                except HardwareError as error:
                    errors.errors.append(error)
                    continue
                node.properties[name] = (value, source, at)
            for name, definition in sorted(definitions.items()):
                if definition.get("required") and name not in values:
                    errors.add("JH-HW-PROPERTY", self.path,
                               pointer("assemblies", self.name, "nodes", path, "properties", name),
                               f"{node.compatible} requires {name}")
            if len(errors.errors) > found:
                invalid.add(path)
        self._signals(errors, invalid)
        errors.check()

    def _property_sources(self, node: Node) -> dict[str, tuple[Any, Path, str]]:
        values: dict[str, tuple[Any, Path, str]] = {}
        binding = node.binding
        for name, definition in binding.data.get("properties", {}).items():
            if "default" in definition:
                values[name] = (definition["default"], binding.path,
                                pointer("properties", name, "default"))
        if node.board:
            for name, value in self._board_properties(node).items():
                values[name] = (value, self.board_source, node.origin[1])
            return values
        parts = node.path.split(".")
        if len(parts) > 1:
            parent = self.nodes[".".join(parts[:-1])]
            for name, value in parent.binding.data["children"][parts[-1]].get("properties", {}).items():
                values[name] = (value, parent.binding.path,
                                pointer("children", parts[-1], "properties", name))
        project = self.data["nodes"][parts[0]]
        at = pointer("nodes", parts[0])
        for part in parts[1:]:
            project = project.get("children", {}).get(part, {})
            at += pointer("children", part)
        for name, value in project.get("properties", {}).items():
            values[name] = (value, self.path, f"{at}/properties/{name}")
        for name, value in node.entry.get("properties", {}).items():
            values[name] = (value, self.path,
                            pointer("assemblies", self.name, "nodes", node.path, "properties", name))
        return values

    @staticmethod
    def _board_properties(node: Node) -> dict[str, Any]:
        facts = node.board_facts
        if "activeLevel" in facts:
            return {"activeLow": facts["activeLevel"] == "low"}
        if facts["kind"] == "bus-device":
            known = node.binding.data.get("properties", {})
            return {name: value for name, value in facts.get("attributes", {}).items() if name in known}
        return {}

    def _signals(self, errors: Collector, invalid: set[str]) -> None:
        """Conditions, required signals and module connections."""
        connected: dict[str, str] = {}      # child signal -> module signal wiring it
        self.module_owner: dict[str, str] = {}
        for path, node in self.nodes.items():
            for name, connection in node.binding.data.get("connections", {}).items():
                self.module_owner[f"{path}.{name}"] = f"{path}.{connection['owner']}"
                for target in [connection["owner"], *connection["aliases"]]:
                    connected[f"{path}.{target}"] = f"{path}.{name}"
        for path, node in sorted(self.nodes.items()):
            if not node.present or node.board or path in invalid:
                continue
            at = pointer("assemblies", self.name, "nodes", path)
            signals = node.binding.data.get("signals", {})
            for name in node.entry.get("pins", {}):
                if name not in signals:
                    errors.add("JH-HW-PROPERTY", self.path, f"{at}/pins/{name}",
                               f"{node.compatible} has no signal {name}")
            for name, signal in sorted(signals.items()):
                key = f"{path}.{name}"
                direct = name in node.entry.get("pins", {})
                if key in connected and direct:
                    errors.add("JH-HW-NET-OWNER", self.path, f"{at}/pins/{name}",
                               f"{connected[key]} already wires this signal")
                    continue
                assignment = self._assignment(key, connected)
                active = self._condition(node, signal)
                if not active:
                    if assignment and assignment["value"] is not None:
                        errors.add("JH-HW-PROPERTY", assignment["source"], assignment["at"],
                                   f"{name} must be unconnected while {signal['when']['property']} "
                                   f"is not {signal['when']['equals']!r}")
                    node.pins[name] = {"value": None, "at": f"{at}/pins/{name}", "source": self.path,
                                       "owner": key, "site": None}
                    continue
                if assignment is None:
                    if signal["required"]:
                        errors.add("JH-HW-PROPERTY", self.path, f"{at}/pins/{name}",
                                   f"{node.compatible} requires signal {name}")
                    assignment = {"value": None, "at": f"{at}/pins/{name}", "source": self.path,
                                  "site": None, "owner": key}
                node.pins[name] = assignment

    def _assignment(self, key: str, connected: dict[str, str]) -> dict[str, Any] | None:
        """Where a signal gets its endpoint: directly or through its module.
        The owner follows module connections down to the child that drives it."""
        path, _, name = key.rpartition(".")
        pins = self.nodes[path].entry.get("pins", {})
        if name in pins:
            owner = key
            while owner in self.module_owner:
                owner = self.module_owner[owner]
            return {"value": pins[name], "source": self.path,
                    "at": pointer("assemblies", self.name, "nodes", path, "pins", name),
                    "site": key, "owner": owner}
        if key in connected:
            return self._assignment(connected[key], connected)
        return None

    def _condition(self, node: Node, signal: dict[str, Any]) -> bool:
        condition = signal.get("when")
        if condition is None:
            return True
        value = node.properties.get(condition["property"], (None,))[0]
        return hardware_schema.json_equal(value, condition["equals"])

    # -- phase 6 ------------------------------------------------------------
    def clock(self) -> None:
        name = self.assembly["clock"]["tree"]
        trees = self.registry.clocks[self.target["id"]]["trees"]
        at = self.at + "/clock/tree"
        if name not in trees:
            raise HardwareError("JH-HW-CLOCK", self.path, at,
                                f"{self.target['id']} supports the trees {sorted(trees)}")
        tree = trees[name]
        problems = clock_registry.board_sources_satisfy(self.board, tree,
                                                        self.target.get("clockInputs", {}))
        if problems:
            raise HardwareError("JH-HW-CLOCK", self.path, at, "; ".join(problems))
        sources = {key: self.board["clockSources"][key] for key in tree["requiredSources"]}
        self.clock_model = {"tree": name, "backend": tree["backend"],
                            "frequenciesHz": tree["frequenciesHz"],
                            "parameters": tree["parameters"], "sources": sources}

    # -- phase 7 ------------------------------------------------------------
    def wiring(self) -> None:
        WiringCheck(self).run()

    # -- phase 8 ------------------------------------------------------------
    def status_led(self) -> None:
        selection = self.assembly["statusLed"]
        at = self.at + "/statusLed/node"
        path = selection["node"]

        def fail(message: str, where: str = at) -> HardwareError:
            return HardwareError("JH-HW-STATUS-LED", self.path, where, message)

        if path is None:
            if self.target["id"] != "mock":
                raise fail("only the mock target has no status LED")
            self.status_model = None
            return
        node = self.nodes.get(path)
        if node is None:
            raise fail(f"no node {path}")
        if not node.present:
            raise fail(f"{path} is absent")
        addressable = node.compatible == "jaszczurhal,addressable-led"
        signal = "data" if addressable else "out"
        if node.binding.data.get("domain") != "gpio" or signal not in node.pins:
            raise fail(f"{node.compatible} is not an LED")
        if not addressable and "activeLow" not in node.properties:
            raise fail(f"{node.compatible} has no known polarity")
        endpoint = node.pins[signal].get("endpoint")
        if endpoint is None:
            raise fail(f"{path}.{signal} is not connected")
        order = selection.get("pixelOrder")
        if order is not None:
            allowed = node.board_facts.get("pixelOrder", {}).get("allowed") if node.board else None
            if not addressable or (allowed is not None and order not in allowed):
                raise fail(f"{path} takes no pixel order {order}", self.at + "/statusLed/pixelOrder")
        model = {"node": path, "endpoint": endpoint, "signal": signal,
                 "kind": "addressable" if addressable else "gpio"}
        if addressable:
            model["pixelOrder"] = order or node.properties["pixelOrder"][0]
        else:
            model["activeLow"] = node.properties["activeLow"][0]
        self.status_model = model

    # -- providers ----------------------------------------------------------
    def providers(self) -> None:
        errors = Collector(self.base)
        chosen = self.assembly.get("providers", {})
        board_caps = {key for key, item in self.board.get("capabilities", {}).items()
                      if item.get("present")}
        for capability, path in sorted(chosen.items()):
            at = pointer("assemblies", self.name, "providers", capability)
            node = self.nodes.get(path)
            if capability not in self.registry.capabilities:
                errors.add("JH-HW-FEATURE", self.path, at, f"unknown capability {capability}")
            elif path == "board":
                if capability not in board_caps:
                    errors.add("JH-HW-FEATURE", self.path, at, f"{self.board['id']} has no {capability}")
            elif node is None:
                errors.add("JH-HW-FEATURE", self.path, at, f"no node {path}")
            elif capability not in node.binding.data.get("provides", []):
                errors.add("JH-HW-FEATURE", self.path, at, f"{node.compatible} does not provide {capability}")
        errors.check()
        providers: dict[str, str | None] = {}
        for capability in sorted(self.registry.capabilities):
            # The board itself is one candidate; several need an explicit choice.
            candidates = sorted(path for path, node in self.nodes.items()
                                if node.present and not node.board
                                and capability in node.binding.data.get("provides", []))
            candidates += ["board"] if capability in board_caps else []
            if capability in chosen:
                providers[capability] = chosen[capability]
            elif len(candidates) == 1:
                providers[capability] = candidates[0]
        capabilities = {}
        for capability in sorted(self.registry.capabilities):
            provider = providers.get(capability)
            usable = provider == "board" or (provider is not None and self.complete(provider))
            capabilities[capability] = bool(usable)
        self.provider_model, self.capability_model = providers, capabilities

    def complete(self, path: str) -> bool:
        """Present, with every required signal, bus, controller and address."""
        node = self.nodes[path]
        if not node.present:
            return False
        if node.board:
            return True
        for name, signal in node.binding.data.get("signals", {}).items():
            if signal["required"] and self._condition(node, signal) and node.pins[name].get("endpoint") is None:
                return False
        bus = node.binding.data.get("bus")
        if bus and bus["required"] and node.entry.get("bus") is None:
            return False
        controller = node.binding.data.get("controller")
        if controller and controller["required"] and node.entry.get("controller") is None:
            return False
        if bus and bus["kind"] == "i2c" and node.entry.get("address") is None:
            return False
        return all(child.present is False or child.required is False or self.complete(child_path)
                   for child_path, child in self.nodes.items()
                   if child_path.rpartition(".")[0] == path and not child.board)

    # -- everything ---------------------------------------------------------
    def resolve(self, target: str | None = None, assembly: str | None = None) -> dict[str, Any]:
        self.load()
        self.select(target, assembly)
        self.presence()
        self.properties()
        self.clock()
        self.wiring()
        self.status_led()
        self.providers()
        return build_model(self)


class WiringCheck:
    """Phase 7: endpoints, nets, reservations, controllers, addresses, transport."""

    def __init__(self, resolver: Resolver) -> None:
        self.r = resolver
        self.claims: dict[tuple, dict[str, Any]] = {}
        self.board_owner: dict[tuple, str] = {}
        gpio = resolver.board["gpio"]
        source = resolver.board_source
        self.valid = boards_module.expand_pin_set(source, "$.gpio.validPins",
                                                  resolver.target["gpio"]["validPins"])
        self.exposed = boards_module.expand_pin_set(source, "$.gpio.exposedPins", gpio["exposedPins"])
        self.reservations = gpio.get("reservations", {})
        self.hard: dict[Any, str] = {}
        self.soft: dict[Any, str] = {}
        for key, item in self.reservations.items():
            for endpoint in item["pins"]:
                (self.hard if item["strength"] == "hard" else self.soft)[endpoint["id"]] = key
        self.released: set[str] = set()
        self.nets: dict[str, dict[str, Any]] = {}

    def run(self) -> None:
        """Reservation keys first, as the endpoints depend on them; every other
        check of the phase reports into one collector."""
        self.releases()
        errors = Collector(self.r.base)
        self.board_claims()
        for step in (self.buses, self.net_endpoints, self.node_endpoints, self.net_owners,
                     self.controllers, self.transport):
            step(errors)
        errors.check()

    def releases(self) -> None:
        errors = Collector(self.r.base)
        for index, key in enumerate(self.r.assembly.get("releaseReservations", [])):
            at = pointer("assemblies", self.r.name, "releaseReservations", index)
            item = self.reservations.get(key)
            if item is None:
                errors.add("JH-HW-RESERVATION", self.r.path, at, f"{self.r.board['id']} has no reservation {key}")
            elif item["strength"] != "soft":
                errors.add("JH-HW-RESERVATION", self.r.path, at, f"{key} is a hard reservation")
            else:
                self.released.add(key)
        errors.check()

    def board_claims(self) -> None:
        """Board devices and the selected clock sources claim their pins first."""
        for path, node in sorted(self.r.nodes.items()):
            if not node.board:
                continue
            facts = node.board_facts
            endpoints = ({"out" if node.compatible.endswith("output") else "in": facts["endpoint"]}
                         if "endpoint" in facts else dict(facts.get("signals", {})))
            if node.compatible == "jaszczurhal,addressable-led":
                endpoints = {"data": facts["endpoint"]}
            signals = node.binding.data.get("signals", {})
            for name, endpoint in sorted(endpoints.items()):
                at = f"{node.origin[1]}/endpoint" if "endpoint" in facts else f"{node.origin[1]}/signals/{name}"
                if name in signals:
                    node.pins[name] = {"value": endpoint, "endpoint": endpoint, "source": self.r.board_source,
                                       "at": at, "site": f"{path}.{name}", "owner": f"{path}.{name}"}
                self.claim(self.key(endpoint), f"{path}.{name}", self.r.board_source, at, board=True)
            for name, signal in signals.items():
                node.pins.setdefault(name, {"value": None, "endpoint": None, "site": None,
                                            "owner": f"{path}.{name}", "source": self.r.board_source,
                                            "at": node.origin[1]})
        for key, source in sorted(self.r.clock_model["sources"].items()):
            for index, endpoint in enumerate(source["pins"]):
                self.claim(self.key(endpoint), f"clock.{key}", self.r.board_source,
                           pointer("clockSources", key, "pins", index), board=True)

    @staticmethod
    def key(endpoint: dict[str, Any]) -> tuple:
        if endpoint["domain"] == "soc-gpio":
            return ("soc", endpoint["id"])
        if endpoint["domain"] == "device-pin":
            return ("device", endpoint["node"], endpoint["index"])
        return (endpoint["domain"], endpoint.get("component"), endpoint["id"])

    def claim(self, key: tuple, owner: str, source: Path, at: str, *, board: bool = False,
              net: str | None = None, errors: Collector | None = None) -> None:
        earlier = self.claims.get(key)
        if earlier is None:
            self.claims[key] = {"owner": owner, "source": source, "at": at, "net": net, "board": board}
            if board:
                self.board_owner[key] = owner
            return
        if net is not None and (earlier["net"] == net or earlier["board"]):
            return
        if errors is not None:
            errors.add("JH-HW-PIN-CONFLICT", source, at,
                       f"{owner} and {earlier['owner']} use the same endpoint without a shared net",
                       (earlier["at"],))

    def normalize(self, value: Any, allowed: tuple[str, ...], source: Path, at: str,
                  errors: Collector) -> dict[str, Any] | None:
        """The explicit endpoint form, after domain, range and namespace checks."""
        if value is None:
            return None
        if not isinstance(value, dict):
            value = {"domain": "soc-gpio", "id": value}
        if value["domain"] not in allowed:
            errors.add("JH-HW-PIN-RANGE", source, at, f"{value['domain']} is not one of {list(allowed)}")
            return None
        if value["domain"] == "device-pin":
            provider = self.r.nodes.get(value["node"])
            if provider is None or not provider.present:
                errors.add("JH-HW-PRESENCE", source, f"{at}/node", f"{value['node']} is not a present node")
                return None
            count = provider.binding.data.get("pinCount")
            if count is None or value["index"] >= count:
                errors.add("JH-HW-PIN-RANGE", source, f"{at}/index",
                           f"{value['node']} has {count or 0} device pins")
                return None
            return dict(value)
        if value["id"] not in self.valid:
            errors.add("JH-HW-PIN-RANGE", source, at, f"{self.r.target['id']} has no GPIO {value['id']}")
            return None
        return {"domain": "soc-gpio", "id": value["id"]}

    def usable(self, endpoint: dict[str, Any], owner: str, source: Path, at: str,
               errors: Collector, board_owner: str | None = None) -> bool:
        """Hard reservation, then soft reservation, then exposure."""
        if endpoint["domain"] != "soc-gpio":
            return True
        pin = endpoint["id"]
        if pin in self.hard:
            item = self.reservations[self.hard[pin]]
            board_node = self.board_node(item["owner"])
            if board_owner is None or board_node is None or board_owner != board_node:
                errors.add("JH-HW-RESERVATION", source, at,
                           f"{pin} is hard-reserved by {item['owner']} ({self.hard[pin]})")
                return False
            return True
        if pin in self.soft and self.soft[pin] not in self.released:
            item = self.reservations[self.soft[pin]]
            board_node = self.board_node(item["owner"])
            if board_owner is None or board_owner != board_node:
                errors.add("JH-HW-RESERVATION", source, at,
                           f"{pin} is soft-reserved by {item['owner']}; release {self.soft[pin]} first")
                return False
        if pin not in self.exposed and ("soc", pin) not in self.board_owner and not (
                pin in self.soft and self.soft[pin] in self.released):
            errors.add("JH-HW-PIN-RANGE", source, at, f"{self.r.board['id']} does not expose {pin}")
            return False
        return True

    def board_node(self, owner: str) -> str | None:
        for path, node in self.r.nodes.items():
            if node.board and boards_module.board_device_owner(path.split(".", 1)[1]) == owner:
                return path
        return None

    def buses(self, errors: Collector) -> None:
        self.bus_endpoints: dict[str, dict[str, Any]] = {}
        for name, bus in sorted(self.r.assembly.get("buses", {}).items()):
            at = pointer("assemblies", self.r.name, "buses", name)
            required, optional = BUS_PINS[bus["kind"]]
            for pin in sorted(set(bus["pins"]) - required - optional):
                errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/pins/{pin}",
                           f"a {bus['kind']} bus has the pins {sorted(required | optional)}")
            for pin in sorted(required - set(bus["pins"])):
                errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/pins/{pin}",
                           f"a {bus['kind']} bus states {pin}, null when unused")
            endpoints = {}
            for pin, value in sorted(bus["pins"].items()):
                if isinstance(value, dict) and "net" in value:
                    errors.add("JH-HW-NET-OWNER", self.r.path, f"{at}/pins/{pin}",
                               "a bus owns its lines; reference the bus instead of a net")
                    continue
                endpoint = self.normalize(value, ("soc-gpio",), self.r.path, f"{at}/pins/{pin}", errors)
                endpoints[pin] = endpoint
                if endpoint is not None and self.usable(endpoint, f"bus.{name}.{pin}", self.r.path,
                                                         f"{at}/pins/{pin}", errors):
                    self.claim(self.key(endpoint), f"bus.{name}.{pin}", self.r.path, f"{at}/pins/{pin}",
                               errors=errors)
            self.bus_endpoints[name] = endpoints

    def net_endpoints(self, errors: Collector) -> None:
        for name, net in sorted(self.r.assembly.get("nets", {}).items()):
            at = pointer("assemblies", self.r.name, "nets", name, "endpoint")
            endpoint = self.normalize(net["endpoint"], ("soc-gpio", "device-pin"), self.r.path, at, errors)
            if endpoint is None:
                continue
            key = self.key(endpoint)
            owner_node = net["owner"].rpartition(".")[0]
            board_owner = owner_node if owner_node.split(".")[0] == "board" else None
            if not self.usable(endpoint, f"net.{name}", self.r.path, at, errors, board_owner):
                continue
            self.nets[name] = {"endpoint": endpoint, "owner": net["owner"], "members": [],
                               "board": self.board_owner.get(key)}
            self.claim(key, f"net.{name}", self.r.path, at, net=name, errors=errors)

    def node_endpoints(self, errors: Collector) -> None:
        for path, node in sorted(self.r.nodes.items()):
            if not node.present or node.board:
                continue
            signals = node.binding.data.get("signals", {})
            for name, assignment in sorted(node.pins.items()):
                value = assignment["value"]
                if isinstance(value, dict) and "net" in value:
                    net = self.nets.get(value["net"])
                    if net is not None:
                        net["members"].append(f"{path}.{name}")
                        assignment["endpoint"], assignment["net"] = net["endpoint"], value["net"]
                    continue
                if assignment["site"] != f"{path}.{name}":
                    continue   # wired through the module signal, claimed there
                endpoint = self.normalize(value, tuple(signals[name]["domains"]), assignment["source"],
                                          assignment["at"], errors)
                assignment["endpoint"] = endpoint
                if endpoint is not None and self.usable(endpoint, f"{path}.{name}", assignment["source"],
                                                         assignment["at"], errors):
                    self.claim(self.key(endpoint), f"{path}.{name}", assignment["source"],
                               assignment["at"], errors=errors)
        for path, node in self.r.nodes.items():
            for name, assignment in node.pins.items():
                site = assignment.get("site")
                if site and site != f"{path}.{name}" and "endpoint" not in assignment:
                    origin = self.r.nodes[site.rpartition(".")[0]].pins[site.rpartition(".")[2]]
                    assignment["endpoint"] = origin.get("endpoint")
                    if origin.get("net"):
                        assignment["net"] = origin["net"]
                        self.nets[origin["net"]]["members"].append(f"{path}.{name}")
                assignment.setdefault("endpoint", None)
        for path, node in sorted(self.r.nodes.items()):
            if node.board:
                continue
            signals = node.binding.data.get("signals", {})
            for name, assignment in sorted(node.pins.items()):
                endpoint = assignment.get("endpoint")
                # A net or a module connection hands over an endpoint the
                # signal itself never saw; its domains still apply.
                if endpoint is not None and endpoint["domain"] not in signals[name]["domains"]:
                    errors.add("JH-HW-PIN-RANGE", assignment["source"], assignment["at"],
                               f"{path}.{name} takes {signals[name]['domains']}, "
                               f"not a {endpoint['domain']} endpoint")
                    continue
                problem = renderers.endpoint_problem(node.compatible, name, renderers.encode_pin(endpoint))
                if problem:
                    errors.add("JH-HW-PIN-RANGE", assignment["source"], assignment["at"], problem)

    def net_owners(self, errors: Collector) -> None:
        for name, net in sorted(self.nets.items()):
            at = pointer("assemblies", self.r.name, "nets", name, "owner")
            owner = net["owner"]
            node_path, _, signal = owner.rpartition(".")
            node = self.r.nodes.get(node_path)
            members = set(net["members"])
            if net["board"]:
                members.add(net["board"])
            if node is None or signal not in node.binding.data.get("signals", {}) or owner not in members:
                errors.add("JH-HW-NET-OWNER", self.r.path, at, f"{owner} is not a signal wired to {name}")
                continue
            for member in sorted(members):
                member_path, _, member_signal = member.rpartition(".")
                assignment = self.r.nodes[member_path].pins[member_signal]
                assignment["owner"] = owner
                assignment["net"] = name
                assignment["endpoint"] = net["endpoint"]
            net["members"] = sorted(members)
            self.polarity(name, members, errors)

    def polarity(self, net: str, members: set[str], errors: Collector) -> None:
        """Outputs sharing one line drive it with the same active level."""
        outputs = []
        for member in members:
            path, _, signal = member.rpartition(".")
            node = self.r.nodes[path]
            if (node.binding.data["signals"][signal]["direction"] != "input"
                    and "activeLow" in node.properties):
                pin = node.pins[signal]
                outputs.append((os.path.relpath(pin["source"], self.r.base),
                                hardware_schema.pointer_key(pin["at"]), pin, node.properties["activeLow"][0], member))
        outputs.sort(key=lambda item: item[:2])
        for item in outputs[1:]:
            if item[3] != outputs[0][3]:
                errors.add("JH-HW-NET-OWNER", item[2]["source"], item[2]["at"],
                           f"{item[4]} and {outputs[0][4]} share {net} with opposite activeLow",
                           (outputs[0][2]["at"],))

    def controllers(self, errors: Collector) -> None:
        used: dict[tuple, str] = {}
        target = self.r.target["id"]

        def take(kind: str, index: int, source: Path, at: str) -> None:
            count = HAL_BUS_COUNT.get(kind, 0)
            if kind == "fdcan":
                count = sum(item["instances"] for item in boards_module.CAN_CONTROLLERS.values()
                            if target in item["targets"])
            if index >= count:
                errors.add("JH-HW-CONTROLLER", source, at, f"{target} has {count} {kind} controllers")
            elif (kind, index) in used:
                errors.add("JH-HW-CONTROLLER", source, at, f"{kind} {index} is already used by {used[(kind, index)]}",
                           (used[(kind, index)],))
            else:
                used[(kind, index)] = at

        for path, node in sorted(self.r.nodes.items()):
            if node.board and node.board_facts.get("kind") == "bus-device":
                bus = node.board_facts["bus"]
                take(bus["kind"], bus["index"], self.r.board_source, f"{node.origin[1]}/bus")
        for name, bus in sorted(self.r.assembly.get("buses", {}).items()):
            take(bus["kind"], bus["index"], self.r.path, pointer("assemblies", self.r.name, "buses", name, "index"))
        for path, node in sorted(self.r.nodes.items()):
            if not node.present or node.board:
                continue
            at = pointer("assemblies", self.r.name, "nodes", path)
            controller, wanted = node.entry.get("controller"), node.binding.data.get("controller")
            if controller is not None:
                if wanted is None or wanted["kind"] != controller["kind"]:
                    errors.add("JH-HW-CONTROLLER", self.r.path, f"{at}/controller",
                               f"{node.compatible} takes no {controller['kind']} controller")
                else:
                    take(controller["kind"], controller["index"], self.r.path, f"{at}/controller")
            address = node.entry.get("address")
            bus = node.binding.data.get("bus")
            if address is not None and (bus is None or bus["kind"] != "i2c"):
                errors.add("JH-HW-ADDRESS", self.r.path, f"{at}/address", f"{node.compatible} is not an I2C device")
        addresses: dict[tuple, str] = {}
        for path, node in sorted(self.r.nodes.items()):
            if node.present and not node.board and node.entry.get("address") is not None and node.entry.get("bus"):
                key = (node.entry["bus"], node.entry["address"])
                at = pointer("assemblies", self.r.name, "nodes", path, "address")
                if key in addresses:
                    errors.add("JH-HW-ADDRESS", self.r.path, at,
                               f"address {key[1]} on {key[0]} is already used", (addresses[key],))
                else:
                    addresses[key] = at

    def transport(self, errors: Collector) -> None:
        buses = self.r.assembly.get("buses", {})
        cs_pins: dict[tuple, str] = {}
        for name, bus in sorted(buses.items()):
            if bus["kind"] == "uart":
                problem = renderers.uart_frame_problem(self.r.target["id"], bus["frame"])
                if problem:
                    errors.add("JH-HW-TRANSPORT", self.r.path,
                               pointer("assemblies", self.r.name, "buses", name) + problem[0], problem[1])
        for path, node in sorted(self.r.nodes.items()):
            if not node.present or node.board:
                continue
            at = pointer("assemblies", self.r.name, "nodes", path)
            wanted, bus_name = node.binding.data.get("bus"), node.entry.get("bus")
            transport = node.entry.get("transport")
            if bus_name is None:
                if transport is not None:
                    errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/transport", "a node without a bus has no transport")
                continue
            bus = buses[bus_name]
            if wanted is None or wanted["kind"] != bus["kind"]:
                errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/bus",
                           f"{node.compatible} cannot use the {bus['kind']} bus {bus_name}")
                continue
            bus_at = pointer("assemblies", self.r.name, "buses", bus_name)
            settings = self.settings(bus, transport, at, bus_at, errors)
            if settings is None:
                continue
            limit = wanted.get("maximumFrequencyHz")
            if limit is not None and settings["frequencyHz"][0] > limit:
                errors.add("JH-HW-TRANSPORT", self.r.path, settings["frequencyHz"][1],
                           f"{node.compatible} takes at most {limit} Hz")
                continue
            problem = renderers.transport_problem(node.compatible, {k: v[0] for k, v in settings.items()},
                                                  {k: v[0] for k, v in node.properties.items()})
            if problem:
                errors.add("JH-HW-TRANSPORT", self.r.path, settings[problem[0]][1], problem[1])
                continue
            node.transport = {k: v[0] for k, v in settings.items()}
            cs = node.pins.get("cs", {}).get("endpoint")
            if bus["kind"] == "spi" and cs is not None:
                key = (bus_name, self.key(cs))
                if key in cs_pins:
                    errors.add("JH-HW-PIN-CONFLICT", self.r.path, node.pins["cs"]["at"],
                               f"{path} shares its chip select on {bus_name}", (cs_pins[key],))
                cs_pins[key] = node.pins["cs"]["at"]

    def settings(self, bus: dict, transport: dict | None, at: str, bus_at: str,
                 errors: Collector) -> dict[str, tuple[Any, str]] | None:
        """Effective transaction settings, each with the pointer that set it."""
        transport = transport or {}
        if bus["kind"] == "uart":
            if transport:
                errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/transport",
                           "UART consumers use the bus baud rate and frame")
                return None
            return {}
        if bus["kind"] == "i2c":
            for key in ("mode", "bitOrder"):
                if key in transport:
                    errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/transport/{key}", "I2C has no SPI settings")
                    return None
            if "frequencyHz" in transport and transport["frequencyHz"] != bus["frequencyHz"]:
                errors.add("JH-HW-TRANSPORT", self.r.path, f"{at}/transport/frequencyHz",
                           f"every I2C consumer runs at the bus rate {bus['frequencyHz']} Hz")
                return None
            return {"frequencyHz": (bus["frequencyHz"], f"{bus_at}/frequencyHz")}
        defaults = {"frequencyHz": bus["frequencyHz"], "mode": bus.get("mode", 0),
                    "bitOrder": bus.get("bitOrder", "msb")}
        return {key: (transport[key], f"{at}/transport/{key}") if key in transport
                else (value, f"{bus_at}/{key}") for key, value in defaults.items()}


def build_model(r: Resolver) -> dict[str, Any]:
    """The resolved hardware of the selected assembly (specification layout)."""
    nodes = {}
    for path, node in sorted(r.nodes.items()):
        item: dict[str, Any] = {"compatible": node.compatible, "domain": node.binding.data["domain"],
                                "present": node.present}
        if node.present:
            definitions = node.binding.data.get("properties", {})
            item["properties"] = {name: typed_value(definitions[name], value)
                                  for name, (value, _, _) in sorted(node.properties.items())}
            item["signals"] = {name: {"endpoint": pin.get("endpoint"),
                                      "owner": pin["owner"] if pin.get("endpoint") is not None else None,
                                      **({"net": pin["net"]} if pin.get("net") else {})}
                               for name, pin in sorted(node.pins.items())}
            for key in ("bus", "controller", "address"):
                if node.entry.get(key) is not None:
                    item[key] = node.entry[key]
            if node.board and node.board_facts.get("kind") == "bus-device":
                facts = node.board_facts
                item["bus"] = {**facts["bus"], "pins": {name: facts["signals"].get(name)
                                                        for name in sorted(BUS_PINS[facts["bus"]["kind"]][0])}}
                frequency = facts.get("attributes", {}).get("defaultSpiClockHz")
                if facts["bus"]["kind"] == "spi" and frequency is not None:
                    item["transport"] = {"frequencyHz": frequency, "mode": 0, "bitOrder": "msb"}
            if node.transport:
                item["transport"] = node.transport
        nodes[path] = item
    buses = {}
    for name, bus in sorted(r.assembly.get("buses", {}).items()):
        item = {key: value for key, value in bus.items() if key != "pins"}
        item["pins"] = {pin: normalize_static(value) for pin, value in sorted(bus["pins"].items())}
        buses[name] = item
    nets = {name: {"endpoint": normalize_static(net["endpoint"]), "owner": net["owner"]}
            for name, net in sorted(r.assembly.get("nets", {}).items())}
    gpio = r.board["gpio"].get("reservations", {})
    released = sorted(r.assembly.get("releaseReservations", []))
    reservations = {key: {"strength": item["strength"], "owner": item["owner"],
                          "pins": item["pins"], "released": key in released}
                    for key, item in sorted(gpio.items())}
    return hardware_output.exact_numbers({
        "selection": {"target": r.target["id"], "board": r.board["id"], "assembly": r.name},
        "hardware": {
            "nodes": nodes, "buses": buses, "nets": nets, "clock": r.clock_model,
            "statusLed": r.status_model, "capabilities": r.capability_model,
            "providers": r.provider_model, "reservations": reservations,
        },
    })


def normalize_static(value: Any) -> Any:
    if value is None or isinstance(value, dict):
        return value
    return {"domain": "soc-gpio", "id": value}


def typed_value(definition: dict[str, Any], value: Any) -> dict[str, Any]:
    item = {"value": value, "cType": definition["cType"]}
    if "unit" in definition:
        item["unit"] = definition["unit"]
    return item
