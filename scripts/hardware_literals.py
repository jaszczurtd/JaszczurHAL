"""C spelling of hardware values: macro tokens, typed literals, hash values.

The device tree specification fixes these forms for every generated header.
A floating value is rounded once from its exact decimal input to the declared
IEEE-754 type; hardware_schema.load_json keeps that input as the token of a
JsonNumber. Integers keep their exact value, uint64 included.
"""

from __future__ import annotations

from decimal import Decimal
from fractions import Fraction
import math
import re
import struct
from typing import Any

from hardware_schema import JsonNumber

# Significand bits (with the hidden one), exponent range and struct format.
FORMATS = {"float": (24, -126, 127, ">f"), "double": (53, -1022, 1023, ">d")}


class LiteralError(ValueError):
    """A value the declared C type cannot hold."""


def macro_token(name: str) -> str:
    """Token conversion of the device tree specification."""
    text = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    text = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", text)
    text = re.sub(r"[^A-Za-z0-9]+", "_", text)
    return re.sub(r"_+", "_", text).strip("_").upper()


def node_token(path: str) -> str:
    """Macro token of a node path: each segment converted, joined with _."""
    return "_".join(macro_token(part) for part in path.split("."))


def c_string(value: str) -> str:
    """UTF-8 C literal. A byte outside printable ASCII becomes a three-digit
    octal escape, which cannot take in the next character the way \\x takes
    a following hex digit; '?' is escaped so no trigraph forms."""
    escaped = []
    for byte in value.encode("utf-8"):
        char = chr(byte)
        if char in '"\\?':
            escaped.append("\\" + char)
        elif 0x20 <= byte < 0x7F:
            escaped.append(char)
        else:
            escaped.append(f"\\{byte:03o}")
    return '"' + "".join(escaped) + '"'


def exact_value(value: Any) -> Fraction:
    """The exact number a JSON value stands for."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise LiteralError(f"{value!r} is not a number")
    if isinstance(value, JsonNumber):
        return Fraction(Decimal(value.token))
    if isinstance(value, float) and not math.isfinite(value):
        raise LiteralError(f"{value} is not finite")
    return Fraction(value)


def integer_value(value: Any) -> int:
    exact = exact_value(value)
    if exact.denominator != 1:
        raise LiteralError(f"{getattr(value, 'token', value)} is not an integer")
    return exact.numerator


def ieee_value(c_type: str, value: Any) -> float:
    """Round the exact value once to binary32 or binary64, ties to even.
    Overflow and a nonzero value rounding to zero fail; -0 becomes +0."""
    digits, low, high, _ = FORMATS[c_type]
    exact = exact_value(value)
    if exact == 0:
        return 0.0
    shown = getattr(value, "token", value)
    size = abs(exact)
    exponent = size.numerator.bit_length() - size.denominator.bit_length()
    if size < Fraction(2) ** exponent:
        exponent -= 1
    exponent = max(exponent, low)
    quantum = Fraction(2) ** (exponent - digits + 1)
    significand = round(size / quantum)
    if significand == 2 ** digits:
        significand, exponent, quantum = 2 ** (digits - 1), exponent + 1, quantum * 2
    if exponent > high:
        raise LiteralError(f"{shown} overflows {c_type}")
    if significand == 0:
        raise LiteralError(f"{shown} rounds to zero in {c_type}")
    result = float(significand * quantum)
    return -result if exact < 0 else result


def ieee_bits(c_type: str, value: Any) -> str:
    """Bit pattern of the rounded value, lowercase hex, most significant first."""
    return struct.pack(FORMATS[c_type][3], ieee_value(c_type, value)).hex()


def c_literal(c_type: str, value: Any) -> str:
    """C spelling of one value; the caller checks the range first."""
    if c_type == "bool":
        return "1" if value else "0"
    if c_type == "string":
        return c_string(value)
    if c_type in FORMATS:
        rounded = ieee_value(c_type, value)
        text = f"{rounded:.8e}f" if c_type == "float" else f"{rounded:.16e}"
        return f"({text})" if rounded < 0 else text
    number = integer_value(value)
    if c_type.startswith("uint"):
        return f"{number}{'ULL' if c_type == 'uint64' else 'U'}"
    if c_type == "int32" and number == -2**31:
        return "(-2147483647 - 1)"
    if c_type == "int64" and number == -2**63:
        return "(-9223372036854775807LL - 1LL)"
    text = f"{number}{'LL' if c_type == 'int64' else ''}"
    return f"({text})" if number < 0 else text
