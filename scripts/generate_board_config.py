#!/usr/bin/env python3
"""Generate resolved build configuration from the board registry that
`jh board list` validates."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from functools import partial
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from typing import Any

from codegen_support import (
    atomic_write_text,
    check_generated_outputs,
    require_exact_fields,
    validation_error,
    write_generated_outputs,
)
import generate_hal_features
import project_config
from repository_layout import (
    BOARD_COMPONENTS_CMAKE_OUTPUT,
    BOARD_FALLBACK_HEADER_OUTPUT,
    BOARD_REGISTRY_HEADER_OUTPUT,
)


class DescriptorError(ValueError):
    """Descriptor validation failure with actionable context."""


fail = partial(validation_error, DescriptorError)
exact_fields = partial(require_exact_fields, error_type=DescriptorError)
atomic_write = partial(atomic_write_text, error_type=DescriptorError)

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
STM32_PIN_PATTERN = re.compile(r"^P([A-Z])([0-9]|1[0-5])$")
FEATURE_PATTERN = re.compile(
    r"^(?:-D)?(HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:=(.*))?$"
)


@dataclass(frozen=True)
class BoardRegistry:
    """One board registry as `jh board list` validated it. The component,
    device-role and CAN tables come from jh, which owns them."""

    targets: dict[str, dict[str, Any]]
    boards: dict[str, dict[str, Any]]
    capabilities: dict[str, Any]
    components: dict[str, dict[str, Any]]
    device_roles: dict[str, dict[str, Any]]
    can_controllers: dict[str, dict[str, Any]]
    bus_device_kind: str


def _registry_state(boards_root: Path) -> tuple[Any, ...] | None:
    """Names, sizes and modification times of the registry inputs; None when
    they cannot be listed, and then no result is kept."""
    try:
        inputs = [
            REPOSITORY_ROOT / "config" / "tooling" / "board_components.json",
            boards_root / "capabilities.json",
            *sorted((boards_root / "targets").glob("*.json")),
            *sorted((boards_root / "profiles").glob("*.json")),
        ]
        return tuple(
            (str(path), status.st_size, status.st_mtime_ns)
            for path, status in ((path, path.stat()) for path in inputs)
        )
    except OSError:
        return None


_MODELS: dict[str, tuple[tuple[Any, ...], BoardRegistry]] = {}


def registry_model(boards_root: Path) -> BoardRegistry:
    """The registry below boards_root as `jh board list` validates it, kept
    for this process until its files change; DescriptorError with the jh
    message when jh rejects it."""
    root = Path(boards_root).absolute()
    key = str(root)
    state = _registry_state(root)
    cached = _MODELS.get(key)
    if cached is not None and state is not None and cached[0] == state:
        return cached[1]
    returncode, output = project_config.run_jh(
        ["board", "list", f"--root={REPOSITORY_ROOT}", f"--boards-root={root}"],
        DescriptorError,
    )
    if returncode != 0:
        raise DescriptorError(output.get("message", f"jh exit {returncode}"))
    model = BoardRegistry(
        targets={item["id"]: item["descriptor"] for item in output["targets"]},
        boards={item["id"]: item["descriptor"] for item in output["boards"]},
        capabilities=output["capabilities"],
        components={
            item["id"]: {"providers": set(item["providers"]), "slot": item["slot"]}
            for item in output["components"]
        },
        device_roles=output["deviceRoles"],
        can_controllers={
            name: {**item, "targets": set(item["targets"])}
            for name, item in output["canControllers"].items()
        },
        bus_device_kind=output["busDeviceKind"],
    )
    if state is not None:
        _MODELS[key] = (state, model)
    return model


def load_registry(
    boards_root: Path,
) -> tuple[
    dict[str, dict[str, Any]],
    dict[str, dict[str, Any]],
    dict[str, Any],
]:
    """Targets, boards and capabilities of the registry below boards_root."""
    model = registry_model(boards_root)
    return model.targets, model.boards, model.capabilities


def _tables() -> BoardRegistry:
    """The jh tables, read with the registry of this repository."""
    return registry_model(REPOSITORY_ROOT / "boards")


def __getattr__(name: str) -> Any:
    """COMPONENT_REGISTRY and CAN_CONTROLLERS for importers, from jh."""
    if name == "COMPONENT_REGISTRY":
        return _tables().components
    if name == "CAN_CONTROLLERS":
        return _tables().can_controllers
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

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


def board_device_owner(device_id: str) -> str:
    """Reservation owner of a board device: board.<device-id in kebab case>."""
    return "board." + re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "-", device_id).lower()


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


def resolve_features(
    features: list[str], target: dict[str, Any]
) -> tuple[list[str], list[str], dict[str, list[str]], str]:
    """Requested and resolved features, their provenance and the digest of the
    resolved set."""
    normalized = normalize_features(features)
    required = normalize_features(target.get("requiredFeatures", []))
    requested_symbols = {item.removesuffix("=1") for item in normalized}
    for definition in required:
        symbol = definition.removesuffix("=1")
        disabled = symbol.replace("HAL_ENABLE_", "HAL_DISABLE_", 1)
        if disabled in requested_symbols:
            raise DescriptorError(
                f"[JH-CFG-TARGET-REQUIRED] {target['id']} requires {symbol}; "
                f"{disabled} cannot be requested"
            )
    requests = [
        generate_hal_features.FeatureRequest(
            symbol=definition.removesuffix("=1"),
            value="1",
            source=f"command-line:--requested-feature[{index}]",
        )
        for index, definition in enumerate(normalized)
    ]
    requests.extend(
        generate_hal_features.FeatureRequest(
            symbol=definition.removesuffix("=1"),
            value="1",
            source=f"target:{target['id']}:requiredFeatures[{index}]",
        )
        for index, definition in enumerate(required)
        if definition.removesuffix("=1") not in requested_symbols
    )
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
    requested = [definition.removesuffix("=1") for definition in normalized]
    provenance = {
        symbol: list(sources) for symbol, sources in resolution.provenance.items()
    }
    for index, definition in enumerate(required):
        symbol = definition.removesuffix("=1")
        source = f"target:{target['id']}:requiredFeatures[{index}]"
        provenance[symbol] = sorted({*provenance.get(symbol, []), source})
    return requested, list(resolution.resolved), provenance, resolution.digest


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


def pin_text(endpoint: dict[str, Any]) -> str:
    """C spelling of a pin: the early hardware macro when an assembly names
    one, otherwise the HAL encoding."""
    return endpoint.get("macro") or f"{encode_hal_pin(endpoint)}u"


def encoded_gpio_pins(pins: set[Any]) -> set[int]:
    """HAL encoding of SoC GPIO IDs (STM32: port * 16 + pin)."""
    return {encode_hal_pin({"domain": "soc-gpio", "id": pin}) for pin in pins}


def endpoint_gpio_pins(endpoints: list[dict[str, Any]]) -> set[int]:
    return encoded_gpio_pins(
        {endpoint["id"] for endpoint in endpoints if endpoint["domain"] == "soc-gpio"}
    )


def gpio_word_count(targets: list[dict[str, Any]]) -> int:
    """64-bit words that hold every valid pin of the given targets."""
    highest = max(
        max(
            encoded_gpio_pins(
                expand_pin_set(
                    Path(f"boards/targets/{target['id']}.json"),
                    "$.gpio.validPins",
                    target["gpio"]["validPins"],
                )
            ),
            default=0,
        )
        for target in targets
    )
    return highest // 64 + 1


def append_gpio_mask(
    lines: list[str], name: str, pins: set[int], words: int
) -> None:
    """Write a GPIO set as 64-bit words, <name>_0 holding pins 0-63; a target
    whose pins fit one word also keeps the single <name> mask."""
    values = [0] * words
    for pin in pins:
        values[pin // 64] |= 1 << (pin % 64)
    if words == 1:
        lines.append(f"#define {name} UINT64_C(0x{values[0]:016x})")
    lines.append(f"#define {name}_WORDS {words}u")
    for index, value in enumerate(values):
        lines.append(f"#define {name}_{index} UINT64_C(0x{value:016x})")


def device_config_lines(board: dict[str, Any]) -> list[str]:
    """Materialize bus-device facts for every known role."""
    tables = _tables()
    devices = {
        device["role"]: device
        for device in board["devices"].values()
        if device["kind"] == tables.bus_device_kind
    }
    lines = ["#define HAL_BOARD_DEVICE_PIN_NONE 0xFFu"]
    for role, spec in sorted(tables.device_roles.items()):
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


def can_config_lines(board: dict[str, Any]) -> list[str]:
    """Materialize the board CAN channels as a count and an X-macro table.

    Each row is X(backend, instance, rx, tx, standby, standby_active_high,
    max_bitrate_hz); backend is the HAL_CAN_BACKEND_* suffix.
    """
    channels = board.get("can", {}).get("channels", [])
    rows = []
    for channel in channels:
        standby = channel.get("standby")
        standby_pin = (
            pin_text(standby) if standby is not None else "HAL_BOARD_DEVICE_PIN_NONE"
        )
        rows.append(
            f"X({_tables().can_controllers[channel['controller']]['backend']}, "
            f"{channel['instance']}u, {pin_text(channel['rx'])}, "
            f"{pin_text(channel['tx'])}, {standby_pin}, "
            f"{1 if channel.get('standbyActiveHigh') else 0}, "
            f"UINT32_C({channel['maxBitrateHz']}))"
        )
    return [
        # Plain int: HAL_CAN_MAX_INSTANCES derives from it and stays an int.
        f"#define HAL_BOARD_CAN_CHANNEL_COUNT {len(channels)}",
        "#define HAL_BOARD_CAN_CHANNELS(X)" + "".join(f" {row}" for row in rows),
    ]


def board_enum_name(board: dict[str, Any]) -> str:
    return board["hal"]["selector"].replace("HAL_BOARD_PROFILE_", "HAL_BOARD_", 1)


def board_is_macro(board: dict[str, Any]) -> str:
    return board["hal"]["selector"].replace(
        "HAL_BOARD_PROFILE_", "HAL_BOARD_IS_", 1
    )


def target_is_macro(target: dict[str, Any]) -> str:
    return target["hal"]["targetSelector"].replace(
        "HAL_TARGET_", "HAL_TARGET_IS_", 1
    )


def board_compile_definitions(
    target: dict[str, Any], board: dict[str, Any]
) -> list[str]:
    components = set(target["components"]) | set(board["components"])
    definitions: list[str] = []
    if "cyw43-pico-pio" in components:
        definitions.extend(
            [
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_PICO_PIO",
                "HAL_CYW43_STACK_LWIP",
                "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
            ]
        )
    if "stm32g474-hse-24mhz" in components:
        definitions.append("HAL_STM32G474_CLOCK_HSE_160MHZ")
    if "cyw43-stm32-gspi" in components:
        definitions.extend(
            [
                "HAL_NETWORK_BACKEND_CYW43",
                "HAL_CYW43_BUS_STM32_GSPI",
                "HAL_CYW43_STACK_LWIP",
                "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u",
            ]
        )
    return definitions


def render_board_registry(
    boards: dict[str, dict[str, Any]], capabilities: dict[str, Any]
) -> str:
    lines = [
        "#pragma once",
        "/* Generated by generate_board_config.py; do not edit. */",
        "#include <stdint.h>",
        "",
        "typedef enum {",
    ]
    ordered_boards = sorted(
        boards.values(), key=lambda item: item["hal"]["profileId"]
    )
    for board in ordered_boards:
        lines.append(f"  {board_enum_name(board)} = {board['hal']['profileId']},")
    enum_aliases: dict[str, str] = {}
    for board in ordered_boards:
        for selector in board["hal"]["legacySelectors"]:
            if not selector.startswith("HAL_BOARD_PROFILE_"):
                continue
            alias = selector.replace("HAL_BOARD_PROFILE_", "HAL_BOARD_", 1)
            enum_aliases[alias] = board_enum_name(board)
    for alias, destination in sorted(enum_aliases.items()):
        lines.append(f"  {alias} = {destination},")
    lines.extend(["} hal_board_profile_t;", ""])
    ordered_capabilities = sorted(
        capabilities.values(), key=lambda config: config["bit"]
    )
    for config in ordered_capabilities:
        lines.append(
            f"#define {config['macro']} (UINT32_C(1) << {config['bit']})"
        )
    all_active = [
        config["macro"]
        for config in ordered_capabilities
        if config.get("status") != "reserved"
    ]
    lines.append(f"#define HAL_BOARD_CAP_ALL ({' | '.join(all_active)})")
    return "\n".join(lines) + "\n"


def selected_board_fact_lines(
    board: dict[str, Any],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
    compile_definitions: list[str],
    target_value: str,
    gpio_words: int,
    *,
    define_selector: bool,
    define_is_macros: bool,
) -> list[str]:
    lines: list[str] = []
    flash_bytes = board["memory"]["flash"]["expectedBytes"]
    if define_selector:
        lines.append(f"#define {board['hal']['selector']} 1")
    lines.extend(
        [
            f"#define HAL_BOARD_PROFILE_ID {board_enum_name(board)}",
            f'#define HAL_BOARD_PROFILE_NAME "{board["id"]}"',
            f"#define HAL_BOARD_PROFILE_TARGET {target_value}",
            f'#define HAL_BOARD_PROVIDER_BOARD "{board["build"].get("board", "")}"',
            f"#define HAL_BOARD_EXPECTED_FLASH_BYTES "
            f"UINT32_C({flash_bytes})",
        ]
    )
    if flash_bytes > 0 and flash_bytes % (1024 * 1024) == 0:
        lines.append(
            f"#define HAL_BOARD_EXPECTED_FLASH_MIB {flash_bytes // (1024 * 1024)}"
        )
    psram = board["memory"].get("psram")
    lines.extend(
        [
            f"#define HAL_BOARD_HAS_PSRAM {1 if psram else 0}",
            "#define HAL_BOARD_PSRAM_BYTES "
            f"UINT32_C({psram['sizeBytes'] if psram else 0})",
        ]
    )
    if psram:
        lines.append(
            f"#define HAL_BOARD_PSRAM_INTERFACE_{psram['interface'].upper()} 1"
        )
    if compile_definitions:
        lines.append(
            "/* Board/provider definitions required by direct compiler consumers. */"
        )
    for definition in compile_definitions:
        name, separator, value = definition.partition("=")
        lines.append(f"#define {name} {value if separator else '1'}")
    if define_is_macros:
        for entry in sorted(
            boards.values(), key=lambda item: item["hal"]["profileId"]
        ):
            lines.append(
                f"#define {board_is_macro(entry)} "
                f"{1 if entry['id'] == board['id'] else 0}"
            )
    for legacy_selector in board["hal"]["legacySelectors"]:
        lines.extend(
            [
                f"#ifndef {legacy_selector}",
                f"#define {legacy_selector} 1",
                "#endif",
            ]
        )
    capability_mask = 0
    for capability_id, config in sorted(capabilities.items()):
        entry = board["capabilities"].get(capability_id, {})
        present = entry.get("present", False)
        lines.append(
            f"#define HAL_BOARD_HAS_{macro_suffix(capability_id)} "
            f"{entry.get('macro') or (1 if present else 0)}"
        )
        if present and config.get("status") != "reserved":
            capability_mask |= 1 << config["bit"]
    lines.append(
        f"#define HAL_BOARD_DECLARED_CAPABILITIES "
        f"UINT32_C(0x{capability_mask:08x})"
    )
    exposed = expand_pin_set(
        Path(f"boards/profiles/{board['id']}.json"),
        "$.gpio.exposedPins",
        board["gpio"]["exposedPins"],
    )
    hard_reserved: list[dict[str, Any]] = []
    soft_reserved: list[dict[str, Any]] = []
    for reservation in board["gpio"]["reservations"].values():
        destination = (
            hard_reserved
            if reservation["strength"] == "hard"
            else soft_reserved
        )
        destination.extend(reservation["pins"])
    append_gpio_mask(
        lines, "HAL_BOARD_GPIO_EXPOSED_MASK", encoded_gpio_pins(exposed), gpio_words
    )
    append_gpio_mask(
        lines,
        "HAL_BOARD_GPIO_HARD_RESERVED_MASK",
        endpoint_gpio_pins(hard_reserved),
        gpio_words,
    )
    append_gpio_mask(
        lines,
        "HAL_BOARD_GPIO_SOFT_RESERVED_MASK",
        endpoint_gpio_pins(soft_reserved),
        gpio_words,
    )
    status_led = board["devices"].get("statusLed")
    if status_led:
        lines.append(
            f"#define HAL_BOARD_STATUS_LED_KIND_"
            f"{macro_suffix(status_led['kind'])} 1"
        )
        if encode_hal_pin(status_led["endpoint"]) is not None:
            lines.append(
                f"#define HAL_BOARD_STATUS_LED_PIN {pin_text(status_led['endpoint'])}"
            )
        if status_led["kind"] in ("gpio", "component-gpio"):
            lines.append("#define HAL_LED_BUILTIN HAL_BOARD_STATUS_LED_PIN")
        if status_led["kind"] == "addressable":
            # A board leaves the order to the project; an assembly chooses it.
            order = status_led.get("pixelOrder")
            order = order if isinstance(order, str) else "PROJECT_DEFINED"
            lines.extend(
                [
                    "#define HAL_BOARD_STATUS_LED_PROTOCOL_WS2812 1",
                    f"#define HAL_BOARD_STATUS_LED_PIXEL_ORDER_{order} 1",
                ]
            )
    lines.extend(device_config_lines(board))
    lines.extend(can_config_lines(board))
    return lines


def target_ram_total_bytes(target: dict[str, Any]) -> int:
    return sum(
        region["sizeBytes"]
        for region in target["memory"]["regions"].values()
        if region["kind"] == "ram"
    )


def selected_target_fact_lines(target: dict[str, Any]) -> list[str]:
    architecture = target["architecture"]
    lines = [
        f'#define HAL_TARGET_DESCRIPTOR_ID "{target["id"]}"',
        f'#define HAL_TARGET_VENDOR_NAME "{architecture["vendor"]}"',
        f'#define HAL_TARGET_FAMILY_NAME "{architecture["family"]}"',
        f'#define HAL_TARGET_SOC_NAME "{architecture["soc"]}"',
        f'#define HAL_TARGET_ISA_NAME "{architecture["isa"]}"',
        f'#define HAL_TARGET_BACKEND_NAME "{architecture["backendName"]}"',
        f'#define HAL_TARGET_MCU_NAME "{architecture["mcuName"]}"',
        f'#define HAL_TARGET_MCU_SUBTYPE_NAME "{architecture["mcuSubtype"]}"',
        f'#define HAL_TARGET_CPU_ARCH_NAME "{architecture["cpuArchName"]}"',
        f'#define HAL_TARGET_CPU_CORES {architecture["cores"]}u',
        f'#define HAL_TARGET_HAS_FPU {1 if architecture["hasFpu"] else 0}',
        "#define HAL_TARGET_RAM_TOTAL_BYTES "
        f'UINT32_C({target_ram_total_bytes(target)})',
        "#define HAL_TARGET_RAM_USABLE_BYTES "
        f'UINT32_C({target["memory"]["ramUsableBytes"]})',
    ]
    if "usbDevice" in target:
        usb = target["usbDevice"]
        lines.extend([
            "/** @brief Default USB device vendor ID of the selected target. */",
            f"#define HAL_TARGET_USB_VID UINT16_C({usb['vid']})",
            "/** @brief Default USB device product ID of the selected target. */",
            f"#define HAL_TARGET_USB_PID UINT16_C({usb['pid']})",
        ])
    descriptor_path = Path(f"boards/targets/{target['id']}.json")
    valid_pins = expand_pin_set(
        descriptor_path, "$.gpio.validPins", target["gpio"]["validPins"]
    )
    words = gpio_word_count([target])
    append_gpio_mask(
        lines, "HAL_TARGET_GPIO_VALID_MASK", encoded_gpio_pins(valid_pins), words
    )
    for trait_id, macro_name in (
        ("input-only", "HAL_TARGET_GPIO_INPUT_ONLY_MASK"),
        ("adc", "HAL_TARGET_GPIO_ADC_MASK"),
    ):
        trait = target["gpio"].get("traits", {}).get(trait_id)
        if trait is None:
            continue
        pins = expand_pin_set(
            descriptor_path, f"$.gpio.traits.{trait_id}", trait
        )
        append_gpio_mask(lines, macro_name, encoded_gpio_pins(pins), words)
    return lines


def fallback_compile_definitions(
    board: dict[str, Any], targets: dict[str, dict[str, Any]]
) -> list[str]:
    variants = {
        tuple(board_compile_definitions(targets[target_id], board))
        for target_id in board["compatibleTargets"]
    }
    if len(variants) != 1:
        raise DescriptorError(
            f"board {board['id']!r} requires target-specific fallback definitions"
        )
    return list(next(iter(variants)))


def all_selectors_absent(selectors: list[str]) -> str:
    return " && ".join(f"!defined({selector})" for selector in selectors)


def render_board_fallback_config(
    targets: dict[str, dict[str, Any]],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
) -> str:
    ordered_boards = sorted(
        boards.values(), key=lambda item: item["hal"]["profileId"]
    )
    primary_selectors = [board["hal"]["selector"] for board in ordered_boards]
    no_primary = all_selectors_absent(primary_selectors)
    lines = [
        "#pragma once",
        "/* Generated by generate_board_config.py; do not edit. */",
        "",
        "#if defined(HAL_CYW43_PROFILE_PICO_W)",
        '#error "HAL_CYW43_PROFILE_PICO_W is invalid; use HAL_CYW43_PROFILE_PICOW."',
        "#endif",
        "",
        "/* Resolve legacy selectors through descriptor target compatibility. */",
    ]
    legacy_boards: dict[str, list[dict[str, Any]]] = {}
    for board in ordered_boards:
        for selector in board["hal"]["legacySelectors"]:
            legacy_boards.setdefault(selector, []).append(board)
    for legacy_selector, matches in sorted(legacy_boards.items()):
        lines.append(f"#if defined({legacy_selector})")
        if len(matches) == 1:
            destination = matches[0]["hal"]["selector"]
            lines.extend(
                [
                    f"#ifndef {destination}",
                    f"#define {destination} 1",
                    "#endif",
                ]
            )
        else:
            branches: list[tuple[str, str]] = []
            for board in matches:
                for target_id in board["compatibleTargets"]:
                    branches.append(
                        (target_is_macro(targets[target_id]), board["hal"]["selector"])
                    )
            for index, (target_macro, destination) in enumerate(branches):
                lines.append(f"#{'if' if index == 0 else 'elif'} {target_macro}")
                lines.append(f"#define {destination} 1")
            lines.extend(
                [
                    "#else",
                    f'#error "JaszczurHAL: {legacy_selector} has no board for the selected target."',
                    "#endif",
                ]
            )
        lines.extend(["#endif", ""])

    auto_detect: list[tuple[str, dict[str, Any]]] = []
    for board in ordered_boards:
        auto_detect.extend(
            (selector, board) for selector in board["hal"]["autoDetectSelectors"]
        )
    if auto_detect:
        detection_count = " + ".join(
            f"defined({selector})" for selector, _board in auto_detect
        )
        lines.extend(
            [
                "/* Prefer an unambiguous provider board selector. */",
                f"#if {no_primary} && ({detection_count}) > 1",
                '#error "JaszczurHAL: multiple provider board selectors are active."',
                "#endif",
                f"#if {no_primary}",
            ]
        )
        for index, (selector, board) in enumerate(auto_detect):
            lines.append(
                f"#{'if' if index == 0 else 'elif'} defined({selector})"
            )
            lines.append(f"#define {board['hal']['selector']} 1")
        lines.extend(["#endif", "#endif", ""])

    source_fallback_targets = [
        target
        for target in sorted(targets.values(), key=lambda item: item["id"])
        if "sourceFallbackBoard" in target
    ]
    lines.extend(
        [
            "/* Apply only explicitly declared source-level fallbacks. */",
            f"#if {no_primary}",
        ]
    )
    for index, target in enumerate(source_fallback_targets):
        fallback_board = boards[target["sourceFallbackBoard"]]
        lines.append(
            f"#{'if' if index == 0 else 'elif'} {target_is_macro(target)}"
        )
        lines.append(f"#define {fallback_board['hal']['selector']} 1")
    if source_fallback_targets:
        lines.append("#else")
    lines.append(
        '#error "JaszczurHAL: unknown board. Configure through the board generator (JH_BOARD), define an explicit HAL_BOARD_PROFILE_*, or provide an unambiguous provider board selector."'
    )
    if source_fallback_targets:
        lines.append("#endif")
    lines.extend(["#endif", ""])

    lines.append("/* Normalize every descriptor selector to a fixed 0/1 macro. */")
    for board in ordered_boards:
        selector = board["hal"]["selector"]
        is_macro = board_is_macro(board)
        lines.extend(
            [
                f"#if defined({selector})",
                f"#define {is_macro} 1",
                "#else",
                f"#define {is_macro} 0",
                "#endif",
                "",
            ]
        )
    selection_count = " + ".join(board_is_macro(board) for board in ordered_boards)
    lines.extend(
        [
            f"#if ({selection_count}) != 1",
            '#error "JaszczurHAL: exactly one HAL_BOARD_PROFILE_* must be selected."',
            "#endif",
            "",
            "/* Validate descriptor target compatibility. */",
        ]
    )
    for board in ordered_boards:
        compatible = " || ".join(
            target_is_macro(targets[target_id])
            for target_id in board["compatibleTargets"]
        )
        lines.extend(
            [
                f"#if {board_is_macro(board)} && !({compatible})",
                f'#error "JaszczurHAL: board {board["id"]} is incompatible with the selected target."',
                "#endif",
            ]
        )
    lines.extend(["", "/* Materialize the selected target facts. */"])
    for index, target in enumerate(
        sorted(targets.values(), key=lambda item: item["id"])
    ):
        lines.append(
            f"#{'if' if index == 0 else 'elif'} {target_is_macro(target)}"
        )
        lines.extend(selected_target_fact_lines(target))
    lines.extend(["#endif", "", "/* Materialize the selected board facts. */"])
    for index, board in enumerate(ordered_boards):
        lines.append(
            f"#{'if' if index == 0 else 'elif'} {board_is_macro(board)}"
        )
        lines.extend(
            selected_board_fact_lines(
                board,
                boards,
                capabilities,
                fallback_compile_definitions(board, targets),
                "HAL_TARGET_NAME",
                gpio_word_count(
                    [targets[target_id] for target_id in board["compatibleTargets"]]
                ),
                define_selector=False,
                define_is_macros=False,
            )
        )
    lines.extend(["#endif", ""])
    return "\n".join(lines)


def static_generated_outputs(
    targets: dict[str, dict[str, Any]],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
) -> dict[Path, str]:
    return {
        BOARD_REGISTRY_HEADER_OUTPUT: render_board_registry(boards, capabilities),
        BOARD_FALLBACK_HEADER_OUTPUT: render_board_fallback_config(
            targets, boards, capabilities
        ),
        BOARD_COMPONENTS_CMAKE_OUTPUT: render_board_components_cmake(),
    }


def render_board_components_cmake() -> str:
    """Render the CMake view of the authoritative board-component registry."""
    components = _tables().components
    lines = [
        "# Generated by generate_board_config.py; do not edit.",
        "set(JH_BOARD_COMPONENT_IDS",
        *(f"    {identifier}" for identifier in components),
        ")",
        "",
    ]
    for identifier, entry in components.items():
        key = identifier.replace("-", "_")
        providers = ";".join(sorted(entry["providers"]))
        lines.extend(
            [
                f'set(JH_BOARD_COMPONENT_{key}_PROVIDERS "{providers}")',
                f'set(JH_BOARD_COMPONENT_{key}_SLOT "{entry["slot"]}")',
                "",
            ]
        )
    return "\n".join(lines)


def c_symbol_part(identifier: str) -> str:
    return identifier.replace("-", "_")


def write_static_outputs(repository_root: Path, outputs: dict[Path, str]) -> None:
    write_generated_outputs(
        repository_root,
        outputs,
        artifact_kind="board",
        error_type=DescriptorError,
    )


def check_static_outputs(repository_root: Path, outputs: dict[Path, str]) -> bool:
    return check_generated_outputs(
        repository_root,
        outputs,
        artifact_kind="board",
        error_type=DescriptorError,
        diff_limit=120,
    )


def generate(
    target: dict[str, Any],
    board: dict[str, Any],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
    output_dir: Path,
    requested_feature_inputs: list[str],
    boards_root: Path | None = None,
) -> None:
    outputs = build_outputs(
        target, board, boards, capabilities, requested_feature_inputs, boards_root
    )
    try:
        (output_dir / "jh_board_registry.h").unlink()
    except FileNotFoundError:
        pass
    for name, content in outputs.items():
        atomic_write(output_dir / name, content)


def build_outputs(
    target: dict[str, Any],
    board: dict[str, Any],
    boards: dict[str, dict[str, Any]],
    capabilities: dict[str, Any],
    requested_feature_inputs: list[str],
    boards_root: Path | None = None,
    *,
    header_definitions: list[str] | None = None,
    cmake_definitions: list[str] | None = None,
    components: list[str] | None = None,
    contract_hash: str | None = None,
    header_prelude: list[str] = (),
    generation_extra: list[Path] = (),
) -> dict[str, str]:
    """Text of every per-build output. An assembly passes the board it
    resolved and the values it owns: the definitions its header carries,
    those still passed to the compiler, its components and its digest for
    the link signature. Without them the outputs are the board's."""
    (
        requested_features,
        resolved_features,
        feature_provenance,
        resolved_features_digest,
    ) = resolve_features(requested_feature_inputs, target)
    feature_contract = [
        f"hal.profileId={board['hal']['profileId']}",
        *(f"{feature}=1" for feature in resolved_features),
    ]
    feature_hash = hashlib.sha256(
        "\n".join(feature_contract).encode()
    ).hexdigest()[:12]
    contract_symbol = (
        f"jh_board_contract_{c_symbol_part(target['id'])}_"
        f"{c_symbol_part(board['id'])}_{(contract_hash or feature_hash)[:12]}"
    )
    if components is None:
        components = sorted(set(target["components"]) | set(board["components"]))
    compile_definitions = board_compile_definitions(target, board)
    if header_definitions is None:
        header_definitions = compile_definitions
    if cmake_definitions is None:
        cmake_definitions = compile_definitions
    resolved = {
        "schemaVersion": 1,
        "target": target["id"],
        "board": board["id"],
        "provider": board["build"]["provider"],
        "providerBoard": board["build"].get("board"),
        "platform": target["build"].get("platform"),
        "idfTarget": target["build"].get("idfTarget"),
        "recipe": target["build"]["recipe"],
        "profileId": board["hal"]["profileId"],
        "selector": board["hal"]["selector"],
        "runtimeName": board["id"],
        "flashBytes": board["memory"]["flash"]["expectedBytes"],
        "psram": board["memory"].get("psram"),
        "components": components,
        "boardCompileDefinitions": cmake_definitions,
        "requestedFeatures": requested_features,
        "resolvedFeatures": resolved_features,
        "featureProvenance": feature_provenance,
        "resolvedFeaturesDigest": resolved_features_digest,
        "features": resolved_features,
        "featureHash": feature_hash,
        "contractSymbol": contract_symbol,
        "capabilities": board["capabilities"],
        "gpio": board["gpio"],
        "clockSources": board.get("clockSources", {}),
        "targetGpio": target["gpio"],
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
        f'set(JH_BOARD_COMPILE_DEFINITIONS "{";".join(cmake_definitions)}")',
        f'set(JH_BOARD_EXPECTED_FLASH_BYTES "{board["memory"]["flash"]["expectedBytes"]}")',
        f'set(JH_BOARD_REQUESTED_FEATURES "{";".join(requested_features)}")',
        f'set(JH_BOARD_RESOLVED_FEATURES "{";".join(resolved_features)}")',
        f'set(JH_BOARD_RESOLVED_FEATURES_DIGEST "{resolved_features_digest}")',
        f'set(JH_BOARD_FEATURES "{";".join(resolved_features)}")',
        f'set(JH_BOARD_INPUT_GLOBS "{";".join(generation_input_globs(boards_root))}")',
        f'set(JH_BOARD_FEATURE_HASH "{feature_hash}")',
        f'set(JH_BOARD_CONTRACT_SYMBOL "{contract_symbol}")',
    ]
    if "platform" in target["build"]:
        cmake_lines.append(f'set(PICO_PLATFORM "{target["build"]["platform"]}")')
    if "idfTarget" in target["build"]:
        cmake_lines.append(f'set(IDF_TARGET "{target["build"]["idfTarget"]}")')
    if "board" in board["build"]:
        cmake_lines.append(f'set(PICO_BOARD "{board["build"]["board"]}")')
    config_lines = [
        "#pragma once",
        "/* Generated by generate_board_config.py; do not edit. */",
        "#include <stdint.h>",
        *header_prelude,
    ]
    config_lines.extend(selected_target_fact_lines(target))
    config_lines.extend(
        selected_board_fact_lines(
            board,
            boards,
            capabilities,
            header_definitions,
            f'"{target["id"]}"',
            gpio_word_count([target]),
            define_selector=True,
            define_is_macros=True,
        )
    )
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
    inputs = [*generation_inputs(boards_root), *generation_extra]
    return {
        "jh_board_config.cmake": "\n".join(cmake_lines) + "\n",
        "jh_board_config.h": "\n".join(config_lines) + "\n",
        "jh_link_contract.h": link_header,
        "jh_link_contract_definition.c": link_definition,
        "jh_link_contract_reference.c": link_reference,
        "jh_board_resolved.json": json.dumps(resolved, indent=2, sort_keys=True) + "\n",
        "generation.d": "\n".join(path.as_posix() for path in inputs) + "\n",
    }


