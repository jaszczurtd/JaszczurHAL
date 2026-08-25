"""Validated access to repository tooling contracts under ``config/tooling``."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TOOLING_CONFIG_ROOT = REPOSITORY_ROOT / "config" / "tooling"


class ToolingContractError(ValueError):
    """A tooling contract is missing or malformed."""


def load_tooling_contract(name: str) -> dict[str, Any]:
    """Load one versioned tooling contract by its plain JSON filename."""
    if not name or Path(name).name != name or not name.endswith(".json"):
        raise ToolingContractError(f"invalid tooling contract name: {name!r}")
    path = TOOLING_CONFIG_ROOT / name
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ToolingContractError(f"cannot load {path}: {error}") from error
    if not isinstance(document, dict):
        raise ToolingContractError(f"{path}: root must be an object")
    if document.get("schemaVersion") != 1:
        raise ToolingContractError(f"{path}: schemaVersion must be 1")
    return document


def require_object(
    document: dict[str, Any], field: str, *, source: str
) -> dict[str, Any]:
    """Return a required object field with a stable contract error."""
    value = document.get(field)
    if not isinstance(value, dict):
        raise ToolingContractError(f"{source}: {field} must be an object")
    return value


def require_list(
    document: dict[str, Any], field: str, *, source: str
) -> list[Any]:
    """Return a required array field with a stable contract error."""
    value = document.get(field)
    if not isinstance(value, list):
        raise ToolingContractError(f"{source}: {field} must be an array")
    return value


def require_string(value: Any, field: str, *, source: str) -> str:
    """Return a required non-empty string value."""
    if not isinstance(value, str) or not value:
        raise ToolingContractError(f"{source}: {field} must be a non-empty string")
    return value


def require_string_list(value: Any, field: str, *, source: str) -> tuple[str, ...]:
    """Return an array of unique, non-empty strings."""
    if (
        not isinstance(value, list)
        or any(not isinstance(item, str) or not item for item in value)
        or len(set(value)) != len(value)
    ):
        raise ToolingContractError(
            f"{source}: {field} must contain unique non-empty strings"
        )
    return tuple(value)
