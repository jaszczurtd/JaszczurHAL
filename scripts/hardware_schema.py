"""Validate hardware description JSON against config/hardware/*.schema.json.

A dependency-free reader for the JSON Schema 2020-12 keywords these schemas
use. Errors carry the JSON Pointer the device tree specification names: the
failing leaf, an extra field itself, or the would-be location of a missing
required field. A value outside the supported keywords fails loudly instead
of being accepted.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
import json
import math
from pathlib import Path
import re
from typing import Any, Iterator

REPO_ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = REPO_ROOT / "config" / "hardware"

SUPPORTED = {
    "$schema", "$defs", "$ref", "title", "description", "type", "properties",
    "required", "additionalProperties", "propertyNames", "enum", "const",
    "pattern", "minimum", "maximum", "minLength", "items", "minItems",
    "uniqueItems", "minProperties", "maxProperties", "allOf", "oneOf", "not",
    "if", "then",
}


@dataclass(frozen=True)
class SchemaError:
    pointer: str
    message: str


class InputError(ValueError):
    """Input JSON that the specification rejects before schema validation."""


def pointer_join(base: str, token: Any) -> str:
    text = str(token).replace("~", "~0").replace("/", "~1")
    return f"{base}/{text}"


def pointer_key(pointer: str) -> tuple:
    """Order pointers by Unicode code point, array indices numerically."""
    parts = []
    for token in pointer.split("/")[1:]:
        parts.append((0, int(token), "") if token.isdigit() else (1, 0, token))
    return tuple(parts)


def load_json(path: Path) -> Any:
    """Read UTF-8 JSON with unique keys, finite numbers and paired surrogates
    only. A decimal number must keep its magnitude in binary64: one that would
    become infinite or zero fails instead."""

    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in items:
            if key in result:
                raise InputError(f"{path}: duplicate key {key!r}")
            result[key] = value
        return result

    def constant(name: str) -> Any:
        raise InputError(f"{path}: non-finite number {name}")

    def number(token: str) -> float:
        value = float(token)
        if not math.isfinite(value) or (value == 0.0 and Decimal(token) != 0):
            raise InputError(f"{path}: number {token} is outside the binary64 range")
        return value

    text = path.read_text(encoding="utf-8")
    data = json.loads(text, object_pairs_hook=pairs, parse_constant=constant, parse_float=number)
    try:
        json.dumps(data, ensure_ascii=False).encode("utf-8")
    except UnicodeEncodeError as error:
        raise InputError(f"{path}: unpaired surrogate in a string") from error
    return data


def json_equal(left: Any, right: Any) -> bool:
    """JSON equality: booleans differ from numbers, 1 equals 1.0."""
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return left == right
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(
            json_equal(a, b) for a, b in zip(left, right))
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(
            json_equal(left[key], right[key]) for key in left)
    return type(left) is type(right) and left == right


def type_matches(value: Any, name: str) -> bool:
    if name == "null":
        return value is None
    if name == "boolean":
        return isinstance(value, bool)
    if name == "string":
        return isinstance(value, str)
    if name == "array":
        return isinstance(value, list)
    if name == "object":
        return isinstance(value, dict)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    if name == "number":
        return isinstance(value, int) or math.isfinite(value)
    if name == "integer":
        return isinstance(value, int) or (math.isfinite(value) and value == int(value))
    raise ValueError(f"unsupported schema type {name!r}")


class SchemaSet:
    """The hardware schemas, resolving references between their files."""

    def __init__(self, directory: Path = SCHEMA_DIR) -> None:
        self.documents: dict[str, Any] = {}
        for path in sorted(directory.glob("*.schema.json")):
            document = load_json(path)
            self._check_keywords(path.name, document)
            self.documents[path.name] = document

    def _check_keywords(self, name: str, node: Any) -> None:
        if isinstance(node, dict):
            unknown = set(node) - SUPPORTED
            if unknown:
                raise ValueError(f"{name}: unsupported schema keywords {sorted(unknown)}")
            for key, value in node.items():
                if key in ("properties", "$defs"):
                    for child in value.values():
                        self._check_keywords(name, child)
                elif key in ("enum", "const", "required"):
                    continue
                else:
                    self._check_keywords(name, value)
        elif isinstance(node, list):
            for item in node:
                self._check_keywords(name, item)

    def resolve(self, document: str, reference: str) -> tuple[str, Any]:
        target, _, fragment = reference.partition("#")
        name = target or document
        node = self.documents[name]
        for token in [t for t in fragment.split("/") if t]:
            node = node[token.replace("~1", "/").replace("~0", "~")]
        return name, node

    def validate(self, schema_name: str, instance: Any) -> list[SchemaError]:
        """Return every error; sort with pointer_key for the first one."""
        return list(self._errors(schema_name, self.documents[schema_name], instance, ""))

    def first_error(self, schema_name: str, instance: Any) -> SchemaError | None:
        errors = self.validate(schema_name, instance)
        return min(errors, key=lambda error: pointer_key(error.pointer)) if errors else None

    def _errors(self, doc: str, schema: Any, value: Any, at: str,
                keep: frozenset[str] = frozenset()) -> Iterator[SchemaError]:
        if schema is True or schema == {}:
            return
        if schema is False:
            yield SchemaError(at, "no value is allowed here")
            return
        if "$ref" in schema:
            ref_doc, ref_schema = self.resolve(doc, schema["$ref"])
            yield from self._errors(ref_doc, ref_schema, value, at)
        if "type" in schema:
            names = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
            if not any(type_matches(value, name) for name in names):
                yield SchemaError(at, f"type: expected {' or '.join(names)}")
                return
        if "const" in schema and not json_equal(value, schema["const"]):
            yield SchemaError(at, f"expected {schema['const']!r}")
        if "enum" in schema and not any(json_equal(value, item) for item in schema["enum"]):
            yield SchemaError(at, f"expected one of {schema['enum']!r}")
        if isinstance(value, str):
            if "minLength" in schema and len(value) < schema["minLength"]:
                yield SchemaError(at, f"shorter than {schema['minLength']}")
            if "pattern" in schema and re.search(schema["pattern"], value) is None:
                yield SchemaError(at, f"does not match {schema['pattern']!r}")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                yield SchemaError(at, f"below {schema['minimum']}")
            if "maximum" in schema and value > schema["maximum"]:
                yield SchemaError(at, f"above {schema['maximum']}")
        if isinstance(value, list):
            yield from self._array_errors(doc, schema, value, at)
        if isinstance(value, dict):
            yield from self._object_errors(doc, schema, value, at, keep)
        for part in schema.get("allOf", []):
            yield from self._errors(doc, part, value, at)
        if "oneOf" in schema:
            yield from self._one_of_errors(doc, schema["oneOf"], value, at)
        if "not" in schema and not list(self._errors(doc, schema["not"], value, at)):
            yield SchemaError(at, "matches a forbidden form")
        if "if" in schema and not list(self._errors(doc, schema["if"], value, at)):
            if "then" in schema:
                # Fields the condition names stay; extra-field errors point past them.
                condition = schema["if"]
                named = frozenset(condition.get("required", [])) | frozenset(condition.get("properties", {}))
                yield from self._errors(doc, schema["then"], value, at, named)

    def _array_errors(self, doc: str, schema: dict, value: list, at: str) -> Iterator[SchemaError]:
        if "minItems" in schema and len(value) < schema["minItems"]:
            yield SchemaError(at, f"fewer than {schema['minItems']} items")
        if schema.get("uniqueItems"):
            for index, item in enumerate(value):
                if any(json_equal(item, earlier) for earlier in value[:index]):
                    yield SchemaError(pointer_join(at, index), "duplicate item")
        if "items" in schema:
            for index, item in enumerate(value):
                yield from self._errors(doc, schema["items"], item, pointer_join(at, index))

    def _object_errors(self, doc: str, schema: dict, value: dict, at: str,
                       keep: frozenset[str]) -> Iterator[SchemaError]:
        for key in schema.get("required", []):
            if key not in value:
                yield SchemaError(pointer_join(at, key), "required field is missing")
        properties = schema.get("properties", {})
        for key, item in value.items():
            if key in properties:
                yield from self._errors(doc, properties[key], item, pointer_join(at, key))
            elif "additionalProperties" in schema:
                extra = schema["additionalProperties"]
                if extra is False:
                    yield SchemaError(pointer_join(at, key), "unknown field")
                else:
                    yield from self._errors(doc, extra, item, pointer_join(at, key))
            if "propertyNames" in schema:
                for error in self._errors(doc, schema["propertyNames"], key, at):
                    yield SchemaError(pointer_join(at, key), f"field name: {error.message}")
        if "minProperties" in schema and len(value) < schema["minProperties"]:
            yield SchemaError(at, f"fewer than {schema['minProperties']} fields")
        if "maxProperties" in schema and len(value) > schema["maxProperties"]:
            # The extra fields are the leaves: point to the first field, in
            # code point order, that the enclosing condition does not name.
            extras = sorted(key for key in value if key not in keep)
            target = pointer_join(at, extras[0]) if extras else at
            yield SchemaError(target, f"more than {schema['maxProperties']} fields")

    def _one_of_errors(self, doc: str, options: list, value: Any, at: str) -> Iterator[SchemaError]:
        results = [list(self._errors(doc, option, value, at)) for option in options]
        passing = sum(1 for errors in results if not errors)
        if passing == 1:
            return
        if passing > 1:
            yield SchemaError(at, "matches more than one form")
            return
        # Report the branch that accepts the value's type; otherwise the value.
        typed = [errors for errors in results
                 if not any(error.pointer == at and error.message.startswith("type: ")
                            for error in errors)]
        if len(typed) == 1:
            yield from typed[0]
        else:
            yield SchemaError(at, "matches none of the allowed forms")
