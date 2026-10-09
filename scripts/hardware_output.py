"""Write a resolved hardware model: the identity hash, jh_hardware.h and the
resolved JSON, in the forms the device tree specification fixes."""

from __future__ import annotations

import hashlib
import json
from typing import Any

from hardware_literals import (FORMATS, c_literal, c_string, exact_value, ieee_bits,
                               integer_value, macro_token, node_token)
import hardware_renderers as renderers

SCHEMA_VERSION = 1
DOMAIN_FLAGS = {None: 0, "soc-gpio": 1}   # every other domain is a device pin (2)
PARITY_FLAGS = {name: flag for name, (flag, _) in renderers.PARITY.items()}


class Literal:
    """A JSON number written with exactly these characters."""

    __slots__ = ("text",)

    def __init__(self, text: str) -> None:
        self.text = text


def exact_numbers(data: Any) -> Any:
    """Integers at their exact value, whatever their JSON spelling (1e2,
    9007199254740993.0); float and double properties keep their token."""
    if isinstance(data, dict):
        if "cType" in data and "value" in data:
            if data["cType"] in FORMATS or data["cType"] in ("bool", "string"):
                return dict(data)
            return {**data, "value": integer_value(data["value"])}
        return {key: exact_numbers(value) for key, value in data.items()}
    if isinstance(data, list):
        return [exact_numbers(item) for item in data]
    if isinstance(data, float):
        exact = exact_value(data)
        return exact.numerator if exact.denominator == 1 else data
    return data


def _floats(data: Any, hash_input: bool) -> Any:
    """Floating values as bit patterns for the hash, literal digits for JSON."""
    if isinstance(data, dict):
        if data.get("cType") in FORMATS and "value" in data:
            c_type, value = data["cType"], data["value"]
            if hash_input:
                return {**data, "value": ieee_bits(c_type, value)}
            return {**data, "value": Literal(c_literal(c_type, value).strip("()").rstrip("f"))}
        return {key: _floats(value, hash_input) for key, value in data.items()}
    if isinstance(data, list):
        return [_floats(item, hash_input) for item in data]
    if isinstance(data, float):
        return _floats({"cType": "double", "value": data}, hash_input)
    return data


def digest(data: Any) -> str:
    """SHA-256 of compact JSON with sorted keys and raw UTF-8; integers are
    exact and floats appear only as typed bit patterns."""
    text = json.dumps(_floats(exact_numbers(data), True), sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), allow_nan=False)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def hardware_digest(model: dict[str, Any]) -> str:
    return digest({"selection": model["selection"], "hardware": model["hardware"]})


def dump_json(data: Any) -> str:
    """Indented JSON with sorted keys; float and double values keep the
    literal's digits."""
    return _write(_floats(exact_numbers(data), False), "") + "\n"


def _write(data: Any, indent: str) -> str:
    inner = indent + "  "
    if isinstance(data, Literal):
        return data.text
    if isinstance(data, dict) and data:
        items = [f"{inner}{json.dumps(key, ensure_ascii=False)}: {_write(value, inner)}"
                 for key, value in sorted(data.items())]
        return "{\n" + ",\n".join(items) + f"\n{indent}}}"
    if isinstance(data, list) and data:
        return "[\n" + ",\n".join(inner + _write(item, inner) for item in data) + f"\n{indent}]"
    return json.dumps(data, ensure_ascii=False, allow_nan=False)


class Header:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def define(self, name: str, value: str, comment: str | None = None) -> None:
        self.lines.append(f"#define {name} {value}" + (f" /* {comment} */" if comment else ""))

    def blank(self, comment: str) -> None:
        self.lines += ["", f"/* {comment} */"]

    def deferred(self, name: str, c_text: str, cpp_text: str, comment: str) -> None:
        self.lines += [f"/* {comment} */", "#ifdef __cplusplus", f"#define {name} {cpp_text}",
                       "#else", f"#define {name} {c_text}", "#endif"]


def pin_macros(header: Header, prefix: str, endpoint: dict[str, Any] | None, owner: bool | None) -> None:
    domain = None if endpoint is None else endpoint["domain"]
    header.define(f"{prefix}_CONNECTED", "1" if endpoint else "0")
    header.define(f"{prefix}_DOMAIN", str(DOMAIN_FLAGS.get(domain, 2)), "0 none, 1 SoC GPIO, 2 device pin")
    if owner is not None:
        header.define(f"{prefix}_OWNER", "1" if owner and endpoint else "0")
    if endpoint is None:
        return
    encoded = renderers.encode_pin(endpoint)
    if encoded is not None:
        header.define(prefix, c_literal("uint8", encoded), "HAL pin encoding")
    if domain == "device-pin":
        header.define(f"{prefix}_PROVIDER", c_string(endpoint["node"]))
        header.define(f"{prefix}_INDEX", c_literal("uint32", endpoint["index"]))
    elif domain not in (None, "soc-gpio"):
        header.define(f"{prefix}_PROVIDER", c_string(f"board.{endpoint['component']}"))
        header.define(f"{prefix}_INDEX", c_literal("uint32", endpoint["id"]))