GENERATION_INPUT_PATTERN = "*.json"


def generation_input_roots(boards_root: Path | None = None) -> list[Path]:
    """Directories whose JSON files a generation reads: the descriptors under
    ``boards_root`` and the feature and tooling registries."""
    repository_root = Path(__file__).resolve().parents[1]
    return [
        (boards_root or repository_root / "boards").resolve(),
        (repository_root / "config").resolve(),
    ]


def generation_input_globs(boards_root: Path | None = None) -> list[str]:
    """Recursive patterns a build system watches to notice new input files."""
    return [
        f"{root.as_posix()}/{GENERATION_INPUT_PATTERN}"
        for root in generation_input_roots(boards_root)
    ]


def generation_inputs(boards_root: Path | None = None) -> list[Path]:
    """Every file one generation reads: the JSON files under the input roots
    and the generator's own modules."""
    scripts_dir = Path(__file__).resolve().parent
    modules = {
        Path(module.__file__).resolve()
        for module in list(sys.modules.values())
        if getattr(module, "__file__", None)
        and Path(module.__file__).resolve().parent == scripts_dir
    }
    return [
        *sorted(modules),
        *(
            path.resolve()
            for root in generation_input_roots(boards_root)
            for path in sorted(root.rglob(GENERATION_INPUT_PATTERN))
        ),
    ]


