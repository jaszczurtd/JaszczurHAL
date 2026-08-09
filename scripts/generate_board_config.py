#!/usr/bin/env python3
"""Validate board descriptors and generate resolved build configuration."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any

import generate_hal_features


class DescriptorError(ValueError):
    """Descriptor validation failure with actionable context."""


COMMON_FIELDS = {
    "$schema",
    "schemaVersion",
    "kind",
    "id",
    "displayName",
    "description",
    "status",
}
TARGET_FIELDS = COMMON_FIELDS | {
    "architecture",
    "hal",
    "build",
    "gpio",
    "memory",
    "defaultBoard",
    "components",
}
BOARD_FIELDS = COMMON_FIELDS | {
    "compatibleTargets",
    "build",
    "hal",
    "memory",
    "gpio",
    "capabilities",
    "devices",
    "peripherals",
    "components",
    "constraints",
}
COMPONENT_REGISTRY = {
    "rp-native": {"providers": {"pico-sdk"}, "slot": "target-runtime"},
    "stm32g474-native": {
        "providers": {"jh-stm32-baremetal"},
        "slot": "target-runtime",
    },
    "host-mock": {"providers": {"host"}, "slot": "target-runtime"},
    "cyw43-pico-pio": {
        "providers": {"pico-sdk"},
        "slot": "network-radio-transport",
    },
    "cyw43-stm32-gspi": {
        "providers": {"jh-stm32-baremetal"},
        "slot": "network-radio-transport",
    },
    "cyw43-lwip": {
        "providers": {"pico-sdk", "jh-stm32-baremetal"},
        "slot": "network-stack",
    },
    "btstack-ble": {
        "providers": {"pico-sdk", "jh-stm32-baremetal"},
        "slot": "bluetooth-host-stack",
    },
    "sx126x-radio": {
        "providers": {"pico-sdk", "jh-stm32-baremetal"},
        "slot": "lora-radio-provider",
    },
}
SINGLE_ENDPOINT_DEVICE_KINDS = {
    "gpio": {"activeLevel"},
    "component-gpio": {"activeLevel"},
    "addressable": {"protocol", "pixelOrder"},
}
BUS_DEVICE_KIND = "bus-device"
# Declarative schema for multi-pin bus devices. Machinery below is role
# agnostic; every entry here is data describing one device role.
DEVICE_ROLE_REGISTRY = {
    "sx1262-radio": {
        "busKinds": {"spi"},
        "macroPrefix": "HAL_BOARD_LORA_RADIO",
        "signals": {
            "sck": "required",
            "mosi": "required",
            "miso": "required",
            "cs": "required",
            "reset": "required",
            "busy": "required",
            "dio1": "required",
            "rfSwitchA": "conditional",
            "rfSwitchB": "conditional",
        },
        "attributes": {
            "minFrequencyHz": {"type": "uint32", "min": 1},
            "maxFrequencyHz": {"type": "uint32", "min": 1},
            "maxSpiClockHz": {"type": "uint32", "min": 1},
            "defaultSpiClockHz": {"type": "uint32", "min": 1},
            "minTxPowerDbm": {"type": "int8"},
            "maxTxPowerDbm": {"type": "int8"},
            "regulator": {"type": "enum", "values": ["ldo", "dcdc"]},
            "rfSwitchMode": {
                "type": "enum",
                "values": [
                    "none",
                    "dio2",
                    "single-gpio",
                    "dio2-single-gpio",
                    "dual-gpio",
                ],
            },
            "tcxoControl": {"type": "enum", "values": ["none", "dio3"]},
            "tcxoVoltage": {
                "type": "enum",
                "values": [
                    "1v6",
                    "1v7",
                    "1v8",
                    "2v2",
                    "2v4",
                    "2v7",
                    "3v0",
                    "3v3",
                ],
                "conditional": True,
            },
            "tcxoStartupUs": {"type": "uint32", "min": 1, "conditional": True},
            "rfSwitchIdleLevelA": {"type": "bool", "conditional": True},
            "rfSwitchRxLevelA": {"type": "bool", "conditional": True},
            "rfSwitchTxLevelA": {"type": "bool", "conditional": True},
            "rfSwitchIdleLevelB": {"type": "bool", "conditional": True},
            "rfSwitchRxLevelB": {"type": "bool", "conditional": True},
            "rfSwitchTxLevelB": {"type": "bool", "conditional": True},
        },
        "ordered": [
            ("minFrequencyHz", "maxFrequencyHz"),
            ("minTxPowerDbm", "maxTxPowerDbm"),
            ("defaultSpiClockHz", "maxSpiClockHz"),
        ],
        "requires": [
            {
                "when": (
                    "rfSwitchMode",
                    {"single-gpio", "dio2-single-gpio", "dual-gpio"},
                ),
                "signals": ["rfSwitchA"],
                "attributes": [
                    "rfSwitchIdleLevelA",
                    "rfSwitchRxLevelA",
                    "rfSwitchTxLevelA",
                ],
            },
            {
                "when": ("rfSwitchMode", {"dual-gpio"}),
                "signals": ["rfSwitchB"],
                "attributes": [
                    "rfSwitchIdleLevelB",
                    "rfSwitchRxLevelB",
                    "rfSwitchTxLevelB",
                ],
            },
            {
                "when": ("tcxoControl", {"dio3"}),
                "attributes": ["tcxoVoltage", "tcxoStartupUs"],
            },
        ],
    }
}
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
MACRO_PATTERN = re.compile(r"^[A-Z][A-Z0-9_]*$")
CAMEL_PATTERN = re.compile(r"^[a-z][a-zA-Z0-9]*$")
STM32_PIN_PATTERN = re.compile(r"^P([A-Z])([0-9]|1[0-5])$")
FEATURE_PATTERN = re.compile(
    r"^(?:-D)?(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:=(.*))?$"
)


def fail(path: Path, json_path: str, value: Any, expected: str) -> None:
    raise DescriptorError(
        f"{path}: {json_path}: got {value!r}; expected {expected}"
    )


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise DescriptorError(f"{path}: invalid JSON: {error}") from error
    if not isinstance(value, dict):
        fail(path, "$", value, "an object")
    return value


def exact_fields(
    path: Path,
    json_path: str,
    value: Any,
    required: set[str],
    allowed: set[str],
) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(path, json_path, value, "an object")
    missing = sorted(required - value.keys())
    unknown = sorted(value.keys() - allowed)
    if missing:
        fail(path, json_path, missing, f"required fields {sorted(required)}")
    if unknown:
        fail(path, json_path, unknown, f"only fields {sorted(allowed)}")
    return value


def validate_common(path: Path, descriptor: dict[str, Any], kind: str) -> None:
    allowed = TARGET_FIELDS if kind == "target" else BOARD_FIELDS
    exact_fields(
        path,
        "$",
        descriptor,
        COMMON_FIELDS - {"$schema"},
        allowed,
    )
    if descriptor["schemaVersion"] != 1:
        fail(path, "$.schemaVersion", descriptor["schemaVersion"], "1")
    if descriptor["kind"] != kind:
        fail(path, "$.kind", descriptor["kind"], repr(kind))
    identifier = descriptor["id"]
    if not isinstance(identifier, str) or not ID_PATTERN.fullmatch(identifier):
        fail(path, "$.id", identifier, "a kebab-case identifier")
    if path.stem != identifier:
        fail(path, "$.id", identifier, f"the filename stem {path.stem!r}")
    for field in ("displayName", "description"):
        if not isinstance(descriptor[field], str) or not descriptor[field].strip():
            fail(path, f"$.{field}", descriptor[field], "a non-empty string")
    if descriptor["status"] not in {
        "supported",
        "experimental",
        "skeleton",
        "deprecated",
    }:
        fail(path, "$.status", descriptor["status"], "a supported status")


def endpoint_key(endpoint: dict[str, Any]) -> tuple[str, Any]:
    return endpoint["domain"], endpoint.get("id")


def expand_pin_set(path: Path, json_path: str, value: Any) -> set[Any]:
    pin_set = exact_fields(path, json_path, value, set(), {"ranges", "values"})
    if ("ranges" in pin_set) == ("values" in pin_set):
        fail(path, json_path, value, "exactly one of ranges or values")
    result: set[Any] = set()
    if "ranges" in pin_set:
        ranges = pin_set["ranges"]
        if not isinstance(ranges, list) or not ranges:
            fail(path, f"{json_path}.ranges", ranges, "a non-empty array")
        for index, item in enumerate(ranges):
            item_path = f"{json_path}.ranges[{index}]"
            exact_fields(path, item_path, item, {"first", "last"}, {"first", "last"})
            first, last = item["first"], item["last"]
            if (
                not isinstance(first, int)
                or isinstance(first, bool)
                or not isinstance(last, int)
                or isinstance(last, bool)
                or first < 0
                or last < first
            ):
                fail(path, item_path, item, "a non-negative ordered integer range")
            result.update(range(first, last + 1))
    else:
        values = pin_set["values"]
        if not isinstance(values, list) or not values:
            fail(path, f"{json_path}.values", values, "a non-empty array")
        for item in values:
            if not isinstance(item, (int, str)) or isinstance(item, bool):
                fail(path, f"{json_path}.values", item, "integer or string pin IDs")
            result.add(item)
        if len(result) != len(values):
            fail(path, f"{json_path}.values", values, "unique pin IDs")
    return result


def validate_endpoint(
    path: Path,
    json_path: str,
    endpoint: Any,
    valid_pins: set[Any],
    components: set[str],
) -> None:
    if not isinstance(endpoint, dict):
        fail(path, json_path, endpoint, "an endpoint object")
    domain = endpoint.get("domain")
    if domain == "soc-gpio":
        exact_fields(path, json_path, endpoint, {"domain", "id"}, {"domain", "id"})
        if endpoint["id"] not in valid_pins:
            fail(path, f"{json_path}.id", endpoint["id"], "a target valid pin")
    elif domain == "component-gpio":
        exact_fields(
            path,
            json_path,
            endpoint,
            {"domain", "component", "id", "halPin"},
            {"domain", "component", "id", "halPin"},
        )
        component_known = endpoint["component"] in components
        if endpoint["component"] == "cyw43" and {
            "cyw43-pico-pio",
            "cyw43-stm32-gspi",
        } & components:
            component_known = True
        if not component_known:
            fail(
                path,
                f"{json_path}.component",
                endpoint["component"],
                "a component selected by the resolved profile",
            )
        if not isinstance(endpoint["id"], int) or not isinstance(
            endpoint["halPin"], int
        ):
            fail(path, json_path, endpoint, "integer component id and halPin")
    else:
        fail(path, f"{json_path}.domain", domain, "soc-gpio or component-gpio")


def validate_role_registry() -> None:
    """Guard the declarative role specs against unreachable conditionals."""
    for role, spec in DEVICE_ROLE_REGISTRY.items():
        if not ID_PATTERN.fullmatch(role):
            raise DescriptorError(f"device role {role!r}: expected a kebab-case ID")
        if not MACRO_PATTERN.fullmatch(spec["macroPrefix"]):
            raise DescriptorError(f"device role {role!r}: expected a C macro prefix")
        gated_signals: set[str] = set()
        gated_attributes: set[str] = set()
        for rule in spec["requires"]:
            gate, allowed = rule["when"]
            gate_spec = spec["attributes"].get(gate)
            if gate_spec is None or gate_spec["type"] != "enum":
                raise DescriptorError(
                    f"device role {role!r}: gate {gate!r} is not an enum attribute"
                )
            unknown = allowed - set(gate_spec["values"])
            if unknown:
                raise DescriptorError(
                    f"device role {role!r}: gate {gate!r} values {sorted(unknown)} "
                    "are not declared by the attribute"
                )
            if gate_spec.get("conditional"):
                raise DescriptorError(
                    f"device role {role!r}: gate {gate!r} must be required"
                )
            gated_signals.update(rule.get("signals", ()))
            gated_attributes.update(rule.get("attributes", ()))
        for name, mode in spec["signals"].items():
            if not CAMEL_PATTERN.fullmatch(name):
                raise DescriptorError(
                    f"device role {role!r}: signal {name!r} is not camelCase"
                )
            if mode not in {"required", "conditional"}:
                raise DescriptorError(
                    f"device role {role!r}: signal {name!r} has mode {mode!r}"
                )
            if (mode == "conditional") != (name in gated_signals):
                raise DescriptorError(
                    f"device role {role!r}: signal {name!r} must be conditional "
                    "exactly when a requires rule gates it"
                )
        for name, attribute in spec["attributes"].items():
            if not CAMEL_PATTERN.fullmatch(name):
                raise DescriptorError(
                    f"device role {role!r}: attribute {name!r} is not camelCase"
                )
            if attribute["type"] not in {"uint32", "int8", "bool", "enum"}:
                raise DescriptorError(
                    f"device role {role!r}: attribute {name!r} has an unknown type"
                )
            if attribute["type"] == "enum" and not attribute.get("values"):
                raise DescriptorError(
                    f"device role {role!r}: attribute {name!r} declares no values"
                )
            if bool(attribute.get("conditional")) != (name in gated_attributes):
                raise DescriptorError(
                    f"device role {role!r}: attribute {name!r} must be conditional "
                    "exactly when a requires rule gates it"
                )
        for lower, upper in spec["ordered"]:
            for name in (lower, upper):
                if spec["attributes"].get(name, {}).get("type") not in {
                    "uint32",
                    "int8",
                }:
                    raise DescriptorError(
                        f"device role {role!r}: ordered pair uses non-numeric "
                        f"attribute {name!r}"
                    )


def validate_device_attributes(
    path: Path,
    json_path: str,
    role: str,
    spec: dict[str, Any],
    attributes: Any,
    active: set[str],
) -> None:
    if not isinstance(attributes, dict):
        fail(path, json_path, attributes, "an object")
    expected = {
        name
        for name, attribute in spec["attributes"].items()
        if not attribute.get("conditional") or name in active
    }
    missing = sorted(expected - attributes.keys())
    unknown = sorted(attributes.keys() - expected)
    if missing:
        fail(path, json_path, missing, f"required {role} attributes {missing}")
    if unknown:
        fail(
            path,
            json_path,
            unknown,
            f"only attributes required by the active {role} configuration",
        )
    for name, value in attributes.items():
        attribute = spec["attributes"][name]
        item_path = f"{json_path}.{name}"
        kind = attribute["type"]
        if kind == "bool":
            if not isinstance(value, bool):
                fail(path, item_path, value, "a boolean")
        elif kind == "enum":
            if value not in attribute["values"]:
                fail(path, item_path, value, f"one of {attribute['values']}")
        else:
            low, high = (0, 0xFFFFFFFF) if kind == "uint32" else (-128, 127)
            low = max(low, attribute.get("min", low))
            if not isinstance(value, int) or isinstance(value, bool):
                fail(path, item_path, value, f"an {kind} integer")
            if value < low or value > high:
                fail(path, item_path, value, f"an {kind} integer in [{low}, {high}]")
    for lower, upper in spec["ordered"]:
        if lower in attributes and upper in attributes:
            if attributes[lower] > attributes[upper]:
                fail(
                    path,
                    f"{json_path}.{lower}",
                    attributes[lower],
                    f"a value not greater than {upper} ({attributes[upper]})",
                )


def validate_bus_device(
    path: Path,
    json_path: str,
    device: dict[str, Any],
    valid_pins: set[Any],
    components: set[str],
    hard_reserved: set[Any],
) -> None:
    exact_fields(
        path,
        json_path,
        device,
        {"kind", "role", "bus", "signals", "attributes"},
        {"kind", "role", "bus", "signals", "attributes"},
    )
    role = device["role"]
    if role not in DEVICE_ROLE_REGISTRY:
        fail(path, f"{json_path}.role", role, "a known device role")
    spec = DEVICE_ROLE_REGISTRY[role]
    bus = exact_fields(
        path, f"{json_path}.bus", device["bus"], {"kind", "index"}, {"kind", "index"}
    )
    if bus["kind"] not in spec["busKinds"]:
        fail(
            path,
            f"{json_path}.bus.kind",
            bus["kind"],
            f"one of {sorted(spec['busKinds'])}",
        )
    if not isinstance(bus["index"], int) or isinstance(bus["index"], bool):
        fail(path, f"{json_path}.bus.index", bus["index"], "an integer bus index")
    if bus["index"] < 0 or bus["index"] > 0xFE:
        fail(path, f"{json_path}.bus.index", bus["index"], "a bus index in [0, 254]")
    signals = device["signals"]
    if not isinstance(signals, dict):
        fail(path, f"{json_path}.signals", signals, "an object")
    unknown_signals = sorted(signals.keys() - spec["signals"].keys())
    if unknown_signals:
        fail(
            path,
            f"{json_path}.signals",
            unknown_signals,
            f"only signals declared by role {role}",
        )
    attributes = device["attributes"]
    declared = attributes if isinstance(attributes, dict) else {}
    active_signals: set[str] = set()
    active_attributes: set[str] = set()
    for rule in spec["requires"]:
        gate, allowed = rule["when"]
        if declared.get(gate) in allowed:
            active_signals.update(rule.get("signals", ()))
            active_attributes.update(rule.get("attributes", ()))
    validate_device_attributes(
        path,
        f"{json_path}.attributes",
        role,
        spec,
        attributes,
        active_attributes,
    )
    expected_signals = {
        name
        for name, mode in spec["signals"].items()
        if mode == "required" or name in active_signals
    }
    missing_signals = sorted(expected_signals - signals.keys())
    extra_signals = sorted(signals.keys() - expected_signals)
    if missing_signals:
        fail(
            path,
            f"{json_path}.signals",
            missing_signals,
            f"required {role} signals {missing_signals}",
        )
    if extra_signals:
        fail(
            path,
            f"{json_path}.signals",
            extra_signals,
            f"only signals required by the active {role} configuration",
        )
    seen: dict[tuple[Any, ...], str] = {}
    for name in sorted(signals):
        signal_path = f"{json_path}.signals.{name}"
        endpoint = signals[name]
        validate_endpoint(path, signal_path, endpoint, valid_pins, components)
        # Component GPIO IDs are only unique per component, so keep it in the key.
        key = (*endpoint_key(endpoint), endpoint.get("component"))
        if key in seen:
            fail(path, signal_path, endpoint, f"a pin not already used by {seen[key]}")
        seen[key] = name
        if endpoint["domain"] == "soc-gpio" and endpoint["id"] not in hard_reserved:
            fail(
                path,
                signal_path,
                endpoint["id"],
                "a pin covered by a hard gpio reservation",
            )


def validate_device(
    path: Path,
    json_path: str,
    device: Any,
    valid_pins: set[Any],
    components: set[str],
    hard_reserved: set[Any],
) -> None:
    if not isinstance(device, dict) or not isinstance(device.get("kind"), str):
        fail(path, json_path, device, "a device object with a kind")
    kind = device["kind"]
    if kind == BUS_DEVICE_KIND:
        validate_bus_device(
            path, json_path, device, valid_pins, components, hard_reserved
        )
        return
    if kind not in SINGLE_ENDPOINT_DEVICE_KINDS:
        fail(
            path,
            f"{json_path}.kind",
            kind,
            f"one of {sorted({BUS_DEVICE_KIND, *SINGLE_ENDPOINT_DEVICE_KINDS})}",
        )
    exact_fields(
        path,
        json_path,
        device,
        {"kind", "endpoint"},
        {"kind", "endpoint", *SINGLE_ENDPOINT_DEVICE_KINDS[kind]},
    )
    validate_endpoint(
        path, f"{json_path}.endpoint", device["endpoint"], valid_pins, components
    )


def validate_components(
    path: Path, json_path: str, components: Any, provider: str
) -> set[str]:
    if not isinstance(components, dict):
        fail(path, json_path, components, "an object")
    slots: dict[str, str] = {}
    for component_id, config in components.items():
        if component_id not in COMPONENT_REGISTRY:
            fail(path, f"{json_path}.{component_id}", config, "a known component ID")
        entry = COMPONENT_REGISTRY[component_id]
        if provider not in entry["providers"]:
            fail(
                path,
                f"{json_path}.{component_id}",
                provider,
                f"one of {sorted(entry['providers'])}",
            )
        exact_fields(
            path,
            f"{json_path}.{component_id}",
            config,
            {"mode"},
            {"mode"},
        )
        if config["mode"] not in {"required", "board-owned", "feature-gated"}:
            fail(
                path,
                f"{json_path}.{component_id}.mode",
                config["mode"],
                "required, board-owned, or feature-gated",
            )
        slot = entry["slot"]
        if slot in slots and slots[slot] != component_id:
            fail(
                path,
                json_path,
                [slots[slot], component_id],
                f"one component in exclusive slot {slot}",
            )
        slots[slot] = component_id
    return set(components)


def validate_target(path: Path, target: dict[str, Any]) -> set[Any]:
    validate_common(path, target, "target")
    architecture = exact_fields(
        path,
        "$.architecture",
        target["architecture"],
        {"vendor", "family", "soc", "isa", "cores"},
        {"vendor", "family", "soc", "isa", "cores"},
    )
    if not isinstance(architecture["cores"], int) or architecture["cores"] < 1:
        fail(path, "$.architecture.cores", architecture["cores"], "a positive integer")
    hal = exact_fields(
        path, "$.hal", target["hal"], {"targetSelector"}, {"targetSelector"}
    )
    if not MACRO_PATTERN.fullmatch(hal["targetSelector"]):
        fail(path, "$.hal.targetSelector", hal["targetSelector"], "a C macro")
    build = exact_fields(
        path,
        "$.build",
        target["build"],
        {"provider", "recipe"},
        {"provider", "recipe", "platform"},
    )
    if build["provider"] not in {"pico-sdk", "jh-stm32-baremetal", "host"}:
        fail(path, "$.build.provider", build["provider"], "a supported provider")
    if build["provider"] == "pico-sdk" and "platform" not in build:
        fail(path, "$.build.platform", None, "a Pico SDK platform")
    gpio = exact_fields(
        path,
        "$.gpio",
        target["gpio"],
        {"pinIdFormat", "validPins", "halEncoding"},
        {"pinIdFormat", "validPins", "halEncoding"},
    )
    valid_pins = expand_pin_set(path, "$.gpio.validPins", gpio["validPins"])
    if gpio["pinIdFormat"] == "integer":
        if any(not isinstance(pin, int) for pin in valid_pins):
            fail(path, "$.gpio.validPins", sorted(valid_pins), "integer pin IDs")
    elif gpio["pinIdFormat"] == "stm32-port-pin":
        if any(
            not isinstance(pin, str) or not STM32_PIN_PATTERN.fullmatch(pin)
            for pin in valid_pins
        ):
            fail(path, "$.gpio.validPins", sorted(valid_pins), "STM32 PxN pin IDs")
    else:
        fail(
            path,
            "$.gpio.pinIdFormat",
            gpio["pinIdFormat"],
            "integer or stm32-port-pin",
        )
    exact_fields(
        path,
        "$.gpio.halEncoding",
        gpio["halEncoding"],
        {"kind"},
        {"kind"},
    )
    memory = exact_fields(
        path, "$.memory", target["memory"], {"regions"}, {"regions"}
    )
    if not isinstance(memory["regions"], dict):
        fail(path, "$.memory.regions", memory["regions"], "an object")
    for region_id, region in memory["regions"].items():
        exact_fields(
            path,
            f"$.memory.regions.{region_id}",
            region,
            {"kind", "sizeBytes", "dmaCapable"},
            {"kind", "sizeBytes", "dmaCapable"},
        )
        if region["kind"] not in {"ram", "flash"}:
            fail(path, f"$.memory.regions.{region_id}.kind", region["kind"], "ram or flash")
        if not isinstance(region["sizeBytes"], int) or region["sizeBytes"] <= 0:
            fail(
                path,
                f"$.memory.regions.{region_id}.sizeBytes",
                region["sizeBytes"],
                "a positive integer",
            )
        if not isinstance(region["dmaCapable"], bool):
            fail(
                path,
                f"$.memory.regions.{region_id}.dmaCapable",
                region["dmaCapable"],
                "a boolean",
            )
    if not isinstance(target["defaultBoard"], str):
        fail(path, "$.defaultBoard", target["defaultBoard"], "a board ID")
    validate_components(path, "$.components", target["components"], build["provider"])
    return valid_pins


def validate_board(
    path: Path,
    board: dict[str, Any],
    targets: dict[str, dict[str, Any]],
    target_pins: dict[str, set[Any]],
    capabilities: dict[str, Any],
) -> None:
    validate_common(path, board, "board")
    compatible = board["compatibleTargets"]
    if not isinstance(compatible, list) or not compatible:
        fail(path, "$.compatibleTargets", compatible, "a non-empty target ID array")
    if len(set(compatible)) != len(compatible):
        fail(path, "$.compatibleTargets", compatible, "unique target IDs")
    for target_id in compatible:
        if target_id not in targets:
            fail(path, "$.compatibleTargets", target_id, "a known target ID")
    build = exact_fields(
        path,
        "$.build",
        board["build"],
        {"provider"},
        {"provider", "board"},
    )
    providers = {targets[target_id]["build"]["provider"] for target_id in compatible}
    if providers != {build["provider"]}:
        fail(path, "$.build.provider", build["provider"], f"target provider {providers}")
    if build["provider"] == "pico-sdk" and "board" not in build:
        fail(path, "$.build.board", None, "a Pico SDK board")
    hal = exact_fields(
        path,
        "$.hal",
        board["hal"],
        {"profileId", "selector", "runtimeName", "legacySelectors"},
        {"profileId", "selector", "runtimeName", "legacySelectors"},
    )
    if not isinstance(hal["profileId"], int) or hal["profileId"] < 1:
        fail(path, "$.hal.profileId", hal["profileId"], "a positive integer")
    if not isinstance(hal["selector"], str) or not MACRO_PATTERN.fullmatch(
        hal["selector"]
    ):
        fail(path, "$.hal.selector", hal["selector"], "a C macro")
    if not isinstance(hal["runtimeName"], str) or not hal["runtimeName"]:
        fail(path, "$.hal.runtimeName", hal["runtimeName"], "a non-empty string")
    if not isinstance(hal["legacySelectors"], list) or any(
        not isinstance(item, str) or not MACRO_PATTERN.fullmatch(item)
        for item in hal["legacySelectors"]
    ):
        fail(path, "$.hal.legacySelectors", hal["legacySelectors"], "C macro array")
    memory = exact_fields(
        path, "$.memory", board["memory"], {"flash"}, {"flash"}
    )
    flash = exact_fields(
        path,
        "$.memory.flash",
        memory["flash"],
        {"source", "expectedBytes"},
        {"source", "expectedBytes"},
    )
    if flash["source"] not in {"sdk", "target", "none"}:
        fail(path, "$.memory.flash.source", flash["source"], "sdk, target, or none")
    if not isinstance(flash["expectedBytes"], int) or flash["expectedBytes"] < 0:
        fail(
            path,
            "$.memory.flash.expectedBytes",
            flash["expectedBytes"],
            "a non-negative integer",
        )
    component_ids = validate_components(
        path, "$.components", board["components"], build["provider"]
    )
    target_component_ids = set()
    for target_id in compatible:
        target_component_ids.update(targets[target_id]["components"])
    resolved_components = component_ids | target_component_ids
    gpio = exact_fields(
        path,
        "$.gpio",
        board["gpio"],
        {"exposedPins", "reservations", "aliases"},
        {"exposedPins", "connectors", "reservations", "aliases"},
    )
    exposed = expand_pin_set(path, "$.gpio.exposedPins", gpio["exposedPins"])
    valid_union = set().union(*(target_pins[target_id] for target_id in compatible))
    if not exposed <= valid_union:
        fail(path, "$.gpio.exposedPins", sorted(exposed - valid_union), "target valid pins")
    for connector_id, connector in gpio.get("connectors", {}).items():
        exact_fields(
            path,
            f"$.gpio.connectors.{connector_id}",
            connector,
            {"pins"},
            {"pins"},
        )
        if not isinstance(connector["pins"], list):
            fail(path, f"$.gpio.connectors.{connector_id}.pins", connector["pins"], "an array")
        for index, endpoint in enumerate(connector["pins"]):
            validate_endpoint(
                path,
                f"$.gpio.connectors.{connector_id}.pins[{index}]",
                endpoint,
                valid_union,
                resolved_components,
            )
    if not isinstance(gpio["reservations"], dict):
        fail(path, "$.gpio.reservations", gpio["reservations"], "an object")
    hard_reserved: set[Any] = set()
    for reservation_id, reservation in gpio["reservations"].items():
        exact_fields(
            path,
            f"$.gpio.reservations.{reservation_id}",
            reservation,
            {"pins", "owner", "strength", "reason"},
            {"pins", "owner", "strength", "reason"},
        )
        if reservation["strength"] not in {"hard", "soft"}:
            fail(
                path,
                f"$.gpio.reservations.{reservation_id}.strength",
                reservation["strength"],
                "hard or soft",
            )
        if not isinstance(reservation["pins"], list) or not reservation["pins"]:
            fail(path, f"$.gpio.reservations.{reservation_id}.pins", reservation["pins"], "a non-empty array")
        for index, endpoint in enumerate(reservation["pins"]):
            validate_endpoint(
                path,
                f"$.gpio.reservations.{reservation_id}.pins[{index}]",
                endpoint,
                valid_union,
                resolved_components,
            )
            if reservation["strength"] == "hard" and endpoint["domain"] == "soc-gpio":
                hard_reserved.add(endpoint["id"])
    if not isinstance(gpio["aliases"], dict):
        fail(path, "$.gpio.aliases", gpio["aliases"], "an object")
    for alias_id, alias in gpio["aliases"].items():
        exact_fields(
            path,
            f"$.gpio.aliases.{alias_id}",
            alias,
            {"endpoint"},
            {"endpoint"},
        )
        validate_endpoint(
            path,
            f"$.gpio.aliases.{alias_id}.endpoint",
            alias["endpoint"],
            valid_union,
            resolved_components,
        )
    if not isinstance(board["capabilities"], dict):
        fail(path, "$.capabilities", board["capabilities"], "an object")
    unknown_capabilities = set(board["capabilities"]) - set(capabilities)
    if unknown_capabilities:
        fail(path, "$.capabilities", sorted(unknown_capabilities), "known capability IDs")
    missing_capabilities = {
        capability_id
        for capability_id, config in capabilities.items()
        if config.get("status") != "reserved"
    } - set(board["capabilities"])
    if missing_capabilities:
        fail(path, "$.capabilities", sorted(missing_capabilities), "all active capability IDs")
    for capability_id, value in board["capabilities"].items():
        exact_fields(
            path,
            f"$.capabilities.{capability_id}",
            value,
            {"present"},
            {"present"},
        )
        if not isinstance(value["present"], bool):
            fail(path, f"$.capabilities.{capability_id}.present", value["present"], "a boolean")
    if board["capabilities"].get("cyw43", {}).get("present"):
        has_transport = bool(
            {"cyw43-pico-pio", "cyw43-stm32-gspi"} & resolved_components
        )
        if not has_transport or "cyw43-lwip" not in resolved_components:
            fail(path, "$.capabilities.cyw43", True, "CYW43 components")
    if not isinstance(board["devices"], dict):
        fail(path, "$.devices", board["devices"], "an object")
    roles_seen: dict[str, str] = {}
    for device_id, device in board["devices"].items():
        if not CAMEL_PATTERN.fullmatch(device_id):
            fail(path, f"$.devices.{device_id}", device_id, "a camelCase device ID")
        validate_device(
            path,
            f"$.devices.{device_id}",
            device,
            valid_union,
            resolved_components,
            hard_reserved,
        )
        role = device.get("role")
        if role is not None:
            if role in roles_seen:
                fail(
                    path,
                    f"$.devices.{device_id}.role",
                    role,
                    f"a role not already declared by {roles_seen[role]}",
                )
            roles_seen[role] = device_id
    if not isinstance(board["peripherals"], dict):
        fail(path, "$.peripherals", board["peripherals"], "an object")


def load_registry(
    boards_root: Path,
) -> tuple[
    dict[str, dict[str, Any]],
    dict[str, dict[str, Any]],
    dict[str, Any],
]:
    validate_role_registry()
    capability_document = load_json(boards_root / "capabilities.json")
    exact_fields(
        boards_root / "capabilities.json",
        "$",
        capability_document,
        {"schemaVersion", "capabilities"},
        {"schemaVersion", "capabilities"},
    )
    if capability_document["schemaVersion"] != 1:
        fail(
            boards_root / "capabilities.json",
            "$.schemaVersion",
            capability_document["schemaVersion"],
            "1",
        )
    capabilities = capability_document["capabilities"]
    if not isinstance(capabilities, dict):
        fail(boards_root / "capabilities.json", "$.capabilities", capabilities, "an object")
    bits: set[int] = set()
    macros: set[str] = set()
    for capability_id, config in capabilities.items():
        if not ID_PATTERN.fullmatch(capability_id):
            fail(boards_root / "capabilities.json", f"$.capabilities.{capability_id}", capability_id, "a kebab-case ID")
        exact_fields(
            boards_root / "capabilities.json",
            f"$.capabilities.{capability_id}",
            config,
            {"bit", "macro", "stateModel", "runtimeOwner"},
            {"bit", "macro", "stateModel", "runtimeOwner", "status"},
        )
        if not isinstance(config["bit"], int) or config["bit"] < 0 or config["bit"] > 31:
            fail(boards_root / "capabilities.json", f"$.capabilities.{capability_id}.bit", config["bit"], "an integer from 0 through 31")
        if config["bit"] in bits:
            fail(boards_root / "capabilities.json", f"$.capabilities.{capability_id}.bit", config["bit"], "a unique capability bit")
        bits.add(config["bit"])
        if not isinstance(config["macro"], str) or not MACRO_PATTERN.fullmatch(config["macro"]) or config["macro"] in macros:
            fail(boards_root / "capabilities.json", f"$.capabilities.{capability_id}.macro", config["macro"], "a unique C macro")
        macros.add(config["macro"])
        if config["stateModel"] not in {"static", "lifecycle"}:
            fail(boards_root / "capabilities.json", f"$.capabilities.{capability_id}.stateModel", config["stateModel"], "static or lifecycle")
    targets: dict[str, dict[str, Any]] = {}
    target_pins: dict[str, set[Any]] = {}
    for path in sorted((boards_root / "targets").glob("*.json")):
        descriptor = load_json(path)
        validate_target(path, descriptor)
        if descriptor["id"] in targets:
            fail(path, "$.id", descriptor["id"], "a globally unique target ID")
        targets[descriptor["id"]] = descriptor
        target_pins[descriptor["id"]] = expand_pin_set(
            path, "$.gpio.validPins", descriptor["gpio"]["validPins"]
        )
    boards: dict[str, dict[str, Any]] = {}
    profile_ids: dict[int, Path] = {}
    selectors: dict[str, Path] = {}
    runtime_names: dict[str, Path] = {}
    for path in sorted((boards_root / "profiles").glob("*.json")):
        descriptor = load_json(path)
        validate_board(path, descriptor, targets, target_pins, capabilities)
        board_id = descriptor["id"]
        if board_id in boards:
            fail(path, "$.id", board_id, "a globally unique board ID")
        hal = descriptor["hal"]
        for value, seen, json_path in (
            (hal["profileId"], profile_ids, "$.hal.profileId"),
            (hal["selector"], selectors, "$.hal.selector"),
            (hal["runtimeName"], runtime_names, "$.hal.runtimeName"),
        ):
            if value in seen:
                fail(path, json_path, value, f"a unique value; first used by {seen[value]}")
            seen[value] = path
        boards[board_id] = descriptor
    for target_id, target in targets.items():
        default_board = target["defaultBoard"]
        if default_board not in boards or target_id not in boards[default_board]["compatibleTargets"]:
            fail(
                boards_root / "targets" / f"{target_id}.json",
                "$.defaultBoard",
                default_board,
                "a compatible board ID",
            )
    expected_profile_ids = {
        "pico": 1,
        "picow": 2,
        "pico2": 3,
        "pico2w": 4,
        "pico-rm2": 5,
        "nucleo-g474re": 6,
        "host-mock": 7,
        "rp2040-zero": 8,
        "rp2040-plus-4mb": 9,
        "rp2040-lora-lf": 10,
        "nucleo-g474re-pim730": 11,
    }
    for board_id, profile_id in expected_profile_ids.items():
        if board_id not in boards or boards[board_id]["hal"]["profileId"] != profile_id:
            fail(boards_root, f"$.profiles.{board_id}.profileId", boards.get(board_id), str(profile_id))
    pico_reservations = set(boards["pico"]["gpio"]["reservations"])
    rm2_reservations = set(boards["pico-rm2"]["gpio"]["reservations"])
    if not pico_reservations <= rm2_reservations:
        fail(boards_root / "profiles/pico-rm2.json", "$.gpio.reservations", sorted(rm2_reservations), "a superset of pico reservations")
    if not set(boards["pico"]["devices"]) <= set(boards["pico-rm2"]["devices"]):
        fail(boards_root / "profiles/pico-rm2.json", "$.devices", boards["pico-rm2"]["devices"], "a superset of pico devices")
    return targets, boards, capabilities


def normalize_features(features: list[str]) -> list[str]:
    normalized: set[str] = set()
    for feature in features:
        value = feature.strip()
        match = FEATURE_PATTERN.fullmatch(value)
        if match is None:
            raise DescriptorError(
                f"feature {feature!r}: expected a HAL_ENABLE_* or "
                "HAL_DISABLE_* symbol, optionally followed by =1"
            )
        explicit_value = match.group(2)
        if explicit_value is not None and explicit_value != "1":
            raise DescriptorError(
                f"[JH-CFG-VALUE] feature {feature!r}: HAL feature symbols "
                "accept only a bare macro or an explicit value of 1"
            )
        normalized.add(f"{match.group(1)}=1")
    ordered = sorted(normalized)
    return ordered


def validate_definitions(definitions: list[str]) -> None:
    for definition in definitions:
        value = definition.strip()
        if "$<" in value:
            raise DescriptorError(
                f"[JH-CFG-VALUE] compile definition {definition!r}: "
                "generator expressions are not supported"
            )
        normalized = value.removeprefix("-D")
        if not normalized.startswith(("HAL_ENABLE_", "HAL_DISABLE_")):
            if "HAL_ENABLE_" in normalized or "HAL_DISABLE_" in normalized:
                raise DescriptorError(
                    f"[JH-CFG-VALUE] compile definition {definition!r}: "
                    "HAL feature symbols must be standalone bare macros or "
                    "have an explicit value of 1"
                )
            continue
        match = FEATURE_PATTERN.fullmatch(value)
        if match is None or (match.group(2) is not None and match.group(2) != "1"):
            raise DescriptorError(
                f"[JH-CFG-VALUE] compile definition {definition!r}: "
                "HAL feature symbols accept only a bare macro or an explicit "
                "value of 1"
            )


def resolve_features(features: list[str]) -> tuple[list[str], list[str]]:
    normalized = normalize_features(features)
    requests = [
        generate_hal_features.FeatureRequest(
            symbol=definition.removesuffix("=1"),
            value="1",
            source=f"command-line:--requested-feature[{index}]",
        )
        for index, definition in enumerate(normalized)
    ]
    try:
        model = generate_hal_features.load_registry(
            Path(__file__).resolve().parents[1] / "config"
        )
        resolution, findings = generate_hal_features.resolve_feature_requests(
            requests, model, "board configuration"
        )
    except generate_hal_features.RegistryError as error:
        raise DescriptorError(str(error)) from error
    if findings:
        raise DescriptorError("\n".join(findings))
    return list(resolution.requested), list(resolution.resolved)


def macro_suffix(identifier: str) -> str:
    return identifier.upper().replace("-", "_")


def camel_macro_suffix(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).upper()


def encode_hal_pin(endpoint: dict[str, Any]) -> int | None:
    """Encode one endpoint into the integer pin ID consumed by HAL modules."""
    if endpoint["domain"] == "soc-gpio":
        pin = endpoint["id"]
        if isinstance(pin, str):
            match = STM32_PIN_PATTERN.fullmatch(pin)
            assert match is not None
            return (ord(match.group(1)) - ord("A")) * 16 + int(match.group(2))
        return pin
    return endpoint.get("halPin")


def device_config_lines(board: dict[str, Any]) -> list[str]:
    """Materialize bus-device facts for every known role."""
    devices = {
        device["role"]: device
        for device in board["devices"].values()
        if device["kind"] == BUS_DEVICE_KIND
    }
    lines = ["#define HAL_BOARD_DEVICE_PIN_NONE 0xFFu"]
    for role, spec in sorted(DEVICE_ROLE_REGISTRY.items()):
        prefix = spec["macroPrefix"]
        device = devices.get(role)
        lines.append(f"#define {prefix}_PRESENT {1 if device else 0}")
        if device is None:
            continue
        lines.append(
            f"#define {prefix}_{macro_suffix(device['bus']['kind'])}_BUS "
            f"{device['bus']['index']}u"
        )
        for name in sorted(spec["signals"]):
            pin = device["signals"].get(name)
            value = "HAL_BOARD_DEVICE_PIN_NONE"
            if pin is not None:
                encoded = encode_hal_pin(pin)
                value = f"{encoded}u"
            lines.append(f"#define {prefix}_PIN_{camel_macro_suffix(name)} {value}")
        for name in sorted(spec["attributes"]):
            attribute = spec["attributes"][name]
            suffix = f"{prefix}_{camel_macro_suffix(name)}"
            if attribute["type"] == "enum":
                value = device["attributes"].get(name)
                for allowed in attribute["values"]:
                    lines.append(
                        f"#define {suffix}_IS_{macro_suffix(allowed)} "
                        f"{1 if value == allowed else 0}"
                    )
                if value is not None:
                    lines.append(f'#define {suffix}_NAME "{value}"')
                continue
            if name not in device["attributes"]:
                continue
            value = device["attributes"][name]
            if attribute["type"] == "bool":
                lines.append(f"#define {suffix} {1 if value else 0}")
            elif attribute["type"] == "uint32":
                lines.append(f"#define {suffix} UINT32_C({value})")
            else:
                lines.append(f"#define {suffix} ({value})")
    return lines


def board_enum_name(board: dict[str, Any]) -> str:
    return board["hal"]["selector"].replace("HAL_BOARD_PROFILE_", "HAL_BOARD_", 1)


def c_symbol_part(identifier: str) -> str:
    return identifier.replace("-", "_")


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent, text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
        os.replace(temporary_name, path)
    finally:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass


def generate(
    target: dict[str, Any],
    board: dict[str, Any],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
    output_dir: Path,
    requested_feature_inputs: list[str],
) -> None:
    requested_features, resolved_features = resolve_features(
        requested_feature_inputs
    )
    resolved_features_digest = generate_hal_features.resolved_features_digest(
        resolved_features
    )
    feature_contract = [
        f"hal.profileId={board['hal']['profileId']}",
        *(f"{feature}=1" for feature in resolved_features),
    ]
    feature_hash = hashlib.sha256(
        "\n".join(feature_contract).encode()
    ).hexdigest()[:12]
    contract_symbol = (
        f"jh_board_contract_{c_symbol_part(target['id'])}_"
        f"{c_symbol_part(board['id'])}_{feature_hash}"
    )
    components = sorted(set(target["components"]) | set(board["components"]))
    board_compile_definitions: list[str] = []
    if "cyw43-pico-pio" in components:
        board_compile_definitions.extend(
            [
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_PICO_PIO",
                "HAL_CYW43_STACK_LWIP",
                "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
            ]
        )
    if "cyw43-stm32-gspi" in components:
        board_compile_definitions.extend(
            [
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_STM32_GSPI",
                "HAL_CYW43_STACK_LWIP",
                "HAL_CYW43_PIN_WL_ON=30u",
                "HAL_CYW43_PIN_CHIP_SELECT=28u",
                "HAL_CYW43_PIN_DATA=31u",
                "HAL_CYW43_PIN_CLOCK=29u",
                "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
            ]
        )
    resolved = {
        "schemaVersion": 1,
        "target": target["id"],
        "board": board["id"],
        "provider": board["build"]["provider"],
        "providerBoard": board["build"].get("board"),
        "platform": target["build"].get("platform"),
        "recipe": target["build"]["recipe"],
        "profileId": board["hal"]["profileId"],
        "selector": board["hal"]["selector"],
        "runtimeName": board["hal"]["runtimeName"],
        "flashBytes": board["memory"]["flash"]["expectedBytes"],
        "components": components,
        "boardCompileDefinitions": board_compile_definitions,
        "requestedFeatures": requested_features,
        "resolvedFeatures": resolved_features,
        "resolvedFeaturesDigest": resolved_features_digest,
        "features": resolved_features,
        "featureHash": feature_hash,
        "contractSymbol": contract_symbol,
        "capabilities": board["capabilities"],
        "gpio": board["gpio"],
        "devices": board["devices"],
        "peripherals": board["peripherals"],
    }
    cmake_lines = [
        "# Generated by generate_board_config.py; do not edit.",
        f'set(JH_RESOLVED_TARGET "{target["id"]}")',
        f'set(JH_RESOLVED_BOARD "{board["id"]}")',
        f'set(JH_BOARD_PROVIDER "{board["build"]["provider"]}")',
        f'set(JH_BOARD_RECIPE "{target["build"]["recipe"]}")',
        f'set(JH_BOARD_COMPONENTS "{";".join(components)}")',
        f'set(JH_BOARD_COMPILE_DEFINITIONS "{";".join(board_compile_definitions)}")',
        f'set(JH_BOARD_EXPECTED_FLASH_BYTES "{board["memory"]["flash"]["expectedBytes"]}")',
        f'set(JH_BOARD_REQUESTED_FEATURES "{";".join(requested_features)}")',
        f'set(JH_BOARD_RESOLVED_FEATURES "{";".join(resolved_features)}")',
        f'set(JH_BOARD_RESOLVED_FEATURES_DIGEST "{resolved_features_digest}")',
        f'set(JH_BOARD_FEATURES "{";".join(resolved_features)}")',
        f'set(JH_BOARD_FEATURE_HASH "{feature_hash}")',
        f'set(JH_BOARD_CONTRACT_SYMBOL "{contract_symbol}")',
    ]
    if "platform" in target["build"]:
        cmake_lines.append(f'set(PICO_PLATFORM "{target["build"]["platform"]}")')
    if "board" in board["build"]:
        cmake_lines.append(f'set(PICO_BOARD "{board["build"]["board"]}")')
    registry_lines = [
        "#pragma once",
        "/* Generated by generate_board_config.py; do not edit. */",
        "#include <stdint.h>",
        "",
        "typedef enum {",
    ]
    for entry in sorted(boards.values(), key=lambda item: item["hal"]["profileId"]):
        registry_lines.append(
            f"  {board_enum_name(entry)} = {entry['hal']['profileId']},"
        )
    registry_lines.extend(
        [
            "  HAL_BOARD_STM32G474_GENERIC = "
            "HAL_BOARD_STM32G474_NUCLEO_G474RE",
            "} hal_board_profile_t;",
            "",
        ]
    )
    for capability_id, config in sorted(
        capabilities.items(), key=lambda item: item[1]["bit"]
    ):
        registry_lines.append(
            f"#define {config['macro']} (UINT32_C(1) << {config['bit']})"
        )
    all_active = [
        config["macro"]
        for config in capabilities.values()
        if config.get("status") != "reserved"
    ]
    registry_lines.append(f"#define HAL_BOARD_CAP_ALL ({' | '.join(all_active)})")
    capability_mask = 0
    config_lines = [
        "#pragma once",
        "/* Generated by generate_board_config.py; do not edit. */",
        f"#define {board['hal']['selector']} 1",
        f"#define HAL_BOARD_PROFILE_ID {board_enum_name(board)}",
        f'#define HAL_BOARD_PROFILE_NAME "{board["hal"]["runtimeName"]}"',
        f'#define HAL_BOARD_PROFILE_TARGET "{target["id"]}"',
        f'#define HAL_BOARD_PROVIDER_BOARD "{board["build"].get("board", "")}"',
        f"#define HAL_BOARD_EXPECTED_FLASH_BYTES UINT32_C({board['memory']['flash']['expectedBytes']})",
    ]
    if board_compile_definitions:
        config_lines.append(
            "/* Board/provider definitions required by direct compiler consumers. */"
        )
    for definition in board_compile_definitions:
        name, separator, value = definition.partition("=")
        config_lines.append(f"#define {name} {value if separator else '1'}")
    for entry in sorted(boards.values(), key=lambda item: item["hal"]["profileId"]):
        selector = entry["hal"]["selector"]
        is_macro = selector.replace("HAL_BOARD_PROFILE_", "HAL_BOARD_IS_", 1)
        config_lines.append(
            f"#define {is_macro} {1 if entry['id'] == board['id'] else 0}"
        )
    for legacy_selector in board["hal"]["legacySelectors"]:
        config_lines.append(f"#define {legacy_selector} 1")
    for capability_id, config in sorted(capabilities.items()):
        present = board["capabilities"].get(capability_id, {}).get("present", False)
        config_lines.append(
            f"#define HAL_BOARD_HAS_{macro_suffix(capability_id)} {1 if present else 0}"
        )
        if present and config.get("status") != "reserved":
            capability_mask |= 1 << config["bit"]
    config_lines.append(
        f"#define HAL_BOARD_DECLARED_CAPABILITIES UINT32_C(0x{capability_mask:08x})"
    )
    status_led = board["devices"].get("statusLed")
    if status_led:
        config_lines.append(
            f"#define HAL_BOARD_STATUS_LED_KIND_{macro_suffix(status_led['kind'])} 1"
        )
        pin = encode_hal_pin(status_led["endpoint"])
        if pin is not None:
            config_lines.append(f"#define HAL_BOARD_STATUS_LED_PIN {pin}u")
        if status_led["kind"] in ("gpio", "component-gpio"):
            config_lines.append("#define HAL_LED_BUILTIN HAL_BOARD_STATUS_LED_PIN")
        if status_led["kind"] == "addressable":
            config_lines.extend(
                [
                    "#define HAL_BOARD_STATUS_LED_PROTOCOL_WS2812 1",
                    "#define HAL_BOARD_STATUS_LED_PIXEL_ORDER_PROJECT_DEFINED 1",
                ]
            )
    config_lines.extend(device_config_lines(board))
    link_header = "\n".join(
        [
            "#pragma once",
            "/* Generated by generate_board_config.py; do not edit. */",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            f"void {contract_symbol}(void);",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            f"#define JH_BOARD_CONTRACT_SYMBOL {contract_symbol}",
            "",
        ]
    )
    link_definition = "\n".join(
        [
            "/* Generated by generate_board_config.py; do not edit. */",
            '#include "jh_link_contract.h"',
            "void JH_BOARD_CONTRACT_SYMBOL(void) {}",
            "",
        ]
    )
    link_reference = "\n".join(
        [
            "/* Generated by generate_board_config.py; do not edit. */",
            '#include "jh_link_contract.h"',
            "#if defined(__GNUC__) || defined(__clang__)",
            "#define JH_CONSTRUCTOR_USED __attribute__((constructor, used))",
            "static void JH_CONSTRUCTOR_USED jh_require_board_contract(void)",
            "{",
            "    JH_BOARD_CONTRACT_SYMBOL();",
            "}",
            "#else",
            "typedef void (*jh_board_contract_fn_t)(void);",
            "static jh_board_contract_fn_t const jh_board_contract_reference =",
            "    &JH_BOARD_CONTRACT_SYMBOL;",
            "#endif",
            "",
        ]
    )
    atomic_write(output_dir / "jh_board_config.cmake", "\n".join(cmake_lines) + "\n")
    atomic_write(output_dir / "jh_board_registry.h", "\n".join(registry_lines) + "\n")
    atomic_write(output_dir / "jh_board_config.h", "\n".join(config_lines) + "\n")
    atomic_write(output_dir / "jh_link_contract.h", link_header)
    atomic_write(output_dir / "jh_link_contract_definition.c", link_definition)
    atomic_write(output_dir / "jh_link_contract_reference.c", link_reference)
    atomic_write(
        output_dir / "jh_board_resolved.json",
        json.dumps(resolved, indent=2, sort_keys=True) + "\n",
    )
    dependencies = [
        str(Path(__file__).resolve()),
        str((Path(__file__).resolve().parent / "generate_hal_features.py")),
        *(
            str(path.resolve())
            for path in sorted(
                (Path(__file__).resolve().parents[1] / "boards").rglob("*.json")
            )
        ),
        *(
            str(path.resolve())
            for path in sorted(
                (Path(__file__).resolve().parents[1] / "config").rglob("*.json")
            )
        ),
    ]
    atomic_write(output_dir / "generation.d", "\n".join(dependencies) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target")
    parser.add_argument("--board")
    parser.add_argument("--boards-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--output-root", type=Path)
    parser.add_argument(
        "--feature",
        "--requested-feature",
        dest="requested_feature",
        action="append",
        default=[],
    )
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--list", choices=("targets", "boards"))
    parser.add_argument("--default-board", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    boards_root = args.boards_root.resolve()
    try:
        targets, boards, capabilities = load_registry(boards_root)
        normalize_features(args.requested_feature)
        validate_definitions(args.define)
        if args.list:
            values = targets if args.list == "targets" else boards
            print("\n".join(sorted(values)))
            return 0
        if args.default_board:
            if not args.target or args.target not in targets:
                raise DescriptorError("--default-board requires a known --target")
            print(targets[args.target]["defaultBoard"])
            return 0
        if args.validate_only:
            if args.target or args.board or args.output_dir or args.output_root:
                raise DescriptorError(
                    "--validate-only does not accept target, board, output-dir, "
                    "or output-root"
                )
            print(f"validated {len(targets)} targets and {len(boards)} boards")
            return 0
        if not args.target or not args.output_dir:
            raise DescriptorError(
                "generation requires --target and --output-dir"
            )
        if args.target not in targets:
            raise DescriptorError(f"unknown target {args.target!r}")
        if not args.board:
            args.board = targets[args.target]["defaultBoard"]
        if args.board not in boards:
            raise DescriptorError(f"unknown board {args.board!r}")
        target = targets[args.target]
        board = boards[args.board]
        if args.target not in board["compatibleTargets"]:
            raise DescriptorError(
                f"board {args.board!r} is not compatible with target {args.target!r}"
            )
        accepted_selectors = {
            board["hal"]["selector"],
            *board["hal"]["legacySelectors"],
        }
        for definition in args.define:
            selector = definition.split("=", 1)[0]
            if (
                selector.startswith("HAL_BOARD_PROFILE_")
                or selector.startswith("HAL_CYW43_PROFILE_")
            ) and selector not in accepted_selectors:
                raise DescriptorError(
                    f"compile definition {selector!r} conflicts with "
                    f"resolved board {args.board!r}"
                )
        output_dir = args.output_dir.resolve()
        repository_root = boards_root.parent
        managed_root = (
            args.output_root.resolve()
            if args.output_root is not None
            else repository_root / ".build"
        )
        host_build_root = os.environ.get("JH_MANAGED_BUILD_ROOT")
        matches_host_build_root = False
        if host_build_root:
            normalized_managed = os.path.normcase(str(managed_root))
            normalized_host = os.path.normcase(str(Path(host_build_root).resolve()))
            try:
                matches_host_build_root = (
                    os.path.commonpath((normalized_managed, normalized_host))
                    == normalized_host
                )
            except ValueError:
                matches_host_build_root = False
        if ".build" not in managed_root.parts and not matches_host_build_root:
            raise DescriptorError(
                f"output root must be a .build directory, one of its "
                f"descendants, or the runtime-managed build root; got {managed_root}"
            )
        if output_dir != managed_root and managed_root not in output_dir.parents:
            raise DescriptorError(
                f"output directory must be below {managed_root}, got {output_dir}"
            )
        generate(
            target,
            board,
            boards,
            capabilities,
            output_dir,
            args.requested_feature,
        )
    except DescriptorError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
