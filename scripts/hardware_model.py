"""Load component types: the HAL catalogue and a project's own bindings.

The HAL catalogue is every file in config/hardware/bindings/, named after its
compatible. A device tree lists project bindings one file at a time, relative
to the device tree; each file is read once however it is named. A compatible
may be defined once: a project file repeating a HAL type or another project
file fails. Errors carry the device tree specification's code, source file
and JSON Pointer.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
import os
from pathlib import Path, PurePosixPath
import re
from typing import Any
import unicodedata

import generate_hal_features
from hardware_literals import LiteralError, exact_value, ieee_value, integer_value
import hardware_schema

REPO_ROOT = Path(__file__).resolve().parents[1]
HAL_BINDINGS = REPO_ROOT / "config" / "hardware" / "bindings"
CAPABILITIES = REPO_ROOT / "boards" / "capabilities.json"

C_TYPES = {
    "bool": ("boolean", None, None),
    "uint8": ("integer", 0, 2**8 - 1), "uint16": ("integer", 0, 2**16 - 1),
    "uint32": ("integer", 0, 2**32 - 1), "uint64": ("integer", 0, 2**64 - 1),
    "int8": ("integer", -2**7, 2**7 - 1), "int16": ("integer", -2**15, 2**15 - 1),
    "int32": ("integer", -2**31, 2**31 - 1), "int64": ("integer", -2**63, 2**63 - 1),
    "float": ("number", None, None), "double": ("number", None, None),
    "string": ("string", None, None),
}


class HardwareError(ValueError):
    """A rejected input, reported as code, source and JSON Pointer."""

    def __init__(self, code: str, source: Path | str, pointer: str, message: str, *,
                 related: tuple[str, ...] = (), line: int | None = None,
                 argument: str | None = None) -> None:
        where = f"{source}:{line}" if line is not None else f"{source}:{pointer or '/'}"
        extra = f" (earlier: {', '.join(related)})" if related else ""
        extra += f" (argument {argument})" if argument else ""
        super().__init__(f"[{code}] {where}: {message}{extra}")
        self.code = code
        self.source = str(source)
        self.pointer = pointer
        self.detail = message
        self.related = tuple(related)
        self.line = line
        self.argument = argument


@dataclass
class Binding:
    compatible: str
    path: Path
    data: dict[str, Any]
    origin: str  # "hal" or "project"


@dataclass
class Catalogue:
    bindings: dict[str, Binding] = field(default_factory=dict)

    def add(self, binding: Binding) -> None:
        existing = self.bindings.get(binding.compatible)
        if existing is not None:
            raise HardwareError(
                "JH-HW-BINDING", binding.path, "/compatible",
                f"{binding.compatible} is already defined by {existing.path}")
        self.bindings[binding.compatible] = binding


def hal_features() -> set[str]:
    """Flags of the HAL feature registry, for requiresFeatures."""
    return set(generate_hal_features.load_registry(REPO_ROOT / "config").features)


def board_capabilities() -> set[str]:
    """Capabilities a board or a type may provide."""
    return set(json.loads(CAPABILITIES.read_text(encoding="utf-8"))["capabilities"])


def _unit_valid(unit: str) -> bool:
    """Printable NFC text without surrounding whitespace."""
    return (unit == unit.strip() and unit.isprintable()
            and unicodedata.normalize("NFC", unit) == unit)


def _schemas() -> hardware_schema.SchemaSet:
    return hardware_schema.SchemaSet()


def _read_binding(schemas: hardware_schema.SchemaSet, path: Path, origin: str) -> Binding:
    try:
        data = hardware_schema.load_json(path)
    except (OSError, ValueError) as error:
        raise HardwareError("JH-HW-SCHEMA", path, "", str(error)) from error
    first = schemas.first_error("binding.schema.json", data)
    if first is not None:
        raise HardwareError("JH-HW-SCHEMA", path, first.pointer, first.message)
    return Binding(data["compatible"], path, data, origin)


def load_hal_catalogue(directory: Path = HAL_BINDINGS) -> Catalogue:
    """Read the HAL types; each file is named after its compatible."""
    schemas = _schemas()
    catalogue = Catalogue()
    for path in sorted(directory.glob("*.json")):
        binding = _read_binding(schemas, path, "hal")
        expected = binding.compatible.replace(",", "-") + ".json"
        if path.name != expected:
            raise HardwareError("JH-HW-BINDING", path, "/compatible",
                                f"HAL type file must be named {expected}")
        catalogue.add(binding)
    return catalogue


def project_binding_paths(device_tree: Path, entries: list[str]) -> list[Path]:
    """Resolve the bindings array of a device tree; reject non-file forms."""
    resolved: list[Path] = []
    for index, entry in enumerate(entries):
        pointer = f"/bindings/{index}"
        posix = PurePosixPath(entry)
        if "\\" in entry or posix.is_absolute() or re.match(r"[A-Za-z][A-Za-z0-9+.-]*:", entry):
            raise HardwareError("JH-HW-BINDING", device_tree, pointer,
                                "a relative path with forward slashes")
        if any(char in entry for char in "*?["):
            raise HardwareError("JH-HW-BINDING", device_tree, pointer, "a single file, not a pattern")
        path = Path(os.path.normpath(device_tree.parent / Path(*posix.parts)))
        if not path.is_file():
            raise HardwareError("JH-HW-BINDING", device_tree, pointer, f"{entry} does not exist")
        if path.resolve() not in {p.resolve() for p in resolved}:
            resolved.append(path)
    return resolved


def load_catalogue(device_tree: Path, entries: list[str],
                   hal: Catalogue | None = None) -> Catalogue:
    """The HAL types plus the project bindings a device tree lists."""
    catalogue = Catalogue(dict((hal or load_hal_catalogue()).bindings))
    schemas = _schemas()
    for path in project_binding_paths(device_tree, entries):
        catalogue.add(_read_binding(schemas, path, "project"))
    check_catalogue(catalogue)
    return catalogue


def _value_fits(value: Any, c_type: str) -> bool:
    """The exact value fits the type; a float or double must round to a
    finite, nonzero (unless zero) value of that format."""
    json_type, low, high = C_TYPES[c_type]
    if json_type == "boolean":
        return isinstance(value, bool)
    if json_type == "string":
        return isinstance(value, str)
    try:
        if json_type == "integer":
            return low <= integer_value(value) <= high
        ieee_value(c_type, value)
    except LiteralError:
        return False
    return True


def check_property_definitions(binding: Binding) -> None:
    for name, definition in binding.data.get("properties", {}).items():
        pointer = f"/properties/{name}"
        c_type = definition["cType"]
        if C_TYPES[c_type][0] != definition["type"] and not (
                definition["type"] == "number" and c_type in ("float", "double")):
            raise HardwareError("JH-HW-BINDING", binding.path, f"{pointer}/cType",
                                f"{c_type} does not hold a JSON {definition['type']}")
        if "unit" in definition and not _unit_valid(definition["unit"]):
            raise HardwareError("JH-HW-BINDING", binding.path, f"{pointer}/unit",
                                "a printable unit without surrounding whitespace")
        for key in ("minimum", "maximum", "default"):
            if key in definition and not _value_fits(definition[key], c_type):
                raise HardwareError("JH-HW-BINDING", binding.path, f"{pointer}/{key}",
                                    f"a value that fits {c_type}")
        for index, member in enumerate(definition.get("enum", [])):
            if not _value_fits(member, c_type):
                raise HardwareError("JH-HW-BINDING", binding.path, f"{pointer}/enum/{index}",
                                    f"a value that fits {c_type}")
        if "default" in definition:
            check_property_value(binding, definition, definition["default"], f"{pointer}/default")


def check_property_value(binding: Binding, definition: dict, value: Any, pointer: str,
                         source: Path | None = None) -> None:
    source = source or binding.path
    if not _value_fits(value, definition["cType"]):
        raise HardwareError("JH-HW-PROPERTY", source, pointer, f"a {definition['cType']} value")
    if "enum" in definition and not any(
            hardware_schema.json_equal(value, member) for member in definition["enum"]):
        raise HardwareError("JH-HW-PROPERTY", source, pointer, f"one of {definition['enum']}")
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return
    if "minimum" in definition and exact_value(value) < exact_value(definition["minimum"]):
        raise HardwareError("JH-HW-PROPERTY", source, pointer, f"at least {definition['minimum']}")
    if "maximum" in definition and exact_value(value) > exact_value(definition["maximum"]):
        raise HardwareError("JH-HW-PROPERTY", source, pointer, f"at most {definition['maximum']}")


def check_catalogue(catalogue: Catalogue, features: set[str] | None = None,
                    capabilities: set[str] | None = None) -> None:
    """Semantic checks of every type beyond its schema."""
    for binding in catalogue.bindings.values():
        data = binding.data
        check_property_definitions(binding)
        signals = data.get("signals", {})
        for name, signal in signals.items():
            condition = signal.get("when")
            if condition and condition["property"] not in data.get("properties", {}):
                raise HardwareError("JH-HW-BINDING", binding.path, f"/signals/{name}/when/property",
                                    "a property of this type")
        children = data.get("children", {})
        for name, child in children.items():
            child_type = catalogue.bindings.get(child["compatible"])
            if child_type is None:
                raise HardwareError("JH-HW-BINDING", binding.path, f"/children/{name}/compatible",
                                    f"unknown type {child['compatible']}")
            for key, value in child.get("properties", {}).items():
                definition = child_type.data.get("properties", {}).get(key)
                pointer = f"/children/{name}/properties/{key}"
                if definition is None:
                    raise HardwareError("JH-HW-BINDING", binding.path, pointer,
                                        f"a property of {child['compatible']}")
                check_property_value(child_type, definition, value, pointer, binding.path)
        owners: dict[str, str] = {}
        for name, connection in data.get("connections", {}).items():
            if name not in signals:
                raise HardwareError("JH-HW-BINDING", binding.path, f"/connections/{name}",
                                    "a signal of this module")
            for index, target in enumerate([connection["owner"], *connection["aliases"]]):
                pointer = (f"/connections/{name}/owner" if index == 0
                           else f"/connections/{name}/aliases/{index - 1}")
                child, _, signal = target.partition(".")
                child_type = catalogue.bindings.get(children.get(child, {}).get("compatible", ""))
                if child_type is None or signal not in child_type.data.get("signals", {}):
                    raise HardwareError("JH-HW-BINDING", binding.path, pointer,
                                        "a signal of a declared child")
                if target in owners:
                    raise HardwareError("JH-HW-BINDING", binding.path, pointer,
                                        f"a signal not already connected through {owners[target]}")
                owners[target] = name
        if features is not None:
            for index, feature in enumerate(data.get("requiresFeatures", [])):
                if feature not in features:
                    raise HardwareError("JH-HW-BINDING", binding.path, f"/requiresFeatures/{index}",
                                        "a flag of the HAL feature registry")
        if capabilities is not None:
            for index, capability in enumerate(data.get("provides", [])):
                if capability not in capabilities:
                    raise HardwareError("JH-HW-BINDING", binding.path, f"/provides/{index}",
                                        "a capability of boards/capabilities.json")
    _check_cycles(catalogue)


def _check_cycles(catalogue: Catalogue) -> None:
    state: dict[str, int] = {}

    def visit(compatible: str, chain: list[str]) -> None:
        if state.get(compatible) == 2:
            return
        if state.get(compatible) == 1:
            binding = catalogue.bindings[chain[0]]
            raise HardwareError("JH-HW-BINDING", binding.path, "/children",
                                "cyclic expansion: " + " -> ".join([*chain, compatible]))
        state[compatible] = 1
        for child in catalogue.bindings[compatible].data.get("children", {}).values():
            if child["compatible"] in catalogue.bindings:
                visit(child["compatible"], [*chain, compatible])
        state[compatible] = 2

    for compatible in sorted(catalogue.bindings):
        visit(compatible, [])