def require_managed_output(
    output_dir: Path, output_root: Path | None, repository_root: Path
) -> Path:
    """The resolved output directory, which must lie below a .build directory
    or the runtime-managed build root."""
    output_dir = output_dir.resolve()
    managed_root = (
        output_root.resolve()
        if output_root is not None
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
    return output_dir


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
    static_group = parser.add_mutually_exclusive_group()
    static_group.add_argument("--write-static", action="store_true")
    static_group.add_argument("--check-static", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    boards_root = args.boards_root.resolve()
    try:
        targets, boards, capabilities = load_registry(boards_root)
        normalize_features(args.requested_feature)
        validate_definitions(args.define)
        if args.write_static or args.check_static:
            if any(
                (
                    args.target,
                    args.board,
                    args.output_dir,
                    args.output_root,
                    args.requested_feature,
                    args.define,
                    args.validate_only,
                    args.list,
                    args.default_board,
                )
            ):
                raise DescriptorError(
                    "static generation does not accept build selection arguments"
                )
            repository_root = boards_root.parent
            outputs = static_generated_outputs(targets, boards, capabilities)
            if args.write_static:
                write_static_outputs(repository_root, outputs)
                return 0
            return 0 if check_static_outputs(repository_root, outputs) else 1
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
        output_dir = require_managed_output(
            args.output_dir, args.output_root, boards_root.parent
        )
        generate(
            target,
            board,
            boards,
            capabilities,
            output_dir,
            args.requested_feature,
            boards_root,
        )
    except DescriptorError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