def render_header(model: dict[str, Any], capabilities: list[str]) -> str:
    """jh_hardware.h: only JH_HW_* macros, no includes and no HAL types."""
    selection, hardware = model["selection"], model["hardware"]
    header = Header()
    header.lines += [
        "/* Generated by generate_hardware_config.py; do not edit. */",
        "/* Early hardware facts of one assembly. Initializer macros expand only",
        " * after the HAL header named in their comment is included. */",
        "#pragma once",
        "",
    ]
    header.define("JH_HW_SCHEMA_VERSION", str(SCHEMA_VERSION))
    header.define("JH_HW_TARGET_NAME", c_string(selection["target"]))
    header.define("JH_HW_BOARD_NAME", c_string(selection["board"]))
    header.define("JH_HW_ASSEMBLY_NAME", c_string(selection["assembly"]))
    header.define("JH_HW_HARDWARE_SHA256", c_string(model["hardwareSha256"]))
    header.blank("effective capabilities")
    for capability in capabilities:
        header.define(f"JH_HW_HAS_{macro_token(capability)}",
                      "1" if hardware["capabilities"].get(capability) else "0")
    for name, bus in sorted(hardware["buses"].items()):
        render_bus(header, name, bus)
    for path, node in sorted(hardware["nodes"].items()):
        render_node(header, path, node, hardware)
    return "\n".join(header.lines) + "\n"


def render_bus(header: Header, name: str, bus: dict[str, Any]) -> None:
    prefix = f"JH_HW_BUS_{macro_token(name)}"
    header.blank(f"{bus['kind']} bus {name}")
    header.define(f"{prefix}_INDEX", c_literal("uint8", bus["index"]))
    for pin, endpoint in sorted(bus["pins"].items()):
        pin_macros(header, f"{prefix}_PIN_{macro_token(pin)}", endpoint, None)
    if bus["kind"] in ("spi", "i2c"):
        header.define(f"{prefix}_FREQUENCY_HZ", c_literal("uint32", bus["frequencyHz"]), "Hz")
        return
    frame = bus["frame"]
    header.define(f"{prefix}_UART_PORT", c_literal("uint8", bus["index"] + 1), "HAL_UART_PORT_<n>")
    header.define(f"{prefix}_BAUD_RATE", c_literal("uint32", bus["baudRate"]), "baud")
    header.define(f"{prefix}_DATA_BITS", c_literal("uint8", frame["dataBits"]))
    header.define(f"{prefix}_PARITY", c_literal("uint8", PARITY_FLAGS[frame["parity"]]),
                  "0 none, 1 even, 2 odd")
    header.define(f"{prefix}_STOP_BITS", c_literal("uint8", frame["stopBits"]))
    header.define(f"{prefix}_FRAME_CONFIG", renderers.uart_frame_token(frame),
                  "needs hal_uart_config.h, HAL_ENABLE_UART")


def render_node(header: Header, path: str, node: dict[str, Any], hardware: dict[str, Any]) -> None:
    prefix = f"JH_HW_NODE_{node_token(path)}"
    header.blank(f"{path}: {node['compatible']}")
    header.define(f"{prefix}_PRESENT", "1" if node["present"] else "0")
    rendered = renderers.config_init(path, node, hardware)
    header.define(f"{prefix}_CONFIG_AVAILABLE", "1" if rendered else "0")
    if not node["present"]:
        return
    for name, item in sorted(node["properties"].items()):
        value = item["value"]
        literal = c_string(value) if item["cType"] == "string" else c_literal(item["cType"], value)
        header.define(f"{prefix}_PROP_{macro_token(name)}", literal, item.get("unit"))
    for name, signal in sorted(node["signals"].items()):
        pin_macros(header, f"{prefix}_PIN_{macro_token(name)}", signal["endpoint"],
                   signal["owner"] == f"{path}.{name}")
    bus = node.get("bus")
    bus_item = hardware["buses"][bus] if isinstance(bus, str) else bus
    if bus_item is not None:
        header.define(f"{prefix}_BUS_INDEX", c_literal("uint8", bus_item["index"]))
    if node.get("controller") is not None:
        header.define(f"{prefix}_CONTROLLER_INDEX", c_literal("uint8", node["controller"]["index"]))
    if node.get("address") is not None:
        header.define(f"{prefix}_ADDRESS", c_literal("uint8", node["address"]), "7-bit I2C address")
    transport = node.get("transport")
    if transport:
        header.define(f"{prefix}_FREQUENCY_HZ", c_literal("uint32", transport["frequencyHz"]), "Hz")
        if "mode" in transport:
            header.define(f"{prefix}_SPI_MODE", c_literal("uint8", transport["mode"]))
            header.define(f"{prefix}_SPI_BIT_ORDER", "1U" if transport["bitOrder"] == "msb" else "0U",
                          "0 LSB first, 1 MSB first")
            header.deferred(f"{prefix}_SPI_SETTINGS_INIT", *renderers.spi_settings_init(transport),
                            "hal_spi_settings_t; needs hal_spi.h")
    if rendered:
        header.deferred(f"{prefix}_CONFIG_INIT", *rendered)
